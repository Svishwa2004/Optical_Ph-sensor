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

// ============================ FLUIDIC GEOMETRY ==============================
// Every wetted line is food-grade silicone, 3 mm ID / 5 mm OD (project doc 2.2).
// Cross-section A = pi * (1.5 mm)^2 = 7.0686 mm^2, so 1 cm of tube holds
// 0.070686 mL. Measured lengths (2026-08-08):
//
//   Pump              inlet (source -> pump)   outlet (pump -> destination)
//   1 water fill      39 cm = 2.757 mL         12 cm = 0.848 mL
//   2 dye dose         8 cm = 0.565 mL         10 cm = 0.707 mL
//   3 vacuum drain    12 cm = 0.848 mL         46 cm = 3.252 mL
//
// Re-cut a tube? Update the *_CM figure below and every dependent duration
// re-derives itself. Nothing downstream is hand-tuned.
static constexpr float TUBE_ML_PER_CM = 0.070686f; // 3 mm ID silicone

static constexpr float P1_INLET_CM  = 39.0f;
static constexpr float P1_OUTLET_CM = 12.0f;
static constexpr float P2_INLET_CM  = 8.0f;
static constexpr float P2_OUTLET_CM = 10.0f;
static constexpr float P3_INLET_CM  = 12.0f;
static constexpr float P3_OUTLET_CM = 46.0f;

static constexpr float lineMl(float cm) { return cm * TUBE_ML_PER_CM; }

static constexpr float P1_LINE_ML = lineMl(P1_INLET_CM + P1_OUTLET_CM); // 3.605
static constexpr float P2_LINE_ML = lineMl(P2_INLET_CM + P2_OUTLET_CM); // 1.272
static constexpr float P3_LINE_ML = lineMl(P3_INLET_CM + P3_OUTLET_CM); // 4.100

// Nominal NKP-DC-S06B throughput, both peristaltic heads: 37 mL/min at 100%.
static constexpr float PUMP_ML_PER_S = 37.0f / 60.0f;          // 0.6167 mL/s
// Pump 2 is electronically geared to 35% duty for metering resolution.
static constexpr float PUMP2_ML_PER_S = PUMP_ML_PER_S * 0.35f; // 0.2158 mL/s

// ----- Volume budget (project doc Section 3) -----
// Cell holds 21.36 mL absolute; operation is capped at 80% = 17.09 mL so the
// dye injection port stays above the liquid line and turbulence has headspace.
static constexpr float CELL_OPERATING_ML = 17.09f;
static constexpr float DYE_DOSE_ML       = 0.34f;  // 2.0% v/v per indicator spec
static constexpr float AGITATION_ML      = 1.10f;  // mixing jet, fires after dosing
static constexpr float BASE_FILL_ML =
    CELL_OPERATING_ML - DYE_DOSE_ML - AGITATION_ML; // 15.65 mL

// Volume delivered by pump 2's full-duty kick-start burst (PUMP2_KICK_MS).
static constexpr float PUMP2_KICK_ML = PUMP_ML_PER_S * (PUMP2_KICK_MS / 1000.0f);

// Time for pump 1 (plain 100% duty, no kick) to move a given volume.
static constexpr uint32_t pump1Ms(float ml) {
    return (uint32_t)((ml / PUMP_ML_PER_S) * 1000.0f + 0.5f);
}
// Time for pump 2 to move a given volume: 250 ms at full duty, remainder at 35%.
static constexpr uint32_t pump2Ms(float ml) {
    return (ml <= PUMP2_KICK_ML)
               ? (uint32_t)((ml / PUMP2_KICK_ML) * (float)PUMP2_KICK_MS + 0.5f)
               : (uint32_t)((float)PUMP2_KICK_MS +
                            ((ml - PUMP2_KICK_ML) / PUMP2_ML_PER_S) * 1000.0f +
                            0.5f);
}

// ----- Timing (ms) -----
// Geometry predicts pump1Ms(15.65 mL) = 25378 ms at the nominal 0.6167 mL/s.
// Overridden to 24000 ms at the user's request (bench run showed 25 s a touch
// high). That implies the real rate is nearer 0.652 mL/s; if a future bench
// pass confirms it, correct PUMP_ML_PER_S instead so dose and prime re-derive
// too, and drop this override back to pump1Ms(BASE_FILL_ML). Assumes the pump-1
// line is already full: the peristaltic rollers pinch the tube shut when
// unpowered, so the 39 cm inlet holds its column between runs. Run
// /api/prime?pump=water once after a tube change or reservoir swap. The previous
// 33000 ms was never bench-verified and delivers 20.35 mL — past the 17.09 mL
// operating cap before dye or agitation water is added.
static const uint32_t BASE_FILL_MS_DEFAULT = 24000;
static const uint32_t BASE_FILL_MS_MIN = 5000;
static const uint32_t BASE_FILL_MS_MAX = 120000;
// Stale NVS values from the pre-geometry firmware, discarded on load.
static const uint32_t LEGACY_BASE_FILL_MS = 33000;
static const uint32_t LEGACY_DOSE_MS = 2700;
static const uint16_t BASE_FILL_SEC_MIN = 5;
static const uint16_t BASE_FILL_SEC_MAX = 120;
static uint32_t baseFillMs = BASE_FILL_MS_DEFAULT;
static const uint32_t BLANKING_WARMUP_MS = 2000;
static const uint32_t BLANKING_SAMPLE_MS = 1000;
// ----- doseMs Derivation -----
// Target delivery: 0.34 mL (2.0% v/v of the 17.09 mL operating volume, per the
// Techno Pharmchem universal indicator spec of 0.2 mL per 10 mL of sample).
// The previous 1.19 mL = 6.98% v/v was tuned for the old 0.04% BTB/MR custom
// dye; the universal indicator carries ~0.084% total dyes, so the old volume
// over-saturates the cell and crushes hue resolution at the pH extremes.
//
// The dose time depends entirely on whether the dye line is already full:
//
//   line primed (dead 0.000 mL) ->  1111 ms   <-- the default, see below
//   outlet drained (0.707 mL)   ->  4386 ms
//   whole line empty (1.272 mL) ->  7006 ms
//
// That spread is why priming is not optional. The 10 cm outlet holds 0.707 mL,
// which is 2.1x the dose itself, so if an unknown fraction of it drains between
// cycles the dose error can exceed 100%. Run /api/prime?pump=dye to fill the
// line to a known-wet state; MICRO_DOSE then moves pure volume and doseMs is
// simply the time to push 0.34 mL. dyeLinePrimed is false at every boot and the
// dashboard warns until you prime.
//
// ** STILL BENCH-VERIFY. ** Dose into a graduated container and trim at runtime
// via /api/config?doseMs=... until 0.34 mL lands. Re-tune if the tubing,
// PUMP2_DUTY, or the pump itself changes.
static const uint32_t DOSE_MS_DEFAULT = pump2Ms(DYE_DOSE_ML);
static const uint32_t DOSE_MS_MIN = 200;
static const uint32_t DOSE_MS_MAX = 8000;
static uint32_t doseMs = DOSE_MS_DEFAULT;

