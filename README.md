# Optical pH Sensor (ESP32)

ESP32 firmware and web UI for a color-based optical pH sensor. The system runs a timed process (fill, blanking, dosing, mixing, diffusion, measurement, drain, cooldown) and reports status plus readings over a web dashboard served from LittleFS.

## Features
- Manual-start process cycle from the web UI
- Live status and progress updates over WebSocket
- TCS34725 color sensor reading and absorbance calculation
- Pump control with PWM for dosing and drain
- LittleFS-hosted UI with real-time telemetry
- Persisted settings and calibration ratios stored in ESP32 Preferences

## Hardware
- ESP32
- TCS34725 color sensor
- Three pumps (fill, dose, drain)
- Status LED on GPIO 2 (through transistor and resistor as per wiring)

For full wiring and timing references, see:
- [Instructions/Electronics Wiring, Component Layout & Timing Guide.md](Instructions/Electronics%20Wiring,%20Component%20Layout%20&%20Timing%20Guide.md)
- [Instructions/End-to-End Process Flow, Wiring, & Timing Guide.md](Instructions/End-to-End%20Process%20Flow,%20Wiring,%20&%20Timing%20Guide.md)
- [Instructions/Complete project document 2.1.md](Instructions/Complete%20project%20document%202.1.md)

## Build and Upload (PlatformIO)
1. Open the project in VS Code with PlatformIO installed.
2. Build and upload the firmware to the ESP32.
3. Upload the filesystem image (LittleFS) so the UI is served correctly:
   - Use the PlatformIO task for "Upload File System Image".

## Configure WiFi
Update the WiFi credentials in [src/main.cpp](src/main.cpp):

```cpp
static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASSWORD";
```

## Web UI
- The UI is served from [data/index.html](data/index.html) at `/`.
- Connect your browser to the ESP32 IP shown in serial logs.
- Click "Start Cycle" to begin a measurement run.

## Reagent: universal indicator (pH 1–14)
This build maps color to pH for a Yamada-type **universal indicator** whose hue sweeps
red → yellow → green → blue → violet across pH 1–14. Instead of a single blue/green
absorbance ratio (which is only monotonic over a narrow band), the firmware converts the
baseline-normalized sample color to **HSV hue** and maps hue → pH through a calibration
table of `{pH, hue}` points.

Safety: the concentrate is ~45% methanol (flash point ~19 °C, flammable) and contains
phenolphthalein (Carc 1A / Muta 2). Keep the reservoir and dye line sealed, ventilate the
enclosure, keep the concentrate away from the powered LED/pumps, and confirm your tubing
and gaskets are methanol-compatible.

## API
- `GET /api/config?baseFillSec=...&drainDuty=...&doseMs=...` updates fill, drain, and dye-dose
  duration (ms, clamped 200–8000) and saves them to NVS.
- `GET /api/calibrate?points=pH:hue,pH:hue,...` replaces the whole hue→pH table (2–8 points,
  pH and hue both strictly ascending). Optional `&hueCut=<deg>` sets the hue branch cut
  (wrap point for the circular hue scale; default 320°).
- `GET /api/calibrate/capture?ph=<known>` records the current live hue from the most recent
  measurement as the hue for that buffer pH, inserting/replacing the matching table point.
  Requires a valid recent reading (adequate saturation/value).
- `GET /api/status` returns settings, the calibration table + calibrated pH span, and live
  telemetry (pH, hue/sat/val, per-channel absorbance, and an `extrapolated` flag when the
  reading falls outside the calibrated hue span).

## Bench calibration & dosing procedure
1. **Set the dye dose.** Start at `doseMs=2700` (≈0.34 mL, ~2% v/v of the 17.09 mL operating
   volume — the manufacturer's recommended dose). Run a cycle with a mid-pH buffer and check
   the MEASURE telemetry: saturation/value should be well above the reject gate and not pinned.
   If colors are too weak, nudge the dose up toward ~3% v/v; if the cell is saturated/opaque,
   reduce it. Bench-measure the actual mL delivered per dose — the 2700 ms figure assumes the
   dye line re-pays its dead volume each shot; a primed line may need closer to ~1100 ms.
   After changing the dose, rebalance the base fill so the cell still reaches ~17.09 mL.
2. **Capture calibration points.** For each buffer on hand (4.01, 6.86, 9.18), fill the cell,
   run a full measurement cycle, then call `/api/calibrate/capture?ph=<buffer>` (or use the
   dashboard's capture control). Capture in ascending pH order.
3. **Check monotonicity.** Hue must increase with pH across your points. If a capture is
   rejected as non-monotonic, the branch cut is likely splitting your hue range — adjust
   `hueCut` so all captured hues fall on one continuous arc, then re-capture.
4. **Verify the span.** Readings between your lowest and highest buffer are interpolated;
   outside that span the status reports `extrapolated: true`. Add buffers (up to 8 points) to
   widen the trustworthy range.

## Notes
- The calibration table is empty/placeholder until you capture your own buffers.
- Legacy 3-ratio calibration keys (`calPh4/calPh55/calPh7`) from the old B/G-ratio model are
  discarded from NVS on first save.
- If the sensor is not detected at boot, the firmware retries in the background.
- If the LittleFS UI is missing, `/` returns a 404 message that tells you to upload `data/index.html`.

## Troubleshooting
- If the UI does not load, re-upload LittleFS.
- If WiFi fails, confirm SSID/password and power stability.
- Verify sensor wiring and I2C pins if the sensor is not detected.
