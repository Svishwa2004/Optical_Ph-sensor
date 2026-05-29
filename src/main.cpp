#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Adafruit_TCS34725.h"
#include <math.h>

// ----- Pinout -----
static const uint8_t I2C_SDA = 21;
static const uint8_t I2C_SCL = 22;
static const uint8_t LED_PIN = 2;
static const uint8_t PUMP1_PIN = 4;
static const uint8_t PUMP2_PIN = 32;
static const uint8_t PUMP3_PIN = 18;

// ----- PWM (Pump 2) -----
static const uint8_t PUMP2_PWM_CHANNEL = 0;
static const uint16_t PUMP2_PWM_FREQ = 5000;
static const uint8_t PUMP2_PWM_RES = 8;
static const uint8_t PUMP2_DUTY = 89; // 35% of 255

// ----- PWM (Drain Pump) -----
static const uint8_t PUMP3_PWM_CHANNEL = 2;
static const uint16_t PUMP3_PWM_FREQ = 5000;
static const uint8_t PUMP3_PWM_RES = 8;
static const uint8_t DRAIN_DUTY_DEFAULT_PERCENT = 40;
static uint8_t drainDutyPercent = DRAIN_DUTY_DEFAULT_PERCENT;
static uint8_t drainPwmDuty = (DRAIN_DUTY_DEFAULT_PERCENT * 255 + 50) / 100;

// ----- Timing (ms) -----
static const uint32_t BASE_FILL_MS_DEFAULT = 33000;
static const uint16_t BASE_FILL_SEC_MIN = 5;
static const uint16_t BASE_FILL_SEC_MAX = 120;
static uint32_t baseFillMs = BASE_FILL_MS_DEFAULT;
static const uint32_t BLANKING_WARMUP_MS = 2000;
static const uint32_t BLANKING_SAMPLE_MS = 1000;
static const uint32_t MICRO_DOSE_MS = 5500;
static const uint32_t AGITATION_MS = 1780;
static const uint32_t DIFFUSION_MS = 15000;
static const uint32_t MEASURE_WARMUP_MS = 1000;
static const uint32_t MEASURE_SAMPLE_MS = 1000;
static const uint32_t DRAIN_MS = 3000;
static const uint32_t COOL_DOWN_MS = 10000;
static const uint32_t IDLE_MS = 3600000;

static const uint8_t SAMPLE_COUNT = 10;
static const uint16_t SAMPLE_SPACING_MS = 100;

// ----- Calibration (placeholder values) -----
static const float CAL_RATIO_PH4 = 1.60f;
static const float CAL_RATIO_PH55 = 1.10f;
static const float CAL_RATIO_PH7 = 0.70f;

// ----- WiFi (hardcoded) -----
static const char *WIFI_SSID = "Sahan’s iPhone";
static const char *WIFI_PASS = "1234567889";
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t WIFI_RETRY_MS = 10000;

// ----- Sensor -----
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
    TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);

// ----- Web -----
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
static bool fsReady = false;

// ----- FSM -----
enum class ProcessState : uint8_t {
    IDLE = 0,
    BASE_FILL = 1,
    BLANKING = 2,
    MICRO_DOSE = 3,
    AGITATION = 4,
    DIFFUSION = 5,
    MEASURE = 6,
  DRAIN = 7,
  COOL_DOWN = 8
};

struct RawReading {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t c;
};

struct Absorbance {
    float r;
    float g;
    float b;
};

static ProcessState currentState = ProcessState::IDLE;
static unsigned long stateStartMs = 0;
static unsigned long lastBroadcastMs = 0;
static unsigned long lastSensorCheckMs = 0;
static unsigned long lastWifiAttemptMs = 0;

static bool sensorConnected = false;
static bool baselineValid = false;
static bool sampleValid = false;
static bool wifiConnected = false;

static RawReading baseline = {0, 0, 0, 0};
static RawReading sample = {0, 0, 0, 0};
static Absorbance lastAbs = {NAN, NAN, NAN};
static float lastRatio = NAN;
static float lastPh = NAN;

// ----- Web UI -----
// Served from LittleFS at /index.html.