// ----- Prime -----
// Fills a pump's line from source to outlet so dead volume stops being a guess.
// 15% overshoot pushes a little fluid past the outlet, guaranteeing the tube is
// wet end-to-end rather than stopping with an air gap at the tip. Prime with the
// cell empty and drain afterwards — the overshoot lands in the chamber.
static constexpr float PRIME_OVERSHOOT = 1.15f;
static const uint32_t PRIME_DYE_MS_DEFAULT   = pump2Ms(P2_LINE_ML * PRIME_OVERSHOOT);
static const uint32_t PRIME_WATER_MS_DEFAULT = pump1Ms(P1_LINE_ML * PRIME_OVERSHOOT);
static const uint32_t PRIME_MS_MIN = 200;
static const uint32_t PRIME_MS_MAX = 20000;
static uint32_t primeMs = PRIME_DYE_MS_DEFAULT;  // duration of the active prime
static bool primeUsesDyePump = true;             // which pump the prime drives
static bool dyeLinePrimed = false;               // cleared on every boot

// 1.10 mL at 0.6167 mL/s. No dead-volume term: the pump-1 line is already full
// from BASE_FILL, so this is pure delivered volume.
static const uint32_t AGITATION_MS = pump1Ms(AGITATION_ML);
static const uint32_t DIFFUSION_MS = 15000;
static const uint32_t MEASURE_WARMUP_MS = 1000;
static const uint32_t MEASURE_SAMPLE_MS = 1000;
// R385 at ~13.3 mL/s clears the 17.09 mL cell plus the 4.10 mL drain path in
// ~1.6 s; the balance is air purge that strips residual droplets off the floor.
static const uint32_t DRAIN_MS = 6000;
static const uint32_t COOL_DOWN_MS = 10000;

// ----- Rinse (post-cycle flush) -----
// A completed measurement leaves dyed residue clinging to the cell walls, the
// optical windows, and pump 3's 0.85 mL inlet. Left there it tints the next
// BLANKING baseline, and because every reading is measured against that baseline
// the whole cycle drifts. A rinse pass fills the cell with clean sample water and
// drains again, diluting the residue ~15-25x per pass and pushing fresh water
// through the drain line. rinseCycles passes run automatically after the
// measurement drain (0 disables the auto-rinse); a rinse can also be triggered
// manually from idle via /api/rinse. It is kept deliberately short: a partial
// fill (not the full operating volume) plus a shortened drain make it quick and
// keep the noisy R385 running only briefly.
static const uint8_t  RINSE_CYCLES_DEFAULT = 1;
static const uint8_t  RINSE_CYCLES_MAX = 5;
static uint8_t rinseCycles    = RINSE_CYCLES_DEFAULT; // 0 = no automatic rinse
static uint8_t rinseRemaining = 0;                    // passes left in the active rinse

// Fill only enough to submerge the optical windows and dilute what clings to
// them — the rinse targets carryover, not measurement, so it needn't reach the
// full 15.65 mL operating level. BENCH-VERIFY this crests the windows; if a film
// is left above the rinse line, raise rinseFillMs. 12 mL at nominal is ~19.5 s.
static constexpr float RINSE_FILL_ML = 12.0f;
static const uint32_t RINSE_FILL_MS_DEFAULT = pump1Ms(RINSE_FILL_ML);
static const uint32_t RINSE_FILL_MS_MIN = 2000;
static const uint32_t RINSE_FILL_MS_MAX = 60000;
static uint32_t rinseFillMs = RINSE_FILL_MS_DEFAULT;

// Shorter than the measurement DRAIN_MS: a rinse doesn't need the long air-purge
// tail, so it stays quick and the R385 runs for less time. The pump clears the
// 12 mL charge plus the 4.10 mL line in ~1.2 s, leaving an ample purge margin.
static const uint32_t RINSE_DRAIN_MS = 4000;

static const uint32_t IDLE_MS = 3600000;
static const uint8_t SENSOR_ZERO_RETRY_COUNT = 3;
static const uint16_t SENSOR_ZERO_RETRY_DELAY_MS = 50;

static const uint8_t SAMPLE_COUNT = 10;
static const uint16_t SAMPLE_SPACING_MS = 100;

// ----- Calibration: hue -> pH multi-point table -----
// The Yamada-type universal indicator sweeps hue monotonically across the full
// pH range (red -> orange -> yellow -> green -> blue -> violet as pH climbs
// 1 -> 14). We therefore map HSV hue (in degrees, "unwrapped" through a branch
// cut so the red end stays low) to pH by piecewise-linear interpolation through
// a table of {pH, hue} points captured against known buffers. Readings whose pH
// falls outside the calibrated span are still reported, but flagged extrapolated.
//
// The defaults below are PLACEHOLDERS approximating typical universal-indicator
// hues. They MUST be replaced by bench calibration against the user's buffers
// (4.01, 6.86, 9.18) via /api/calibrate/capture before readings are trusted.
static const uint8_t CAL_MAX_POINTS = 8;
static const uint8_t CAL_MIN_POINTS = 2;
struct CalPoint {
    float ph;   // known buffer pH
    float hue;  // measured unwrapped hue, degrees (ascending with pH)
};
static CalPoint calPoints[CAL_MAX_POINTS] = {
    {2.0f,   0.0f},
    {4.0f,  35.0f},
    {6.0f,  65.0f},
    {7.0f,  95.0f},
    {9.0f, 170.0f},
    {12.0f, 280.0f},
};
static uint8_t calCount = 6;

