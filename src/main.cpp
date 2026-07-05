#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "Adafruit_TCS34725.h"
#include <LiquidCrystal_I2C.h>          // HD44780 1602 via PCF8574 I2C backpack
#include <cmath>
#include <math.h>
#include <esp_task_wdt.h>

// ----- Pinout -----
static const uint8_t I2C_SDA = 21;
static const uint8_t I2C_SCL = 22;
static const uint8_t LED_PIN = 2;
static const uint8_t PUMP1_PIN = 4;
static const uint8_t PUMP2_PIN = 32;
static const uint8_t PUMP3_PIN = 18;
static const uint8_t BUTTON_PIN = 34; // START_BTN (input-only on ESP32)

// ----- PWM (Pump 2) -----
static const uint8_t  PUMP2_PWM_CHANNEL = 0;
static const uint16_t PUMP2_PWM_FREQ    = 25000; // 25 kHz — above audible range, eliminates motor coil whine
static const uint8_t  PUMP2_PWM_RES     = 8;
// 35% duty electronically gear-reduces NKP native 37 mL/min to ~12.95 mL/min
// (per project document Section 2.1: "electronically geared down to run incredibly slowly (~13 mL/min)")
// 35% of 255 = 89 (raw duty count)
static const uint8_t  PUMP2_DUTY        = 89;  // 35% of 255
static const uint8_t  PUMP2_KICK_DUTY   = 255; // 100% — kick-start torque burst
static const uint32_t PUMP2_KICK_MS     = 250; // kick-start duration (ms)

// ----- PWM (Drain Pump) -----
static const uint8_t PUMP3_PWM_CHANNEL = 2;
static const uint16_t PUMP3_PWM_FREQ = 40000; // 40 kHz — above audible range, eliminates coil whine
static const uint8_t PUMP3_PWM_RES = 8;
// 80% duty ≈ 8 V effective (at 10 V supply). Quieter than full speed; still
// generates enough vacuum to fully empty the small flowcell within DRAIN_MS.
// Raise toward 100 if the cell does not empty cleanly.
static const uint8_t DRAIN_DUTY_DEFAULT_PERCENT = 80;
static uint8_t drainDutyPercent = DRAIN_DUTY_DEFAULT_PERCENT;
static uint8_t drainPwmDuty = (DRAIN_DUTY_DEFAULT_PERCENT * 255 + 50) / 100;
// Soft-start ramp: step size (0-255 units) per tick and tick interval (ms).
// Pump climbs from ~25% to target duty over ~300 ms, eliminating the
// loud torque-impact clunk of a hard full-voltage start.
static const uint8_t  PUMP3_RAMP_STEP_MS   = 20;   // ms between duty increments
static const uint8_t  PUMP3_RAMP_START_PCT = 25;   // % duty at ramp start (enough to overcome static friction)

