# Optical pH Sensor (ESP32)

ESP32 firmware and web UI for a color-based optical pH sensor. The system runs a timed process (fill, blanking, dosing, mixing, diffusion, measurement, drain, rinse, cooldown) and reports status plus readings over a web dashboard served from LittleFS.

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

## Fluidics: measured tube lengths
All pump timings are derived from the tubing geometry rather than hand-tuned, so re-measuring a
line means changing one constant. With 3 mm ID silicone each cm holds 0.0707 mL.

| Pump | Inlet | Outlet | Line volume | Notes |
| --- | --- | --- | --- | --- |
| 1 water fill | 39 cm | 12 cm | 3.61 mL | reservoir → cell |
| 2 dye dose | 8 cm | 10 cm | 1.27 mL | outlet alone is 0.71 mL = 2.1× a dose |
| 3 drain | 12 cm | 46 cm | 4.10 mL | 3.25 mL of dyed waste sits downstream |

Volume budget closes on the cell's 17.09 mL operating volume: 15.65 mL base fill + 0.34 mL dye
(2% v/v) + 1.10 mL agitation. Absolute cell capacity is 21.36 mL, and the dye injection port sits
above the operating line.

Two consequences worth knowing before you bench it. The dye outlet holds 2.1× a full dose, so an
unprimed line swallows the first shot entirely — prime it, and treat `dyeLinePrimed: false` in the
status JSON as "readings are not trustworthy yet". And pump 3's 46 cm outlet holds enough dyed
waste to siphon back into the cell, so route it downhill or give it an air break.

If you re-measure a tube, edit the `*_CM` constants in the fluidic geometry block at the top of
[src/main.cpp](src/main.cpp); every duration recomputes at compile time.

## API
- `GET /api/config?baseFillSec=...&baseFillMs=...&drainDuty=...&doseMs=...&rinseCycles=...&rinseFillMs=...`
  updates fill, drain, and dye-dose duration (ms, clamped 200–8000), the number of automatic
  post-cycle rinse passes (0–5), and the rinse fill time (ms), and saves them to NVS. Prefer
  `baseFillMs`: one second of fill is 0.617 mL, which is 3.6% of the cell and too coarse to land on
  target. `baseFillMs` wins if both are supplied.
- `GET /api/prime?pump=dye|water[&ms=...]` runs a supply pump to fill its line, so dead volume is
  known rather than assumed. Defaults cover the measured line plus 15% overshoot (dye 6300 ms,
  water 7000 ms). Only allowed from idle (409 otherwise); run with the cell empty and drain after.
- `GET /api/rinse[&cycles=N]` flushes the cell with clean water to clear dye carryover before the
  next run: N fill+drain passes (default `rinseCycles`, min 1), then cool down. Only allowed from
  idle (409 otherwise). This is the manual twin of the automatic rinse that already follows every
  successful measurement.
- `GET /api/calibrate?points=pH:hue,pH:hue,...` replaces the whole hue→pH table (2–8 points,
  pH and hue both strictly ascending). Optional `&hueCut=<deg>` sets the hue branch cut
  (wrap point for the circular hue scale; default 320°).
- `GET /api/calibrate/capture?ph=<known>` records the current live hue from the most recent
  measurement as the hue for that buffer pH, inserting/replacing the matching table point.
  Requires a valid recent reading (adequate saturation/value).
- `GET /api/status` returns settings, the calibration table + calibrated pH span, a `fluidics`
  object with the volumes the current timings deliver, `dyeLinePrimed`, and live telemetry (pH,
  hue/sat/val, per-channel absorbance, and an `extrapolated` flag when the reading falls outside
  the calibrated hue span).

## Bench calibration & dosing procedure
1. **Prime both lines.** Call `/api/prime?pump=water` then `/api/prime?pump=dye` with the cell
   empty, then run a drain to clear the overshoot. `dyeLinePrimed` goes true and the dashboard
   warning clears. Re-prime after any reservoir swap, tube change, or long idle period.