// ----- Utilities -----
static uint8_t clampPercent(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

static uint16_t clampSeconds(int value, uint16_t minValue, uint16_t maxValue) {
  if (value < static_cast<int>(minValue)) return minValue;
  if (value > static_cast<int>(maxValue)) return maxValue;
  return static_cast<uint16_t>(value);
}

static uint8_t percentToDuty(uint8_t percent) {
  return static_cast<uint8_t>((percent * 255 + 50) / 100);
}

static void setBaseFillSeconds(int seconds) {
  uint16_t clamped = clampSeconds(seconds, BASE_FILL_SEC_MIN, BASE_FILL_SEC_MAX);
  baseFillMs = static_cast<uint32_t>(clamped) * 1000;
}

static void setDrainDutyPercent(uint8_t percent) {
  drainDutyPercent = percent;
  drainPwmDuty = percentToDuty(percent);
  if (currentState == ProcessState::DRAIN) {
    ledcWrite(PUMP3_PWM_CHANNEL, drainPwmDuty);
  }
}

static void setAllOutputsOff() {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(PUMP1_PIN, LOW);
    ledcWrite(PUMP2_PWM_CHANNEL, 0);
  ledcWrite(PUMP3_PWM_CHANNEL, 0);
}

static void pump1On(bool on) {
    digitalWrite(PUMP1_PIN, on ? HIGH : LOW);
}

static void pump2On(bool on) {
    ledcWrite(PUMP2_PWM_CHANNEL, on ? PUMP2_DUTY : 0);
}

static void pump3On(bool on) {
  ledcWrite(PUMP3_PWM_CHANNEL, on ? drainPwmDuty : 0);
}

static void ledOn(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

static RawReading readAverageRaw(uint8_t samples, uint16_t spacingMs) {
    RawReading out = {0, 0, 0, 0};
    if (samples == 0) return out;

    uint32_t sumR = 0;
    uint32_t sumG = 0;
    uint32_t sumB = 0;
    uint32_t sumC = 0;

    for (uint8_t i = 0; i < samples; i++) {
        uint16_t r, g, b, c;
        tcs.getRawData(&r, &g, &b, &c);
        sumR += r;
        sumG += g;
        sumB += b;
        sumC += c;
        delay(spacingMs);
    }

    out.r = sumR / samples;
    out.g = sumG / samples;
    out.b = sumB / samples;
    out.c = sumC / samples;
    return out;
}

static float safeTransmittance(float test, float base) {
    if (base <= 0.0f) return 0.0001f;
    float t = test / base;
    if (t < 0.0001f) t = 0.0001f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

static Absorbance computeAbsorbance(const RawReading &base, const RawReading &test) {
    Absorbance a;
    a.r = -log10f(safeTransmittance((float)test.r, (float)base.r));
    a.g = -log10f(safeTransmittance((float)test.g, (float)base.g));
    a.b = -log10f(safeTransmittance((float)test.b, (float)base.b));
    return a;
}

static float computeRatio(const Absorbance &a) {
    if (a.g <= 0.0f || isnan(a.g)) return NAN;
    return a.b / a.g;
}

static float mapRatioToPH(float ratio) {
    if (isnan(ratio)) return NAN;

    if (ratio >= CAL_RATIO_PH4) return 4.0f;
    if (ratio <= CAL_RATIO_PH7) return 7.0f;

    if (ratio >= CAL_RATIO_PH55) {
        float t = (ratio - CAL_RATIO_PH55) / (CAL_RATIO_PH4 - CAL_RATIO_PH55);
        return 5.5f - t * 1.5f;
    }

    float t = (ratio - CAL_RATIO_PH7) / (CAL_RATIO_PH55 - CAL_RATIO_PH7);
    return 7.0f - t * 1.5f;
}

static const char *getStateLabel(ProcessState state) {
    switch (state) {
        case ProcessState::BASE_FILL: return "Base Fill";
        case ProcessState::BLANKING: return "Dynamic Blanking";
        case ProcessState::MICRO_DOSE: return "Micro Dose";
        case ProcessState::AGITATION: return "Agitation Blast";
        case ProcessState::DIFFUSION: return "Diffusion Pause";
        case ProcessState::MEASURE: return "Measurement";
        case ProcessState::DRAIN: return "Vacuum Drain";
    case ProcessState::COOL_DOWN: return "Cool Down";
        default: return "Idle";
    }
}

static const char *getStateAction(ProcessState state) {
    switch (state) {
        case ProcessState::BASE_FILL: return "Filling chamber";
        case ProcessState::BLANKING: return "Zeroing baseline color";
        case ProcessState::MICRO_DOSE: return "Injecting dye";
        case ProcessState::AGITATION: return "High velocity mixing";
        case ProcessState::DIFFUSION: return "Stabilizing solution";
        case ProcessState::MEASURE: return "Reading optical color";
        case ProcessState::DRAIN: return "Evacuating chamber";
    case ProcessState::COOL_DOWN: return "Cooling pump";
        default: return "Idle wait";
    }
}

static uint32_t getStateDuration(ProcessState state) {
    switch (state) {
    case ProcessState::BASE_FILL: return baseFillMs;
        case ProcessState::BLANKING: return BLANKING_WARMUP_MS + BLANKING_SAMPLE_MS;
        case ProcessState::MICRO_DOSE: return MICRO_DOSE_MS;
        case ProcessState::AGITATION: return AGITATION_MS;
        case ProcessState::DIFFUSION: return DIFFUSION_MS;
        case ProcessState::MEASURE: return MEASURE_WARMUP_MS + MEASURE_SAMPLE_MS;
        case ProcessState::DRAIN: return DRAIN_MS;
    case ProcessState::COOL_DOWN: return COOL_DOWN_MS;
        case ProcessState::IDLE: return IDLE_MS;
        default: return 0;
    }
}

static String buildStatusJson() {
  JsonDocument doc;

    unsigned long now = millis();
    uint32_t duration = getStateDuration(currentState);
    uint32_t elapsed = now - stateStartMs;
    float progress = duration > 0 ? (float)elapsed / (float)duration : 0.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    doc["state"] = getStateLabel(currentState);
    doc["stateId"] = (uint8_t)currentState;
    doc["action"] = getStateAction(currentState);
    doc["elapsedMs"] = elapsed;
    doc["durationMs"] = duration;
    doc["progress"] = progress;
    doc["remainingMs"] = duration > elapsed ? (duration - elapsed) : 0;
    doc["baseFillMs"] = baseFillMs;
    doc["baseFillSec"] = baseFillMs / 1000;
    doc["baselineValid"] = baselineValid;
    doc["sampleValid"] = sampleValid;
    doc["drainDutyPercent"] = drainDutyPercent;
    doc["drainDutyRaw"] = drainPwmDuty;
    doc["sensorConnected"] = sensorConnected;
    doc["canStart"] = sensorConnected && currentState == ProcessState::IDLE;

    if (isnan(lastPh)) {
        doc["ph"] = nullptr;
    } else {
        doc["ph"] = lastPh;
    }

    if (isnan(lastRatio)) {
        doc["ratio"] = nullptr;
    } else {
        doc["ratio"] = lastRatio;
    }

    JsonObject baselineObj = doc["baseline"].to<JsonObject>();
    baselineObj["r"] = baseline.r;
    baselineObj["g"] = baseline.g;
    baselineObj["b"] = baseline.b;

    JsonObject sampleObj = doc["sample"].to<JsonObject>();
    sampleObj["r"] = sample.r;
    sampleObj["g"] = sample.g;
    sampleObj["b"] = sample.b;

    JsonObject absObj = doc["absorbance"].to<JsonObject>();
    if (isnan(lastAbs.r)) {
      absObj["r"] = nullptr;
    } else {
      absObj["r"] = lastAbs.r;
    }
    if (isnan(lastAbs.g)) {
      absObj["g"] = nullptr;
    } else {
      absObj["g"] = lastAbs.g;
    }
    if (isnan(lastAbs.b)) {
      absObj["b"] = nullptr;
    } else {
      absObj["b"] = lastAbs.b;
    }

    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    wifiObj["ssid"] = WiFi.SSID();
    wifiObj["ip"] = WiFi.localIP().toString();
    wifiObj["rssi"] = WiFi.RSSI();

    doc["uptimeMs"] = now;

    String out;
    serializeJson(doc, out);
    return out;
}

static void broadcastStatus() {
    String payload = buildStatusJson();
    ws.textAll(payload);
}

static void sendStatusToClient(AsyncWebSocketClient *client) {
    String payload = buildStatusJson();
    client->text(payload);
}

static void setState(ProcessState next) {
    setAllOutputsOff();
    currentState = next;
    stateStartMs = millis();

    if (next == ProcessState::BASE_FILL) {
        pump1On(true);
    } else if (next == ProcessState::BLANKING) {
        ledOn(true);
        baselineValid = false;
    } else if (next == ProcessState::MICRO_DOSE) {
        pump2On(true);
    } else if (next == ProcessState::AGITATION) {
        pump1On(true);
    } else if (next == ProcessState::MEASURE) {
        ledOn(true);
        sampleValid = false;
    } else if (next == ProcessState::DRAIN) {
        pump3On(true);
    }

    broadcastStatus();
}

static void handleWebsocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    sendStatusToClient(client);
    return;
  }

  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) {
    return;
  }

  String payload;
  payload.reserve(len + 1);
  for (size_t i = 0; i < len; i++) {
    payload += (char)data[i];
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    return;
  }

  const char *action = doc["action"];
  if (action && strcmp(action, "start") == 0) {
    if (sensorConnected && currentState == ProcessState::IDLE) {
      setState(ProcessState::BASE_FILL);
    } else {
      sendStatusToClient(client);
    }
  }
}