// ----- Timing (ms) -----
static const uint32_t BASE_FILL_MS_DEFAULT = 33000;
static const uint16_t BASE_FILL_SEC_MIN = 5;
static const uint16_t BASE_FILL_SEC_MAX = 120;
static uint32_t baseFillMs = BASE_FILL_MS_DEFAULT;
static const uint32_t BLANKING_WARMUP_MS = 2000;
static const uint32_t BLANKING_SAMPLE_MS = 1000;
// ----- MICRO_DOSE_MS Derivation (from physical tubing geometry) -----
// Target delivery:       1.19 mL into the flowcell (per project doc Section 3)
//
// Tubing dimensions:     3 mm ID / 5 mm OD silicone
//   Total tube length:   175 mm
//   Exposed length:      49.5 mm (outside pump head = dead volume)
//   In-pump length:      175 - 49.5 = 125.5 mm (the peristaltic compression zone)
//
// Dead volume (exposed section, fluid that must be pushed through first):
//   V_dead = pi * r^2 * L = pi * 1.5^2 * 49.5 = 350 mm^3 = 0.350 mL
//
// Total volume pump must displace:
//   V_total = 1.19 + 0.350 = 1.540 mL
//
// Flow rates:
//   At 100% duty (kick): 37 mL/min = 0.6167 mL/s
//   At  35% duty (steady): 37 * 0.35 = 12.95 mL/min = 0.2158 mL/s
//
// Volume delivered during 250 ms kick at 100%:
//   V_kick = 0.6167 * 0.250 = 0.154 mL
//
// Remaining volume after kick:
//   V_steady = 1.540 - 0.154 = 1.386 mL
//
// Time at steady 35% duty:
//   t_steady = 1.386 / 0.2158 = 6.423 s = 6423 ms
//
// Total MICRO_DOSE_MS = PUMP2_KICK_MS + t_steady = 250 + 6423 = 6673 ms
// Rounded to nearest 50 ms = 6650 ms
//
// Re-calibrate if: tubing is replaced, PUMP2_DUTY changes, or pump is swapped.
static const uint32_t MICRO_DOSE_MS = 6650;
static const uint32_t AGITATION_MS = 1780;
static const uint32_t DIFFUSION_MS = 15000;
static const uint32_t MEASURE_WARMUP_MS = 1000;
static const uint32_t MEASURE_SAMPLE_MS = 1000;
static const uint32_t DRAIN_MS = 3000;
static const uint32_t COOL_DOWN_MS = 10000;
static const uint32_t IDLE_MS = 3600000;
static const uint8_t SENSOR_ZERO_RETRY_COUNT = 3;
static const uint16_t SENSOR_ZERO_RETRY_DELAY_MS = 50;

static const uint8_t SAMPLE_COUNT = 10;
static const uint16_t SAMPLE_SPACING_MS = 100;

// ----- Calibration (placeholder values) -----
static float CAL_RATIO_PH4 = 1.60f;
static float CAL_RATIO_PH55 = 1.10f;
static float CAL_RATIO_PH7 = 0.70f;

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
static Preferences preferences;
static bool preferencesReady = false;

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
static unsigned long lastHeartbeatMs = 0;          // 5-second serial heartbeat
static const unsigned long HEARTBEAT_INTERVAL_MS = 5000;

static bool sensorConnected = false;
static bool sensorHealthy = true;
static bool sensorReadError = false;
static bool baselineValid = false;
static bool sampleValid = false;
static bool wifiConnected = false;

// LCD 1602 (HD44780 via PCF8574 I2C backpack)
// PCF8574 ships in two address variants: 0x27 (A0-A2 = GND) or 0x3F (A0-A2 = VCC).
// The I2C scan in setup() probes both and initialises whichever responds.
static const uint8_t LCD_ADDR_A = 0x27;
static const uint8_t LCD_ADDR_B = 0x3F;
static LiquidCrystal_I2C lcd(LCD_ADDR_A, 16, 2); // address may be overridden in setup
static bool lcdPresent = false;
static unsigned long lastDisplayUpdateMs = 0;
static const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 500;
// Page cycling: page 0 = main (state/pH/ratio), page 1 = health (sensor/WiFi/duty)
static uint8_t lcdPage = 0;
static unsigned long lastPageFlipMs = 0;
static const unsigned long PAGE_FLIP_INTERVAL_MS = 3000;

// Button debounce
static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static int lastButtonRead = HIGH;
static int stableButtonState = HIGH;
static unsigned long lastButtonChangeMs = 0;

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

static void saveSettings() {
  if (!preferencesReady) {
    return;
  }

  preferences.putUInt("baseFillMs", baseFillMs);
  preferences.putUChar("drainDuty", drainDutyPercent);
  preferences.putFloat("calPh4", CAL_RATIO_PH4);
  preferences.putFloat("calPh55", CAL_RATIO_PH55);
  preferences.putFloat("calPh7", CAL_RATIO_PH7);
}