// Hue branch cut (degrees). Hues at/above this threshold are shifted down by 360
// so the red (acidic) end reads as a small/negative value and the whole ramp
// stays monotonically ascending with pH. Tunable if the dye's red end wraps.
static const float HUE_BRANCH_CUT_DEFAULT = 320.0f;
static float hueBranchCut = HUE_BRANCH_CUT_DEFAULT;

// Saturation/value sanity gates: a well-dyed, well-lit cell should be colorful
// and bright. Readings below these (near-grey or near-black) are unreliable.
static const float MIN_VALID_SATURATION = 0.10f;
static const float MIN_VALID_VALUE = 0.04f;

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
  COOL_DOWN = 8,
  PRIME = 9,
  RINSE_FILL = 10,
  RINSE_DRAIN = 11
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
static float lastRatio = NAN;  // legacy B/G ratio, kept for diagnostics
static float lastPh = NAN;
static float lastHue = NAN;       // unwrapped hue in degrees
static float lastSat = NAN;       // saturation [0,1]
static float lastVal = NAN;       // value [0,1]
static bool lastExtrapolated = false;  // pH outside calibrated span?

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

// A valid calibration table has 2..CAL_MAX_POINTS entries, all finite, with pH
// strictly ascending AND hue strictly ascending (so hueToPh is single-valued).
static bool validateCalPoints(const CalPoint *pts, uint8_t count) {
  if (count < CAL_MIN_POINTS || count > CAL_MAX_POINTS) return false;
  for (uint8_t i = 0; i < count; i++) {
    if (!std::isfinite(pts[i].ph) || !std::isfinite(pts[i].hue)) return false;
    if (i > 0) {
      if (pts[i].ph <= pts[i - 1].ph) return false;
      if (pts[i].hue <= pts[i - 1].hue) return false;
    }
  }
  return true;
}

static void saveSettings() {
  if (!preferencesReady) {
    return;
  }

  preferences.putUInt("baseFillMs", baseFillMs);
  preferences.putUChar("drainDuty", drainDutyPercent);
  preferences.putUInt("doseMs", doseMs);
  preferences.putUChar("rinseCycles", rinseCycles);
  preferences.putUInt("rinseFillMs", rinseFillMs);
  preferences.putFloat("hueCut", hueBranchCut);
  preferences.putUChar("calCount", calCount);
  preferences.putBytes("calPoints", calPoints, calCount * sizeof(CalPoint));

  // Discard legacy 3-ratio calibration keys from the old B/G-ratio model.
  if (preferences.isKey("calPh4")) preferences.remove("calPh4");
  if (preferences.isKey("calPh55")) preferences.remove("calPh55");
  if (preferences.isKey("calPh7")) preferences.remove("calPh7");
}

static void loadSettings() {
  if (!preferencesReady) {
    return;
  }

  uint32_t storedBaseFillMs = BASE_FILL_MS_DEFAULT;
  if (preferences.isKey("baseFillMs")) {
    storedBaseFillMs = preferences.getUInt("baseFillMs", BASE_FILL_MS_DEFAULT);
  }
  // Migration: 33000 ms was the old never-verified default and overfills the
  // cell (20.35 mL vs the 17.09 mL cap). Discard it so units already in the
  // field pick up the geometry-derived value instead of inheriting the bug.
  if (storedBaseFillMs == LEGACY_BASE_FILL_MS) {
    Serial.println("Discarding legacy 33000 ms base fill (overfills cell); using derived default.");
    storedBaseFillMs = BASE_FILL_MS_DEFAULT;
  }
  if (storedBaseFillMs < BASE_FILL_MS_MIN) storedBaseFillMs = BASE_FILL_MS_MIN;
  if (storedBaseFillMs > BASE_FILL_MS_MAX) storedBaseFillMs = BASE_FILL_MS_MAX;
  baseFillMs = storedBaseFillMs;

  if (preferences.isKey("drainDuty")) {
    drainDutyPercent = clampPercent(preferences.getUChar("drainDuty", DRAIN_DUTY_DEFAULT_PERCENT));
  } else {
    drainDutyPercent = DRAIN_DUTY_DEFAULT_PERCENT;
  }
  drainPwmDuty = percentToDuty(drainDutyPercent);

  if (preferences.isKey("doseMs")) {
    uint32_t storedDose = preferences.getUInt("doseMs", DOSE_MS_DEFAULT);
    // Migration: 2700 ms came from an assumed 49.5 mm dead-volume segment. The
    // measured outlet is 10 cm, so that figure is either a 2x overdose (primed
    // line) or delivers nothing at all (drained line). Neither is usable.
    if (storedDose == LEGACY_DOSE_MS) {
      Serial.println("Discarding legacy 2700 ms dose (wrong dead-volume assumption); using derived default.");
      storedDose = DOSE_MS_DEFAULT;
    }
    if (storedDose < DOSE_MS_MIN) storedDose = DOSE_MS_MIN;
    if (storedDose > DOSE_MS_MAX) storedDose = DOSE_MS_MAX;
    doseMs = storedDose;
  }

  if (preferences.isKey("rinseCycles")) {
    uint8_t storedCycles = preferences.getUChar("rinseCycles", RINSE_CYCLES_DEFAULT);
    rinseCycles = (storedCycles > RINSE_CYCLES_MAX) ? RINSE_CYCLES_MAX : storedCycles;
  }

  if (preferences.isKey("rinseFillMs")) {
    uint32_t storedRinseFill = preferences.getUInt("rinseFillMs", RINSE_FILL_MS_DEFAULT);
    if (storedRinseFill < RINSE_FILL_MS_MIN) storedRinseFill = RINSE_FILL_MS_MIN;
    if (storedRinseFill > RINSE_FILL_MS_MAX) storedRinseFill = RINSE_FILL_MS_MAX;
    rinseFillMs = storedRinseFill;
  }

  if (preferences.isKey("hueCut")) {
    float storedCut = preferences.getFloat("hueCut", HUE_BRANCH_CUT_DEFAULT);
    if (std::isfinite(storedCut) && storedCut > 0.0f && storedCut <= 360.0f) {
      hueBranchCut = storedCut;
    }
  }

  if (preferences.isKey("calCount") && preferences.isKey("calPoints")) {
    uint8_t storedCount = preferences.getUChar("calCount", 0);
    if (storedCount >= CAL_MIN_POINTS && storedCount <= CAL_MAX_POINTS) {
      CalPoint tmp[CAL_MAX_POINTS];
      size_t want = storedCount * sizeof(CalPoint);
      size_t got = preferences.getBytes("calPoints", tmp, want);
      if (got == want && validateCalPoints(tmp, storedCount)) {
        memcpy(calPoints, tmp, want);
        calCount = storedCount;
      } else {
        Serial.println("Stored calibration table invalid; keeping defaults.");
      }
    }
  } else if (preferences.isKey("calPh4")) {
    // Migration: an old 3-ratio calibration exists but no hue table. The ratio
    // model is incompatible with the universal indicator, so discard it and
    // fall back to the placeholder hue table (must be re-calibrated on bench).
    Serial.println("Legacy ratio calibration found; discarding — recalibrate hue table.");
  }
}