2. **Verify the fill level.** Run a cycle and watch the base fill. The default is 24000 ms (a
   manual override; geometry predicts 25378 ms for 15.65 mL at the nominal 37 mL/min, but the
   pump runs slightly fast). The level should crest the optical windows with headspace to spare.
   If it is off, trim with `/api/config?baseFillMs=...` and
   note the ratio, since the same error scales the dose and agitation volumes.
3. **Verify the dose.** Dose into a graduated container and confirm 0.34 mL lands at the default
   1111 ms. If the delivered volume is short by roughly 0.7 mL, the line drained between runs —
   re-prime and re-measure rather than inflating `doseMs` to compensate. Trim via
   `/api/config?doseMs=...`. Check the MEASURE telemetry: saturation and value should sit well
   above the reject gate without pinning. Weak colour, nudge toward 3% v/v; opaque, back off.
4. **Capture calibration points.** For each buffer (4.01, 6.86, 9.18), fill the cell, run a full
   cycle, then call `/api/calibrate/capture?ph=<buffer>` or use the dashboard control. Capture in
   ascending pH order.
5. **Check monotonicity.** Hue must increase with pH. If a capture is rejected as non-monotonic,
   the branch cut is splitting your hue range — adjust `hueCut` so all captured hues fall on one
   continuous arc, then re-capture.
6. **Verify the span.** Readings between your lowest and highest buffer are interpolated; outside
   that span the status reports `extrapolated: true`. Add buffers (up to 8 points) to widen the
   trustworthy range.

## Residual liquid between runs
After a cycle, the two supply lines hold leftover liquid, but they are not the same problem.

Pump 1's line stays full of clean sample water and that is intentional. The peristaltic head
pinches the tube shut when idle, so the primed column holds. `BASE_FILL` actually depends on it:
if the line drained, the first ~0.85 mL of the next fill would re-wet the tube instead of the cell.
So the line is left charged — re-prime with `/api/prime?pump=water` only after a reservoir swap,
tube change, or long idle. Do not try to empty it.

Pump 3 is the line that matters. Its inlet holds ~0.85 mL of dyed water and the cell walls keep a
tinted film; left there, that residue corrupts the next `BLANKING` baseline and every reading drifts
with it. Its 46 cm / 3.25 mL outlet can also siphon dyed waste back toward the cell if it does not
run downhill. Both are handled by the post-cycle rinse: after a successful measurement drains, the
firmware runs `rinseCycles` fill+drain passes (default 1) that fill the cell to the full operating
level and drain it completely. The rinse deliberately mirrors a real fill/drain — full 15.65 mL fill,
full 6 s drain — so the wash covers the entire wetted zone (walls, windows, and the dye tidemark at
the top of the fill line) and the cell comes out empty rather than leaving a ring above a partial
rinse. Trigger it manually from idle with `/api/rinse`, or set `rinseCycles=0` to disable the
automatic rinse (not recommended: dye carryover will tint the baseline). Still route pump 3's outlet
downhill or give it an air break; the rinse reduces siphon-back but is not a substitute for gravity.

## Notes
- The calibration table is empty/placeholder until you capture your own buffers.
- `dyeLinePrimed` resets to false on every boot by design — the firmware cannot know whether the
  line drained while powered down.
- The automatic post-cycle rinse only runs after a clean measurement. Sensor-error and aborted
  cycles drain directly to cool-down without rinsing (`rinseRemaining` stays 0).
- Two stale NVS values are discarded on load: a 33000 ms base fill (never bench-verified, delivers
  20.35 mL and overfills past the 17.09 mL cap) and a 2700 ms dose (derived from an assumed 49.5 mm
  dead-volume segment rather than the measured 10 cm outlet). Both fall back to the derived default.
- Legacy 3-ratio calibration keys (`calPh4/calPh55/calPh7`) from the old B/G-ratio model are
  discarded from NVS on first save.
- If the sensor is not detected at boot, the firmware retries in the background.
- If the LittleFS UI is missing, `/` returns a 404 message that tells you to upload `data/index.html`.

## Troubleshooting
- If the UI does not load, re-upload LittleFS.
- If WiFi fails, confirm SSID/password and power stability.
- Verify sensor wiring and I2C pins if the sensor is not detected.
