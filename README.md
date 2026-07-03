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

## API
- `GET /api/config?baseFillSec=...&drainDuty=...` updates the runtime fill and drain settings and saves them to NVS.
- `GET /api/calibrate?ph4Ratio=...&ph55Ratio=...&ph7Ratio=...` stores the three calibration ratios used by the pH mapping.
- `GET /api/status` returns the current settings, calibration values, and live telemetry.

## Notes
- Calibration constants are placeholders until you measure your own buffers.
- If the sensor is not detected at boot, the firmware retries in the background.
- If the LittleFS UI is missing, `/` returns a 404 message that tells you to upload `data/index.html`.

## Troubleshooting
- If the UI does not load, re-upload LittleFS.
- If WiFi fails, confirm SSID/password and power stability.
- Verify sensor wiring and I2C pins if the sensor is not detected.