static void setBaseFillSeconds(int seconds, bool persist = true) {
  uint16_t clamped = clampSeconds(seconds, BASE_FILL_SEC_MIN, BASE_FILL_SEC_MAX);
  baseFillMs = static_cast<uint32_t>(clamped) * 1000;
  if (persist) {
    saveSettings();
  }
}

// Millisecond-resolution base fill. Whole-second steps move 0.617 mL each,
// which is 3.6% of the cell — too coarse to land on the 15.65 mL target, so the
// fill is stored in ms. setBaseFillSeconds() stays for API back-compat.
static uint32_t setBaseFillMs(uint32_t requested, bool persist = true) {
  if (requested < BASE_FILL_MS_MIN) requested = BASE_FILL_MS_MIN;
  if (requested > BASE_FILL_MS_MAX) requested = BASE_FILL_MS_MAX;
  baseFillMs = requested;
  if (persist) {
    saveSettings();
  }
  return baseFillMs;
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

static uint32_t setDoseMs(uint32_t requested, bool persist = true) {
  if (requested < DOSE_MS_MIN) requested = DOSE_MS_MIN;
  if (requested > DOSE_MS_MAX) requested = DOSE_MS_MAX;
  doseMs = requested;
  if (persist) {
    saveSettings();
  }
  return doseMs;
}

static uint8_t setRinseCycles(int cycles, bool persist = true) {
  if (cycles < 0) cycles = 0;
  if (cycles > RINSE_CYCLES_MAX) cycles = RINSE_CYCLES_MAX;
  rinseCycles = static_cast<uint8_t>(cycles);
  if (persist) {
    saveSettings();
  }
  return rinseCycles;
}

static uint32_t setRinseFillMs(uint32_t requested, bool persist = true) {
  if (requested < RINSE_FILL_MS_MIN) requested = RINSE_FILL_MS_MIN;
  if (requested > RINSE_FILL_MS_MAX) requested = RINSE_FILL_MS_MAX;
  rinseFillMs = requested;
  if (persist) {
    saveSettings();
  }
  return rinseFillMs;
}

// Replace the whole calibration table atomically. Validates before committing so
// a bad request never leaves a partially-updated (non-monotonic) table.
static bool setCalibrationPoints(const CalPoint *pts, uint8_t count, bool persist = true) {
  if (!validateCalPoints(pts, count)) {
    return false;
  }
  memcpy(calPoints, pts, count * sizeof(CalPoint));
  calCount = count;
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

// Pump 2 at 100% duty. Used only for priming, where throughput matters and
// metering precision does not — pump2On()'s 35% gear-down would make filling
// the 1.27 mL line needlessly slow.
static void pump2Full(bool on) {
    ledcWrite(PUMP2_PWM_CHANNEL, on ? PUMP2_KICK_DUTY : 0);
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

struct Hsv {
    float h;  // hue, degrees [0,360)
    float s;  // saturation [0,1]
    float v;  // value [0,1]
};

// Compute the transmitted colour of the dyed sample relative to the clear-water
// baseline, then convert to HSV. Dividing by the baseline removes the light
// source and any nutrient-solution tint captured during Dynamic Blanking, so
// what remains is the dye's own colour. Returns hue in [0,360); NAN if invalid.
static Hsv computeHsv(const RawReading &base, const RawReading &test) {
    Hsv out = {NAN, NAN, NAN};
    float tr = safeTransmittance((float)test.r, (float)base.r);
    float tg = safeTransmittance((float)test.g, (float)base.g);
    float tb = safeTransmittance((float)test.b, (float)base.b);

    float cmax = fmaxf(tr, fmaxf(tg, tb));
    float cmin = fminf(tr, fminf(tg, tb));
    float delta = cmax - cmin;

    out.v = cmax;
    out.s = (cmax <= 0.0f) ? 0.0f : (delta / cmax);

    if (delta <= 1e-6f) {
        out.h = 0.0f;  // achromatic; hue undefined, saturation gate will reject
        return out;
    }

    float h;
    if (cmax == tr) {
        h = 60.0f * fmodf(((tg - tb) / delta), 6.0f);
    } else if (cmax == tg) {
        h = 60.0f * (((tb - tr) / delta) + 2.0f);
    } else {
        h = 60.0f * (((tr - tg) / delta) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
    out.h = h;
    return out;
}

// Shift the red (acidic) end below the branch cut so the ramp stays monotonic
// with pH. Hues at/above hueBranchCut are pulled down by 360 degrees.
static float unwrapHue(float hue) {
    if (isnan(hue)) return NAN;
    return (hue >= hueBranchCut) ? hue - 360.0f : hue;
}

// Piecewise-linear interpolation of unwrapped hue -> pH through calPoints.
// Sets outExtrapolated when the hue lies outside the calibrated span (the
// result is a linear extrapolation off the nearest segment). calPoints must be
// sorted ascending in both pH and hue (enforced at set time).
static float hueToPh(float unwrappedHue, bool &outExtrapolated) {
    outExtrapolated = false;
    if (isnan(unwrappedHue) || calCount < CAL_MIN_POINTS) return NAN;

    if (unwrappedHue <= calPoints[0].hue) {
        outExtrapolated = (unwrappedHue < calPoints[0].hue);
        float h0 = calPoints[0].hue, h1 = calPoints[1].hue;
        float p0 = calPoints[0].ph, p1 = calPoints[1].ph;
        if (h1 == h0) return p0;
        return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
    }
    uint8_t last = calCount - 1;
    if (unwrappedHue >= calPoints[last].hue) {
        outExtrapolated = (unwrappedHue > calPoints[last].hue);
        float h0 = calPoints[last - 1].hue, h1 = calPoints[last].hue;
        float p0 = calPoints[last - 1].ph, p1 = calPoints[last].ph;
        if (h1 == h0) return p1;
        return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
    }
    for (uint8_t i = 0; i < last; i++) {
        float h0 = calPoints[i].hue, h1 = calPoints[i + 1].hue;
        if (unwrappedHue >= h0 && unwrappedHue <= h1) {
            float p0 = calPoints[i].ph, p1 = calPoints[i + 1].ph;
            if (h1 == h0) return p0;
            return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
        }
    }
    return NAN;
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
    case ProcessState::PRIME: return "Line Prime";
    case ProcessState::RINSE_FILL: return "Rinse Fill";
    case ProcessState::RINSE_DRAIN: return "Rinse Drain";
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
    case ProcessState::PRIME: return "Filling supply line";
    case ProcessState::RINSE_FILL: return "Flushing cell";
    case ProcessState::RINSE_DRAIN: return "Clearing rinse water";
        default: return "Idle wait";
    }
}

static uint32_t getStateDuration(ProcessState state) {
    switch (state) {
    case ProcessState::BASE_FILL: return baseFillMs;
        // Duration matches actual readAverageRaw() execution time so the
        // progress bar stays in sync regardless of SAMPLE_COUNT / SAMPLE_SPACING_MS.
        case ProcessState::BLANKING: return BLANKING_WARMUP_MS + (uint32_t)(SAMPLE_COUNT * SAMPLE_SPACING_MS);
        case ProcessState::MICRO_DOSE: return doseMs;
        case ProcessState::AGITATION: return AGITATION_MS;
        case ProcessState::DIFFUSION: return DIFFUSION_MS;
        case ProcessState::MEASURE:  return MEASURE_WARMUP_MS  + (uint32_t)(SAMPLE_COUNT * SAMPLE_SPACING_MS);
        case ProcessState::DRAIN: return DRAIN_MS;
    case ProcessState::COOL_DOWN: return COOL_DOWN_MS;
    case ProcessState::PRIME: return primeMs;
    case ProcessState::RINSE_FILL: return rinseFillMs;
    case ProcessState::RINSE_DRAIN: return RINSE_DRAIN_MS;
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
    doc["doseMs"] = doseMs;
    doc["dyeLinePrimed"] = dyeLinePrimed;
    doc["rinseCycles"] = rinseCycles;
    doc["rinseFillMs"] = rinseFillMs;
    doc["rinseRemaining"] = rinseRemaining;

    // Fluidics: derived volumes so the dashboard can show what the current
    // timings actually deliver instead of restating the raw milliseconds.
    JsonObject fluid = doc["fluidics"].to<JsonObject>();
    fluid["baseFillMl"] = baseFillMs * PUMP_ML_PER_S / 1000.0f;
    fluid["agitationMl"] = AGITATION_ML;
    fluid["cellOperatingMl"] = CELL_OPERATING_ML;
    fluid["dyeTargetMl"] = DYE_DOSE_ML;
    fluid["dyeLineMl"] = P2_LINE_ML;
    fluid["primeDyeMs"] = PRIME_DYE_MS_DEFAULT;
    fluid["primeWaterMs"] = PRIME_WATER_MS_DEFAULT;

    doc["baselineValid"] = baselineValid;
    doc["sampleValid"] = sampleValid;
    doc["drainDutyPercent"] = drainDutyPercent;
    doc["drainDutyRaw"] = drainPwmDuty;
    doc["sensorHealthy"] = sensorHealthy;
    doc["sensorReadError"] = sensorReadError;
    doc["sensorConnected"] = sensorConnected;
    doc["canStart"] = sensorConnected && sensorHealthy && currentState == ProcessState::IDLE;

    // Calibration: N-point hue->pH table + branch cut, plus the calibrated span.
    JsonObject calibrationObj = doc["calibration"].to<JsonObject>();
    calibrationObj["hueBranchCut"] = hueBranchCut;
    calibrationObj["count"] = calCount;
    calibrationObj["phMin"] = calCount > 0 ? calPoints[0].ph : (float)NAN;
    calibrationObj["phMax"] = calCount > 0 ? calPoints[calCount - 1].ph : (float)NAN;
    JsonArray pointsArr = calibrationObj["points"].to<JsonArray>();
    for (uint8_t i = 0; i < calCount; i++) {
        JsonObject p = pointsArr.add<JsonObject>();
        p["ph"] = calPoints[i].ph;
        p["hue"] = calPoints[i].hue;
    }

    if (isnan(lastPh)) {
        doc["ph"] = nullptr;
    } else {
        doc["ph"] = lastPh;
    }
    doc["extrapolated"] = lastExtrapolated;

    // HSV telemetry (drives the reading now; ratio kept for diagnostics).
    JsonObject hsvObj = doc["hsv"].to<JsonObject>();
    if (isnan(lastHue)) { hsvObj["h"] = nullptr; } else { hsvObj["h"] = lastHue; }
    if (isnan(lastSat)) { hsvObj["s"] = nullptr; } else { hsvObj["s"] = lastSat; }
    if (isnan(lastVal)) { hsvObj["v"] = nullptr; } else { hsvObj["v"] = lastVal; }

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
        rinseRemaining = 0;  // fresh cycle: no rinse queued until MEASURE succeeds
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
    } else if (next == ProcessState::RINSE_FILL) {
        pump1On(true);       // clean water into the cell to dilute dyed residue
    } else if (next == ProcessState::RINSE_DRAIN) {
        pump3On(true);       // evacuate the rinse water (and flush the drain line)
    } else if (next == ProcessState::PRIME) {
        // Priming is not metered, so drive the selected pump at full duty.
        if (primeUsesDyePump) {
            pump2Full(true);
        } else {
            pump1On(true);
        }
    }

    broadcastStatus();
}

// Kick off a line prime. Only legal from IDLE so a prime can never interrupt a
// measurement mid-cycle. Returns false if busy; the caller reports that as 409.
static bool startPrime(bool useDyePump, uint32_t ms) {
    if (currentState != ProcessState::IDLE) {
        return false;
    }
    if (ms < PRIME_MS_MIN) ms = PRIME_MS_MIN;
    if (ms > PRIME_MS_MAX) ms = PRIME_MS_MAX;
    primeUsesDyePump = useDyePump;
    primeMs = ms;
    setState(ProcessState::PRIME);
    return true;
}

// Kick off a manual rinse (fill clean water, drain, repeat). Only legal from IDLE
// so it can never interrupt a measurement. `cycles` passes run, clamped 1..MAX;
// the sequence ends in COOL_DOWN so the drain pump gets its usual rest. Returns
// false if busy; the caller reports that as 409.
static bool startRinse(uint8_t cycles) {
    if (currentState != ProcessState::IDLE) {
        return false;
    }
    if (cycles < 1) cycles = 1;
    if (cycles > RINSE_CYCLES_MAX) cycles = RINSE_CYCLES_MAX;
    rinseRemaining = cycles;
    setState(ProcessState::RINSE_FILL);
    return true;
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
      rinseRemaining = 0;  // an explicit abort should not trigger the auto-rinse
      setState(ProcessState::DRAIN);
    }
    sendStatusToClient(client);
  }
}

// Serialize the active calibration table into a JSON object (shared by the
// /api/calibrate responses so their shape matches buildStatusJson's).
static void writeCalibrationJson(JsonObject obj) {
  obj["hueBranchCut"] = hueBranchCut;
  obj["count"] = calCount;
  obj["phMin"] = calCount > 0 ? calPoints[0].ph : (float)NAN;
  obj["phMax"] = calCount > 0 ? calPoints[calCount - 1].ph : (float)NAN;
  JsonArray arr = obj["points"].to<JsonArray>();
  for (uint8_t i = 0; i < calCount; i++) {
    JsonObject p = arr.add<JsonObject>();
    p["ph"] = calPoints[i].ph;
    p["hue"] = calPoints[i].hue;
  }
}

// Parse a "ph:hue,ph:hue,..." string into a CalPoint array. Returns the count
// parsed (0 on malformed input or overflow). Does NOT validate monotonicity —
// that is left to setCalibrationPoints.
static uint8_t parseCalPoints(const String &s, CalPoint *out) {
  uint8_t n = 0;
  int start = 0;
  while (start < (int)s.length() && n < CAL_MAX_POINTS) {
    int comma = s.indexOf(',', start);
    String token = (comma < 0) ? s.substring(start) : s.substring(start, comma);
    token.trim();
    if (token.length() > 0) {
      int colon = token.indexOf(':');
      if (colon < 0) return 0;  // malformed pair
      out[n].ph = token.substring(0, colon).toFloat();
      out[n].hue = token.substring(colon + 1).toFloat();
      n++;
    }
    if (comma < 0) break;
    start = comma + 1;
  }
  return n;
}

// Insert or replace a calibration point at a known pH using a measured hue,
// keeping the table sorted by pH. Writes the result into `out` and returns the
// new count, or 0 if the table would overflow.
static uint8_t upsertCalPoint(float ph, float hue, CalPoint *out) {
  const float PH_EPS = 0.05f;
  uint8_t n = 0;
  bool replaced = false;
  for (uint8_t i = 0; i < calCount; i++) {
    if (fabsf(calPoints[i].ph - ph) <= PH_EPS) {
      out[n].ph = ph;
      out[n].hue = hue;
      n++;
      replaced = true;
    } else {
      if (n >= CAL_MAX_POINTS) return 0;
      out[n++] = calPoints[i];
    }
  }
  if (!replaced) {
    if (n >= CAL_MAX_POINTS) return 0;
    out[n].ph = ph;
    out[n].hue = hue;
    n++;
    // Sort ascending by pH (tiny table — simple insertion sort).
    for (uint8_t i = 1; i < n; i++) {
      CalPoint key = out[i];
      int j = i - 1;
      while (j >= 0 && out[j].ph > key.ph) {
        out[j + 1] = out[j];
        j--;
      }
      out[j + 1] = key;
    }
  }
  return n;
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

      // Preferred over baseFillSec: 1 s of fill is 0.617 mL, too coarse to hit
      // the 15.65 mL target. Takes precedence if both params are supplied.
      if (request->hasParam("baseFillMs")) {
        long ms = request->getParam("baseFillMs")->value().toInt();
        setBaseFillMs((uint32_t)(ms < 0 ? 0 : ms));
      }

      if (request->hasParam("drainDuty")) {
        int percent = request->getParam("drainDuty")->value().toInt();
        setDrainDutyPercent(clampPercent(percent));
      }

      if (request->hasParam("doseMs")) {
        long ms = request->getParam("doseMs")->value().toInt();
        setDoseMs((uint32_t)(ms < 0 ? 0 : ms));
      }

      if (request->hasParam("rinseCycles")) {
        int cycles = request->getParam("rinseCycles")->value().toInt();
        setRinseCycles(cycles);
      }

      if (request->hasParam("rinseFillMs")) {
        long ms = request->getParam("rinseFillMs")->value().toInt();
        setRinseFillMs((uint32_t)(ms < 0 ? 0 : ms));
      }

      JsonDocument doc;
      doc["baseFillMs"] = baseFillMs;
      doc["baseFillSec"] = baseFillMs / 1000;
      doc["doseMs"] = doseMs;
      doc["drainDutyPercent"] = drainDutyPercent;
      doc["drainDutyRaw"] = drainPwmDuty;
      doc["rinseCycles"] = rinseCycles;
      doc["rinseFillMs"] = rinseFillMs;
      String payload;
      serializeJson(doc, payload);
      broadcastStatus();
      request->send(200, "application/json", payload);
    });

    // Fill a supply line so dead volume is known rather than guessed.
    //   GET /api/prime?pump=dye|water[&ms=6300]
    // Defaults come from the measured tube lengths plus a 15% overshoot. Run
    // with the cell empty and drain afterwards — the overshoot lands inside.
    server.on("/api/prime", HTTP_GET, [](AsyncWebServerRequest *request) {
      bool useDye = true;
      if (request->hasParam("pump")) {
        String which = request->getParam("pump")->value();
        which.toLowerCase();
        if (which == "water" || which == "1" || which == "fill") {
          useDye = false;
        } else if (which != "dye" && which != "2") {
          request->send(400, "application/json",
                        "{\"error\":\"pump must be dye or water\"}");
          return;
        }
      }

      uint32_t ms = useDye ? PRIME_DYE_MS_DEFAULT : PRIME_WATER_MS_DEFAULT;
      if (request->hasParam("ms")) {
        long requested = request->getParam("ms")->value().toInt();
        if (requested > 0) {
          ms = (uint32_t)requested;
        }
      }

      if (!startPrime(useDye, ms)) {
        request->send(409, "application/json",
                      "{\"error\":\"busy; prime is only allowed from idle\"}");
        return;
      }

      JsonDocument doc;
      doc["pump"] = useDye ? "dye" : "water";
      doc["primeMs"] = primeMs;
      doc["lineMl"] = useDye ? P2_LINE_ML : P1_LINE_ML;
      String payload;
      serializeJson(doc, payload);
      request->send(200, "application/json", payload);
    });

    // Flush the cell with clean water to clear dye carryover before the next run.
    //   GET /api/rinse[&cycles=1]
    // Runs `cycles` fill+drain passes (default rinseCycles, min 1), then cools
    // down. Only allowed from idle (409 otherwise). This is the manual twin of
    // the automatic rinse that already follows every successful measurement.
    server.on("/api/rinse", HTTP_GET, [](AsyncWebServerRequest *request) {
      uint8_t cycles = (rinseCycles < 1) ? 1 : rinseCycles;
      if (request->hasParam("cycles")) {
        int requested = request->getParam("cycles")->value().toInt();
        if (requested > 0) cycles = (uint8_t)requested;
      }

      if (!startRinse(cycles)) {
        request->send(409, "application/json",
                      "{\"error\":\"busy; rinse is only allowed from idle\"}");
        return;
      }

      JsonDocument doc;
      doc["cycles"] = rinseRemaining;
      doc["rinseFillMs"] = rinseFillMs;
      doc["rinseDrainMs"] = RINSE_DRAIN_MS;
      String payload;
      serializeJson(doc, payload);
      request->send(200, "application/json", payload);
    });

    // Replace the whole hue->pH table. Also accepts an optional branch-cut.
    //   GET /api/calibrate?points=4.01:35,6.86:95,9.18:180[&hueCut=320]
    server.on("/api/calibrate", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (request->hasParam("hueCut")) {
        float cut = request->getParam("hueCut")->value().toFloat();
        if (std::isfinite(cut) && cut > 0.0f && cut <= 360.0f) {
          hueBranchCut = cut;
        }
      }

      if (!request->hasParam("points")) {
        JsonDocument errorDoc;
        errorDoc["error"] = "Missing points=ph:hue,ph:hue,... (2-8 points)";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }

      CalPoint parsed[CAL_MAX_POINTS];
      uint8_t n = parseCalPoints(request->getParam("points")->value(), parsed);
      if (!setCalibrationPoints(parsed, n)) {
        JsonDocument errorDoc;
        errorDoc["error"] =
            "Need 2-8 points, all finite, with pH and hue both strictly ascending";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }

      JsonDocument doc;
      writeCalibrationJson(doc["calibration"].to<JsonObject>());
      String payload;
      serializeJson(doc, payload);
      broadcastStatus();
      request->send(200, "application/json", payload);
    });

    // Capture one calibration point from the most recent live reading:
    //   GET /api/calibrate/capture?ph=6.86
    // Uses the current unwrapped hue (from the last MEASURE) as that pH's hue,
    // inserting or replacing the matching table entry. Run a measurement cycle
    // with the buffer in the cell first, then call this with its known pH.
    server.on("/api/calibrate/capture", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("ph")) {
        JsonDocument errorDoc;
        errorDoc["error"] = "Missing ph= (known buffer pH)";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }
      float ph = request->getParam("ph")->value().toFloat();

      if (isnan(lastHue) || isnan(lastSat) || isnan(lastVal) ||
          lastSat < MIN_VALID_SATURATION || lastVal < MIN_VALID_VALUE) {
        JsonDocument errorDoc;
        errorDoc["error"] =
            "No valid recent reading to capture — run a measurement with this buffer first";
        String payload;
        serializeJson(errorDoc, payload);
        request->send(409, "application/json", payload);
        return;
      }

      float hue = unwrapHue(lastHue);
      CalPoint candidate[CAL_MAX_POINTS];
      uint8_t n = upsertCalPoint(ph, hue, candidate);
      if (n == 0 || !setCalibrationPoints(candidate, n)) {
        JsonDocument errorDoc;
        errorDoc["error"] =
            "Captured point makes the table non-monotonic (or table full). "
            "Check buffer order / branch cut.";
        errorDoc["capturedHue"] = hue;
        String payload;
        serializeJson(errorDoc, payload);
        request->send(400, "application/json", payload);
        return;
      }

      JsonDocument doc;
      doc["capturedPh"] = ph;
      doc["capturedHue"] = hue;
      writeCalibrationJson(doc["calibration"].to<JsonObject>());
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
            if (now - stateStartMs >= doseMs) {
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
              lastHue = NAN;
              lastSat = NAN;
              lastVal = NAN;
              lastExtrapolated = false;
              Serial.printf("[%lu] [MEAS]  FAILED — sensor read error. Draining.\n", millis());
              setState(ProcessState::DRAIN);
              break;
            }
                sampleValid = true;
                ledOn(false);
                Serial.printf("[%lu] [MEAS]  Raw  R=%-5u G=%-5u B=%-5u C=%-5u\n",
                              millis(), sample.r, sample.g, sample.b, sample.c);

            if (baselineValid) {
                    // Absorbance/ratio retained for diagnostics only.
                    lastAbs = computeAbsorbance(baseline, sample);
                    lastRatio = computeRatio(lastAbs);

                    Hsv hsv = computeHsv(baseline, sample);
                    lastHue = hsv.h;
                    lastSat = hsv.s;
                    lastVal = hsv.v;
                    Serial.printf("[%lu] [MEAS]  Abs  R=%.4f G=%.4f B=%.4f  (ratio=%.4f)\n",
                                  millis(), lastAbs.r, lastAbs.g, lastAbs.b, lastRatio);
                    Serial.printf("[%lu] [MEAS]  HSV  H=%.1f S=%.3f V=%.3f\n",
                                  millis(), lastHue, lastSat, lastVal);

                    if (isnan(hsv.h) || hsv.s < MIN_VALID_SATURATION || hsv.v < MIN_VALID_VALUE) {
                        lastPh = NAN;
                        lastExtrapolated = false;
                        Serial.printf("[%lu] [MEAS]  pH=NAN — sample too grey/dark "
                                      "(S<%.2f or V<%.2f). Check dose/lighting.\n",
                                      millis(), MIN_VALID_SATURATION, MIN_VALID_VALUE);
                    } else {
                        float uh = unwrapHue(hsv.h);
                        bool extrap = false;
                        lastPh = hueToPh(uh, extrap);
                        lastExtrapolated = extrap;
                        if (isnan(lastPh)) {
                          Serial.printf("[%lu] [MEAS]  pH=NAN (check calibration table)\n", millis());
                        } else {
                          Serial.printf("[%lu] [MEAS]  pH=%.2f%s\n", millis(), lastPh,
                                        extrap ? " (EXTRAPOLATED — outside calibrated span)" : "");
                        }
                    }
                }

                // A clean reading just completed — queue the post-cycle rinse so
                // DRAIN hands off to it instead of COOL_DOWN. Sensor-error paths
                // above drain directly and leave rinseRemaining at 0 (no rinse).
                rinseRemaining = rinseCycles;
                setState(ProcessState::DRAIN);
            }
            break;

        case ProcessState::DRAIN:
            if (now - stateStartMs >= DRAIN_MS) {
            // A completed measurement queues rinseCycles passes; run them before
            // cooling down. An aborted/errored drain leaves rinseRemaining at 0.
            if (rinseRemaining > 0) {
              setState(ProcessState::RINSE_FILL);
            } else {
              setState(ProcessState::COOL_DOWN);
            }
            }
            break;

        case ProcessState::RINSE_FILL:
            if (now - stateStartMs >= rinseFillMs) {
                setState(ProcessState::RINSE_DRAIN);
            }
            break;

        case ProcessState::RINSE_DRAIN:
            if (now - stateStartMs >= RINSE_DRAIN_MS) {
                // One pass done. More queued -> fill again; otherwise cool down.
                if (rinseRemaining > 0) {
                    rinseRemaining--;
                }
                if (rinseRemaining > 0) {
                    setState(ProcessState::RINSE_FILL);
                } else {
                    setState(ProcessState::COOL_DOWN);
                }
            }
            break;

        case ProcessState::COOL_DOWN:
          if (now - stateStartMs >= COOL_DOWN_MS) {
            setState(ProcessState::IDLE);
          }
          break;

        case ProcessState::PRIME:
          if (now - stateStartMs >= primeMs) {
            // Only the dye line's primed state gates dosing accuracy; a water
            // prime just tops up pump 1's column and needs no flag.
            if (primeUsesDyePump) {
              dyeLinePrimed = true;
            }
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
    Serial.printf("[CFG]    Base fill:    %u ms (%.2f mL)\n",
                  baseFillMs, baseFillMs * PUMP_ML_PER_S / 1000.0f);
    Serial.printf("[CFG]    Dose:         %u ms (target %.2f mL, primed line)\n",
                  doseMs, DYE_DOSE_ML);
    Serial.printf("[CFG]    Dye line:     %.2f mL  prime %u ms\n",
                  P2_LINE_ML, PRIME_DYE_MS_DEFAULT);
    Serial.printf("[CFG]    Dye primed:   %s\n", dyeLinePrimed ? "yes" : "NO - run /api/prime?pump=dye");
    Serial.printf("[CFG]    Drain duty:   %u %%\n", drainDutyPercent);
    Serial.printf("[CFG]    Rinse:        %u cycle(s), fill %u ms (%.2f mL)\n",
                  rinseCycles, rinseFillMs, rinseFillMs * PUMP_ML_PER_S / 1000.0f);
    Serial.printf("[CFG]    Hue branch:   %.1f deg\n", hueBranchCut);
    Serial.printf("[CFG]    Cal points:   %u  (pH %.2f-%.2f)\n",
                  calCount,
                  calCount > 0 ? calPoints[0].ph : NAN,
                  calCount > 0 ? calPoints[calCount - 1].ph : NAN);
    for (uint8_t i = 0; i < calCount; i++) {
      Serial.printf("[CFG]      pH %.2f -> hue %.1f\n", calPoints[i].ph, calPoints[i].hue);
    }
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