static void loadSettings() {
  if (!preferencesReady) {
    return;
  }

  uint32_t storedBaseFillMs = BASE_FILL_MS_DEFAULT;
  if (preferences.isKey("baseFillMs")) {
    storedBaseFillMs = preferences.getUInt("baseFillMs", BASE_FILL_MS_DEFAULT);
  }
  uint16_t storedBaseFillSec = clampSeconds(static_cast<int>(storedBaseFillMs / 1000), BASE_FILL_SEC_MIN, BASE_FILL_SEC_MAX);
  baseFillMs = static_cast<uint32_t>(storedBaseFillSec) * 1000;

  if (preferences.isKey("drainDuty")) {
    drainDutyPercent = clampPercent(preferences.getUChar("drainDuty", DRAIN_DUTY_DEFAULT_PERCENT));
  } else {
    drainDutyPercent = DRAIN_DUTY_DEFAULT_PERCENT;
  }
  drainPwmDuty = percentToDuty(drainDutyPercent);

  if (preferences.isKey("calPh4") && preferences.isKey("calPh55") && preferences.isKey("calPh7")) {
    float storedPh4 = preferences.getFloat("calPh4", CAL_RATIO_PH4);
    float storedPh55 = preferences.getFloat("calPh55", CAL_RATIO_PH55);
    float storedPh7 = preferences.getFloat("calPh7", CAL_RATIO_PH7);
    if (std::isfinite(storedPh4) && std::isfinite(storedPh55) && std::isfinite(storedPh7) &&
        storedPh4 > storedPh55 && storedPh55 > storedPh7) {
      CAL_RATIO_PH4 = storedPh4;
      CAL_RATIO_PH55 = storedPh55;
      CAL_RATIO_PH7 = storedPh7;
    } else {
      Serial.println("Calibration values invalid; keeping defaults.");
    }
  }
}

static void setBaseFillSeconds(int seconds, bool persist = true) {
  uint16_t clamped = clampSeconds(seconds, BASE_FILL_SEC_MIN, BASE_FILL_SEC_MAX);
  baseFillMs = static_cast<uint32_t>(clamped) * 1000;
  if (persist) {
    saveSettings();
  }
}

static void setDrainDutyPercent(uint8_t percent, bool persist = true) {
  drainDutyPercent = percent;
  drainPwmDuty = percentToDuty(percent);
  if (currentState == ProcessState::DRAIN) {
    ledcWrite(PUMP3_PWM_CHANNEL, drainPwmDuty);
  }
  if (persist) {
    saveSettings();
  }
}

static bool setCalibrationRatios(float ph4, float ph55, float ph7, bool persist = true) {
  if (!std::isfinite(ph4) || !std::isfinite(ph55) || !std::isfinite(ph7)) {
    return false;
  }

  if (!(ph4 > ph55 && ph55 > ph7)) {
    return false;
  }

  CAL_RATIO_PH4 = ph4;
  CAL_RATIO_PH55 = ph55;
  CAL_RATIO_PH7 = ph7;

  if (persist) {
    saveSettings();
  }

  return true;
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
    if (!on) {
        ledcWrite(PUMP2_PWM_CHANNEL, 0);
        return;
    }
    // Kick-start: drive full duty briefly to overcome static friction,
    // then settle to steady operating duty.
    ledcWrite(PUMP2_PWM_CHANNEL, PUMP2_KICK_DUTY);
    delay(PUMP2_KICK_MS);
    ledcWrite(PUMP2_PWM_CHANNEL, PUMP2_DUTY);
}

static void pump3On(bool on) {
  if (!on) {
    ledcWrite(PUMP3_PWM_CHANNEL, 0);
    return;
  }
  // Soft-start ramp: begin at PUMP3_RAMP_START_PCT and step up to drainPwmDuty.
  // This eliminates the hard torque-impact clunk of a cold diaphragm pump start
  // and significantly reduces perceived noise during the drain phase.
  uint8_t startDuty = (PUMP3_RAMP_START_PCT * 255 + 50) / 100;
  if (startDuty >= drainPwmDuty) {
    // Target is already below or equal to start — no ramp needed.
    ledcWrite(PUMP3_PWM_CHANNEL, drainPwmDuty);
    return;
  }
  ledcWrite(PUMP3_PWM_CHANNEL, startDuty);
  // Step from startDuty to drainPwmDuty in increments of ~10 duty counts.
  for (uint8_t d = startDuty; d < drainPwmDuty; d += 10) {
    ledcWrite(PUMP3_PWM_CHANNEL, d);
    delay(PUMP3_RAMP_STEP_MS);
  }
  ledcWrite(PUMP3_PWM_CHANNEL, drainPwmDuty); // lock to final target
}