static void setupServer() {
    ws.onEvent(handleWebsocketEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!fsReady || !LittleFS.exists("/index.html")) {
        request->send(404, "text/plain", "Not Found");
        return;
      }
      request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String payload = buildStatusJson();
        request->send(200, "application/json", payload);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (request->hasParam("baseFillSec")) {
        int seconds = request->getParam("baseFillSec")->value().toInt();
        setBaseFillSeconds(seconds);
      }

      if (request->hasParam("drainDuty")) {
        int percent = request->getParam("drainDuty")->value().toInt();
        setDrainDutyPercent(clampPercent(percent));
      }

      JsonDocument doc;
      doc["baseFillMs"] = baseFillMs;
      doc["baseFillSec"] = baseFillMs / 1000;
      doc["drainDutyPercent"] = drainDutyPercent;
      doc["drainDutyRaw"] = drainPwmDuty;
      String payload;
      serializeJson(doc, payload);
      request->send(200, "application/json", payload);
    });

    server.begin();
}

static void runStateMachine() {
    unsigned long now = millis();

    switch (currentState) {
        case ProcessState::BASE_FILL:
          if (now - stateStartMs >= baseFillMs) {
                setState(ProcessState::BLANKING);
            }
            break;

        case ProcessState::BLANKING:
            if (!baselineValid && (now - stateStartMs >= BLANKING_WARMUP_MS)) {
                baseline = readAverageRaw(SAMPLE_COUNT, SAMPLE_SPACING_MS);
                baselineValid = true;
                ledOn(false);
                setState(ProcessState::MICRO_DOSE);
            }
            break;

        case ProcessState::MICRO_DOSE:
            if (now - stateStartMs >= MICRO_DOSE_MS) {
                setState(ProcessState::AGITATION);
            }
            break;

        case ProcessState::AGITATION:
            if (now - stateStartMs >= AGITATION_MS) {
                setState(ProcessState::DIFFUSION);
            }
            break;

        case ProcessState::DIFFUSION:
            if (now - stateStartMs >= DIFFUSION_MS) {
                setState(ProcessState::MEASURE);
            }
            break;

        case ProcessState::MEASURE:
            if (!sampleValid && (now - stateStartMs >= MEASURE_WARMUP_MS)) {
                sample = readAverageRaw(SAMPLE_COUNT, SAMPLE_SPACING_MS);
                sampleValid = true;
                ledOn(false);

                if (baselineValid) {
                    lastAbs = computeAbsorbance(baseline, sample);
                    lastRatio = computeRatio(lastAbs);
                    lastPh = mapRatioToPH(lastRatio);
                }

                setState(ProcessState::DRAIN);
            }
            break;

        case ProcessState::DRAIN:
            if (now - stateStartMs >= DRAIN_MS) {
            setState(ProcessState::COOL_DOWN);
            }
            break;

        case ProcessState::COOL_DOWN:
          if (now - stateStartMs >= COOL_DOWN_MS) {
            setState(ProcessState::IDLE);
          }
          break;

        case ProcessState::IDLE:
        default:
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(LED_PIN, OUTPUT);
    pinMode(PUMP1_PIN, OUTPUT);
    pinMode(PUMP2_PIN, OUTPUT);
    pinMode(PUMP3_PIN, OUTPUT);
    setAllOutputsOff();

    ledcSetup(PUMP2_PWM_CHANNEL, PUMP2_PWM_FREQ, PUMP2_PWM_RES);
    ledcAttachPin(PUMP2_PIN, PUMP2_PWM_CHANNEL);
    ledcWrite(PUMP2_PWM_CHANNEL, 0);

    ledcSetup(PUMP3_PWM_CHANNEL, PUMP3_PWM_FREQ, PUMP3_PWM_RES);
    ledcAttachPin(PUMP3_PIN, PUMP3_PWM_CHANNEL);
    ledcWrite(PUMP3_PWM_CHANNEL, 0);

    Wire.begin(I2C_SDA, I2C_SCL);

    Serial.println("Optical pH Sensor booting...");
    sensorConnected = tcs.begin();

    if (!sensorConnected) {
        Serial.println("TCS34725 not found. Will retry.");
    } else {
        tcs.setInterrupt(true);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastWifiAttemptMs = millis();

    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi connect timeout. Will retry in background.");
    }

    fsReady = LittleFS.begin();
    if (!fsReady) {
      Serial.println("LittleFS mount failed.");
    }

    setupServer();

    setState(ProcessState::IDLE);
}

void loop() {
    ws.cleanupClients();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
    }
  } else {
    wifiConnected = false;
    unsigned long now = millis();
    if (now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
      lastWifiAttemptMs = now;
      Serial.println("Retrying WiFi connection...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

    if (!sensorConnected) {
        unsigned long now = millis();
        if (now - lastSensorCheckMs >= 2000) {
            lastSensorCheckMs = now;
            if (tcs.begin()) {
                sensorConnected = true;
                tcs.setInterrupt(true);
                Serial.println("TCS34725 connected.");
              setState(ProcessState::IDLE);
            }
        }
        delay(10);
        return;
    }

    runStateMachine();

    unsigned long now = millis();
    if (now - lastBroadcastMs >= 1000) {
        lastBroadcastMs = now;
        broadcastStatus();
    }
}