static void ledOn(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

static RawReading readAverageRaw(uint8_t samples, uint16_t spacingMs) {
    RawReading out = {0, 0, 0, 0};
  sensorReadError = false;
  if (samples == 0 || !sensorConnected || !sensorHealthy) return out;

  float sumR = 0.0f;
  float sumG = 0.0f;
  float sumB = 0.0f;
  float sumC = 0.0f;

    for (uint8_t i = 0; i < samples; i++) {
        uint16_t r, g, b, c;
      bool zeroReading = true;
      for (uint8_t attempt = 0; attempt < SENSOR_ZERO_RETRY_COUNT; attempt++) {
        tcs.getRawData(&r, &g, &b, &c);
        if (!(r == 0 && g == 0 && b == 0 && c == 0)) {
          zeroReading = false;
          break;
        }
        if (attempt + 1 < SENSOR_ZERO_RETRY_COUNT) {
          delay(SENSOR_ZERO_RETRY_DELAY_MS);
        }
      }

    if (zeroReading) {
      sensorHealthy = false;
      sensorReadError = true;
      sensorConnected = false;
      Serial.printf("[%lu] TCS34725 returned zeroed reading after retries.\n", millis());
      return out;
    }
        sumR += r;
        sumG += g;
        sumB += b;
        sumC += c;
        delay(spacingMs);
    }

  out.r = static_cast<uint16_t>(sumR / samples + 0.5f);
  out.g = static_cast<uint16_t>(sumG / samples + 0.5f);
  out.b = static_cast<uint16_t>(sumB / samples + 0.5f);
  out.c = static_cast<uint16_t>(sumC / samples + 0.5f);
    return out;
}

static float safeTransmittance(float test, float base) {
    if (base <= 0.0f) return 0.0001f;
    float t = test / base;
    if (t < 0.0001f) t = 0.0001f;
  if (t > 1.0f) {
    Serial.printf("[%lu] Warning: test exceeded baseline; clamping transmittance to 1.0.\n", millis());
    t = 1.0f;
  }
    return t;
}

static Absorbance computeAbsorbance(const RawReading &base, const RawReading &test) {
    Absorbance a;
  a.r = -std::log10(safeTransmittance((float)test.r, (float)base.r));
  a.g = -std::log10(safeTransmittance((float)test.g, (float)base.g));
  a.b = -std::log10(safeTransmittance((float)test.b, (float)base.b));
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
        // Duration matches actual readAverageRaw() execution time so the
        // progress bar stays in sync regardless of SAMPLE_COUNT / SAMPLE_SPACING_MS.
        case ProcessState::BLANKING: return BLANKING_WARMUP_MS + (uint32_t)(SAMPLE_COUNT * SAMPLE_SPACING_MS);
        case ProcessState::MICRO_DOSE: return MICRO_DOSE_MS;
        case ProcessState::AGITATION: return AGITATION_MS;
        case ProcessState::DIFFUSION: return DIFFUSION_MS;
        case ProcessState::MEASURE:  return MEASURE_WARMUP_MS  + (uint32_t)(SAMPLE_COUNT * SAMPLE_SPACING_MS);
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
    doc["sensorHealthy"] = sensorHealthy;
    doc["sensorReadError"] = sensorReadError;
    doc["sensorConnected"] = sensorConnected;
    doc["canStart"] = sensorConnected && sensorHealthy && currentState == ProcessState::IDLE;

    JsonObject calibrationObj = doc["calibration"].to<JsonObject>();
    calibrationObj["ph4"] = CAL_RATIO_PH4;
    calibrationObj["ph55"] = CAL_RATIO_PH55;
    calibrationObj["ph7"] = CAL_RATIO_PH7;

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
        ledOn(true);         // LED on — begins thermal stabilisation
        baselineValid = false;
    } else if (next == ProcessState::MICRO_DOSE) {
        ledOn(true);         // LED on first — pump2On() blocks 250 ms for kick-start, LED must be on before that
        pump2On(true);
    } else if (next == ProcessState::AGITATION) {
        pump1On(true);
        ledOn(true);         // Keep LED on — maintain thermal equilibrium
    } else if (next == ProcessState::DIFFUSION) {
        ledOn(true);         // Keep LED on — maintain thermal equilibrium
    } else if (next == ProcessState::MEASURE) {
        ledOn(true);         // LED already stable — sample captured at same point as baseline
        sampleValid = false;
    } else if (next == ProcessState::DRAIN) {
        pump3On(true);       // LED off via setAllOutputsOff() above — measurement done
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
  } else if (action && strcmp(action, "abort") == 0) {
    // Abort mid-cycle: drain the flow cell and return to idle.
    if (currentState != ProcessState::IDLE) {
      Serial.printf("[%lu] Abort requested via WebSocket; draining.\n", millis());
      setState(ProcessState::DRAIN);
    }
    sendStatusToClient(client);
  }
}

static void setupServer() {
    ws.onEvent(handleWebsocketEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!fsReady || !LittleFS.exists("/index.html")) {
        request->send(404, "text/plain", "Dashboard UI is missing. Upload LittleFS data/index.html.");
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
      JsonObject calibrationObj = doc["calibration"].to<JsonObject>();
      calibrationObj["ph4"] = CAL_RATIO_PH4;
      calibrationObj["ph55"] = CAL_RATIO_PH55;
      calibrationObj["ph7"] = CAL_RATIO_PH7;
      String payload;
      serializeJson(doc, payload);
      broadcastStatus();
      request->send(200, "application/json", payload);
    });

    server.on("/api/calibrate", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("ph4Ratio") || !request->hasParam("ph55Ratio") || !request->hasParam("ph7Ratio")) {
        JsonDocument errorDoc;
        errorDoc["error"] = "Missing calibration ratios";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }

      float ph4 = request->getParam("ph4Ratio")->value().toFloat();
      float ph55 = request->getParam("ph55Ratio")->value().toFloat();
      float ph7 = request->getParam("ph7Ratio")->value().toFloat();
      if (!setCalibrationRatios(ph4, ph55, ph7)) {
        JsonDocument errorDoc;
        errorDoc["error"] = "Calibration ratios must be finite and strictly descending";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }

      JsonDocument doc;
      JsonObject calibrationObj = doc["calibration"].to<JsonObject>();
      calibrationObj["ph4"] = CAL_RATIO_PH4;
      calibrationObj["ph55"] = CAL_RATIO_PH55;
      calibrationObj["ph7"] = CAL_RATIO_PH7;
      String payload;
      serializeJson(doc, payload);
      broadcastStatus();
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
            if (sensorReadError) {
              baselineValid = false;
              ledOn(false);  // Error path — safe to kill LED
              Serial.printf("[%lu] [BLANK] FAILED — sensor read error. Draining.\n", millis());
              setState(ProcessState::DRAIN);
              break;
            }
                baselineValid = true;
                // LED intentionally kept ON — setState(MICRO_DOSE) will re-enable it
                // so the LED stays thermally stable through to MEASURE.
                Serial.printf("[%lu] [BLANK] OK  R=%-5u G=%-5u B=%-5u C=%-5u\n",
                              millis(), baseline.r, baseline.g, baseline.b, baseline.c);
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
            if (sensorReadError) {
              sampleValid = false;
              ledOn(false);
              lastAbs = {NAN, NAN, NAN};
              lastRatio = NAN;
              lastPh = NAN;
              Serial.printf("[%lu] [MEAS]  FAILED — sensor read error. Draining.\n", millis());
              setState(ProcessState::DRAIN);
              break;
            }
                sampleValid = true;
                ledOn(false);
                Serial.printf("[%lu] [MEAS]  Raw  R=%-5u G=%-5u B=%-5u C=%-5u\n",
                              millis(), sample.r, sample.g, sample.b, sample.c);

            if (baselineValid) {
                    lastAbs = computeAbsorbance(baseline, sample);
                    lastRatio = computeRatio(lastAbs);
                    lastPh = mapRatioToPH(lastRatio);
                    Serial.printf("[%lu] [MEAS]  Abs  R=%.4f G=%.4f B=%.4f\n",
                                  millis(), lastAbs.r, lastAbs.g, lastAbs.b);
                    if (isnan(lastRatio) || isnan(lastPh)) {
                      Serial.printf("[%lu] [MEAS]  Ratio=NAN  pH=NAN (check calibration)\n", millis());
                    } else {
                      Serial.printf("[%lu] [MEAS]  Ratio=%.4f  pH=%.2f\n",
                                    millis(), lastRatio, lastPh);
                    }
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

    pinMode(LED_PIN,   OUTPUT);
    pinMode(PUMP1_PIN, OUTPUT);
    // PUMP2_PIN and PUMP3_PIN are PWM outputs via LEDC.
    // ledcAttachPin() below reconfigures those GPIOs automatically,
    // so an explicit pinMode(OUTPUT) call for them is not needed.
    digitalWrite(LED_PIN, LOW);
    digitalWrite(PUMP1_PIN, LOW);

    preferencesReady = preferences.begin("optph", false);
    if (!preferencesReady) {
      Serial.println("Preferences mount failed. Settings will not persist.");
    } else {
      loadSettings();
    }

    ledcSetup(PUMP2_PWM_CHANNEL, PUMP2_PWM_FREQ, PUMP2_PWM_RES);
    ledcAttachPin(PUMP2_PIN, PUMP2_PWM_CHANNEL);
    ledcWrite(PUMP2_PWM_CHANNEL, 0);

    ledcSetup(PUMP3_PWM_CHANNEL, PUMP3_PWM_FREQ, PUMP3_PWM_RES);
    ledcAttachPin(PUMP3_PIN, PUMP3_PWM_CHANNEL);
    ledcWrite(PUMP3_PWM_CHANNEL, 0);

    setAllOutputsOff();

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);   // 100 kHz standard-mode — more tolerant of marginal wiring than default 400 kHz
    Wire.setTimeOut(50);

    // Button initialisation (external 10k pull-up expected)
    pinMode(BUTTON_PIN, INPUT);

    // I2C bus scan — detect TCS34725 (0x29) and LCD backpack (0x27 or 0x3F)
    Serial.println("[I2C]    Scanning bus...");
    uint8_t lcdAddr = 0;
    for (uint8_t addr = 3; addr < 0x78; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("[I2C]    Device found at 0x%02X\n", addr);
        if (addr == LCD_ADDR_A || addr == LCD_ADDR_B) {
          lcdAddr = addr; // PCF8574 backpack for 1602 LCD
        }
      }
    }

    if (lcdAddr != 0) {
      lcd = LiquidCrystal_I2C(lcdAddr, 16, 2);
      lcd.init();
      lcd.backlight();
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("pH Sensor Boot  ");
      lcd.setCursor(0, 1); lcd.print("Please wait...  ");
      lcdPresent = true;
      lastDisplayUpdateMs = millis();
      lastPageFlipMs      = millis();
      Serial.printf("[LCD]    1602 found at 0x%02X — initialized.\n", lcdAddr);
    } else {
      Serial.printf("[LCD]    1602 not found (checked 0x%02X and 0x%02X).\n",
                    LCD_ADDR_A, LCD_ADDR_B);
    }

    // ---- Boot Banner ----
    Serial.println();
    Serial.println("============================================");
    Serial.println("  Optical pH Sensor — Custom Build");
    Serial.printf("  Compiled: %s %s\n", __DATE__, __TIME__);
    Serial.println("============================================");

    sensorConnected = tcs.begin();
    if (!sensorConnected) {
        Serial.println("[SENSOR] TCS34725 NOT found — will retry every 2 s.");
    } else {
        tcs.setInterrupt(true);
        sensorHealthy = true;
        Serial.println("[SENSOR] TCS34725 OK");
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    lastWifiAttemptMs = millis();
    Serial.printf("[WIFI]   Connecting to: %s\n", WIFI_SSID);

    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.printf("[WIFI]   Connected. IP: %s  RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.println("[WIFI]   Timeout — will retry in background.");
    }

    fsReady = LittleFS.begin();
    if (!fsReady) {
      Serial.println("[FS]     LittleFS mount failed.");
    } else {
      Serial.println("[FS]     LittleFS OK");
    }

    setupServer();

    // Subscribe the Arduino loop task to the hardware Task Watchdog Timer.
    // The watchdog is fed with esp_task_wdt_reset() each loop() iteration.
    // Timeout is set by sdkconfig CONFIG_ESP_TASK_WDT_TIMEOUT_S (default 5 s).
    esp_task_wdt_add(NULL);

    // ---- Configuration Summary ----
    Serial.println("--------------------------------------------");
    Serial.printf("[CFG]    Base fill:    %u s\n",  baseFillMs / 1000);
    Serial.printf("[CFG]    Drain duty:   %u %%\n", drainDutyPercent);
    Serial.printf("[CFG]    Cal pH4:      %.3f\n",  CAL_RATIO_PH4);
    Serial.printf("[CFG]    Cal pH5.5:    %.3f\n",  CAL_RATIO_PH55);
    Serial.printf("[CFG]    Cal pH7:      %.3f\n",  CAL_RATIO_PH7);
    Serial.printf("[CFG]    LCD 1602:     %s\n",    lcdPresent ? "present" : "not found");
    Serial.println("--------------------------------------------");
    Serial.printf("[%lu] System ready. Waiting for START button or WebSocket 'start'.\n", millis());

    setState(ProcessState::IDLE);
}

void loop() {
    esp_task_wdt_reset();     // Feed the hardware watchdog each iteration
    ws.cleanupClients();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.printf("[%lu] WiFi connected. IP: %s\n", millis(), WiFi.localIP().toString().c_str());
    }
  } else {
    wifiConnected = false;
    unsigned long now = millis();
    if (now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
      lastWifiAttemptMs = now;
      Serial.printf("[%lu] Retrying WiFi connection...\n", millis());
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
                sensorHealthy   = true;
                tcs.setInterrupt(true);
                Serial.printf("[%lu] TCS34725 reconnected.\n", millis());
                // Only broadcast/reset if already IDLE — do NOT interrupt a running cycle.
                if (currentState == ProcessState::IDLE) {
                    broadcastStatus();
                }
            }
        }
        // No blocking delay() here — the 2 s retry guard is sufficient throttling.
        return;
    }

    runStateMachine();

    unsigned long now = millis();
    if (now - lastBroadcastMs >= 1000) {
        lastBroadcastMs = now;
        broadcastStatus();
    }

    // Button polling with debounce (external pull-up, active LOW)
    now = millis();
    int reading = digitalRead(BUTTON_PIN);
    if (reading != lastButtonRead) {
      lastButtonChangeMs = now;
      lastButtonRead = reading;
    }

    if ((now - lastButtonChangeMs) > BUTTON_DEBOUNCE_MS) {
      if (reading != stableButtonState) {
        stableButtonState = reading;
        if (stableButtonState == LOW) {
          Serial.println("START_BTN pressed");
          if (sensorConnected && currentState == ProcessState::IDLE) {
            setState(ProcessState::BASE_FILL);
          }
        }
      }
    }

    // ---- 5-second serial heartbeat ----
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
      lastHeartbeatMs = now;
      Serial.printf("[%lu] [HB] State=%-16s ", millis(), getStateLabel(currentState));
      // Countdown remaining in current state
      uint32_t dur = getStateDuration(currentState);
      uint32_t elapsed = now - stateStartMs;
      if (dur > 0 && elapsed < dur) {
        Serial.printf("T-%lu s  ", (dur - elapsed) / 1000);
      } else {
        Serial.print("         ");
      }
      // pH
      if (isnan(lastPh)) {
        Serial.print("pH=---  ");
      } else {
        Serial.printf("pH=%.2f  ", lastPh);
      }
      // Ratio
      if (isnan(lastRatio)) {
        Serial.print("ratio=---  ");
      } else {
        Serial.printf("ratio=%.4f  ", lastRatio);
      }
      // Sensor + WiFi
      Serial.printf("Sensor=%s  WiFi=%s",
                    sensorConnected ? (sensorHealthy ? "OK" : "UNHEALTHY") : "DISCONN",
                    wifiConnected   ? WiFi.localIP().toString().c_str() : "--");
      Serial.println();
    }

    // ---- LCD 1602 update (500 ms cadence, 2-page layout) ----
    // Page 0 — Main:   Row0=State+Timer  Row1=pH+Ratio
    // Page 1 — Health: Row0=Sensor+Duty  Row1=WiFi IP
    // Pages flip every PAGE_FLIP_INTERVAL_MS (3 s).
    if (lcdPresent && (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS)) {
      lastDisplayUpdateMs = now;

      // Flip page on schedule
      if (now - lastPageFlipMs >= PAGE_FLIP_INTERVAL_MS) {
        lastPageFlipMs = now;
        lcdPage = (lcdPage + 1) % 2;
        lcd.clear(); // clear ghost chars when switching pages
      }

      // Helper lambda — writes exactly 16 chars to a row (pads/truncates)
      // Defined inline to avoid polluting global namespace.
      auto lcdRow = [](uint8_t row, const char *text) {
        char buf[17];
        snprintf(buf, sizeof(buf), "%-16.16s", text);
        lcd.setCursor(0, row);
        lcd.print(buf);
      };

      if (lcdPage == 0) {
        // ---- Page 0: Main ----

        // Row 0: "<State 11chr>  <Timer 4chr>"
        // e.g.  "Base Fill    33s"  or  "Idle            "
        {
          char row0[17];
          const char *label = getStateLabel(currentState);
          uint32_t dur     = getStateDuration(currentState);
          uint32_t elapsed = now - stateStartMs;
          char timerPart[6] = "     "; // 5 spaces default
          if (dur > 0 && elapsed < dur) {
            uint32_t remSec = (dur - elapsed + 500) / 1000;
            snprintf(timerPart, sizeof(timerPart), "%4us", remSec);
          }
          snprintf(row0, sizeof(row0), "%-11.11s%5s", label, timerPart);
          lcdRow(0, row0);
        }

        // Row 1: "pH:XX.XX R:X.XXX"
        {
          char row1[17];
          char phPart[9];   // 8 chars
          char ratPart[9];  // 8 chars
          if (isnan(lastPh)) {
            strcpy(phPart, "pH: --  ");
          } else {
            snprintf(phPart, sizeof(phPart), "pH:%5.2f", lastPh);
          }
          if (isnan(lastRatio)) {
            strcpy(ratPart, "R: ---  ");
          } else {
            snprintf(ratPart, sizeof(ratPart), "R:%6.3f", lastRatio);
          }
          snprintf(row1, sizeof(row1), "%-8.8s%-8.8s", phPart, ratPart);
          lcdRow(1, row1);
        }

      } else {
        // ---- Page 1: Health ----

        // Row 0: "SEN:OK   D: 80%"  or  "SEN:ERR  D: 80%"
        {
          char row0[17];
          const char *senStr;
          if (!sensorConnected)  senStr = "SEN:DISC";
          else if (!sensorHealthy) senStr = "SEN:ERR ";
          else                   senStr = "SEN:OK  ";
          char dutyPart[9];
          snprintf(dutyPart, sizeof(dutyPart), "D:%3u%%  ", drainDutyPercent);
          snprintf(row0, sizeof(row0), "%-8.8s%-8.8s", senStr, dutyPart);
          lcdRow(0, row0);
        }

        // Row 1: "W:192.168.1.42  "  or  "W:-- No WiFi    "
        {
          char row1[17];
          if (wifiConnected) {
            String ip = WiFi.localIP().toString();
            snprintf(row1, sizeof(row1), "W:%-14.14s", ip.c_str());
          } else {
            strcpy(row1, "W:-- No WiFi    ");
          }
          lcdRow(1, row1);
        }
      }
    }
}
