# PCB Design Guide - 2-Layer (ESP32 DevKit V1, Through-Hole)

This guide builds a compact 2-layer PCB using the same through-hole component set as the 1-layer version, but with a full ground plane and tighter routing. It follows the pin map in [src/main.cpp](src/main.cpp) and the wiring rules in [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md), [Instructions/Electronics Wiring, Component Layout & Timing Guide.md](Instructions/Electronics%20Wiring,%20Component%20Layout%20%26%20Timing%20Guide.md), and [Instructions/End-to-End Process Flow, Wiring, & Timing Guide.md](Instructions/End-to-End%20Process%20Flow,%20Wiring,%20%26%20Timing%20Guide.md).

## 1. Scope and Assumptions

- Board stack: 2-layer (top and bottom copper), components on top, bottom as continuous GND plane.
- Controller: ESP32 DevKit V1 (DOIT style, 30-pin, 2x15 headers, 2.54 mm pitch). Verify your board length and row spacing before final PCB.
- Power: 12V input, LM2596 buck module to 5V, then ESP32 DevKit V1 generates 3.3V on its onboard regulator.
- Pumps: NKP-DC-S06B x2 and R385 diaphragm pump x1, as documented in [Instructions/Complete project document 2.1.md](Instructions/Complete%20project%20document%202.1.md).
- Sensor: TCS34725 breakout on I2C, as in [platformio.ini](platformio.ini).
- LED driver: 2N2222 transistor with 1k base resistor and 100 ohm LED resistors.
- Connectors: 5.08 mm screw terminals for 12V and pumps; 2.54 mm headers for LED and I2C.

## 2. Net Names and Pin Map (Firmware-Consistent)

Use these nets to match the firmware in [src/main.cpp](src/main.cpp):

| Net | ESP32 Pin | Function |
| --- | --- | --- |
| I2C_SDA | GPIO21 | TCS34725 SDA |
| I2C_SCL | GPIO22 | TCS34725 SCL |
| LED_GATE | GPIO2 | LED driver transistor base |
| PUMP1_GATE | GPIO4 | Pump 1 MOSFET gate |
| PUMP2_GATE | GPIO32 | Pump 2 MOSFET gate (PWM 5 kHz, 35% duty) |
| PUMP3_GATE | GPIO18 | Pump 3 MOSFET gate (PWM 5 kHz, 40% duty) |

Connector nets from [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md):

- VIN_12V, GND (J1)
- PUMP1_NEG, PUMP2_NEG, PUMP3_NEG (J2 to J4)
- LED_A1, LED_A2, LED_K (J5)
- I2C_SDA, I2C_SCL, +3V3, GND (J6)
- ESP_TX0, ESP_RX0, +3V3, GND (J7 optional)

## 3. Exact BOM (Through-Hole Only)

### 3.1 On-Board PCB Components

| Ref | Qty | Exact Model | Package / Footprint | Notes |
| --- | --- | --- | --- | --- |
| J1 | 1 | KF301-2P 5.08 mm | 2-pin screw terminal | 12V input |
| J2, J3, J4 | 3 | KF301-2P 5.08 mm | 2-pin screw terminal | Pump connectors |
| J5 | 1 | 1x3 2.54 mm header | TH header | LED connector |
| J6 | 1 | 1x4 2.54 mm header | TH header | TCS34725 I2C connector |
| J7 | 1 | 1x4 2.54 mm header | TH header | Optional UART |
| H1, H2 | 2 | 1x15 2.54 mm female header | TH header | ESP32 DevKit V1 socket |
| U1 | 1 | LM2596S-ADJ buck module (4-pin) | TH module, 43 x 21 mm typical | 12V to 5V, set to 5.0V |
| D1 | 1 | 1N5822 Schottky | DO-201AD | Reverse polarity protection |
| C1 | 1 | 100 uF, 25V radial | Radial electrolytic | VIN bulk |
| C2 | 1 | 0.1 uF, 50V radial | Radial ceramic | VIN bypass |
| C3 | 1 | 100 uF, 10V radial | Radial electrolytic | 5V bulk near ESP32 |
| C4 | 1 | 0.1 uF, 50V radial | Radial ceramic | 5V bypass |
| Q1, Q2, Q3 | 3 | IRLZ44N | TO-220 | Pump MOSFETs |
| Rg1, Rg2, Rg3 | 3 | 100 ohm, 1/4W | Axial, 7.62 mm pitch | MOSFET gate series |
| Rpd1, Rpd2, Rpd3 | 3 | 10k, 1/4W | Axial, 7.62 mm pitch | MOSFET gate pulldown |
| Df1, Df2, Df3 | 3 | 1N4007 | DO-41 | Flyback diodes at pumps |
| Q4 | 1 | PN2222A | TO-92 | LED driver transistor |
| Rb | 1 | 1k, 1/4W | Axial, 7.62 mm pitch | LED driver base resistor |
| Rled1, Rled2 | 2 | 100 ohm, 1/4W | Axial, 7.62 mm pitch | LED current limit |
| Rpu1, Rpu2 | 2 | 4.7k, 1/4W | Axial, 7.62 mm pitch | I2C pullups (optional) |
| TP_12V, TP_5V, TP_3V3, TP_GND | 4 | Keystone 5000 | TH test point | Power rails test points |

### 3.2 Plug-In and Off-Board Components

| Item | Qty | Exact Model | Connection |
| --- | --- | --- | --- |
| ESP32 DevKit V1 | 1 | DOIT ESP32 DevKit V1 30-pin | Plugs into H1/H2 |
| TCS34725 | 1 | Adafruit TCS34725 breakout (PID 1334) or equivalent | J6 (I2C) |
| Pump 1 | 1 | NKP-DC-S06B | J2 |
| Pump 2 | 1 | NKP-DC-S06B | J3 |
| Pump 3 | 1 | R385 diaphragm pump | J4 |
| LED 1 | 1 | 5 mm cool white LED, 6500K | J5 |
| LED 2 | 1 | 5 mm warm white LED, 3000K | J5 |
| Tubing | As needed | 3 mm ID / 5 mm OD silicone | Flow cell interfaces |

## 4. Footprint and Drill Standards (IPC-7351 / JEDEC Nominal)

Use these global standard dimensions, then verify against the exact part datasheet before release:

| Package | Lead Pitch | Lead Dia | Drill | Body (typical) |
| --- | --- | --- | --- | --- |
| TO-220 | 2.54 mm | 0.8 to 1.0 mm | 1.1 mm | 10.0 x 4.4 mm |
| TO-92 | 1.27 mm | 0.45 to 0.56 mm | 0.8 mm | 4.5 x 4.0 mm |
| DO-41 | 10.16 mm | 0.8 mm | 1.0 mm | 5.2 x 2.7 mm |
| DO-201AD | 15.24 mm | 1.2 mm | 1.5 mm | 9.5 x 5.5 mm |
| Axial 1/4W resistor | 7.62 mm | 0.6 mm | 0.8 mm | 6.3 x 2.3 mm |
| Radial electrolytic | 2.5 mm | 0.6 mm | 0.8 mm | 6.3 to 8.0 mm dia |
| 2.54 mm header | 2.54 mm | 0.6 mm square | 1.0 mm | Header body per vendor |
| 5.08 mm terminal | 5.08 mm | 1.2 mm | 1.4 mm | Body per vendor |

## 5. Placement Plan (Compact, 2-Layer)

### 5.1 Board Orientation (Top View)

- Left edge: power and pumps (wet/high power zone).
- Right edge: ESP32 and sensor headers (dry/logic zone).
- Bottom edge: pump connectors and MOSFETs.
- Top edge: 12V input and buck module.

### 5.2 Placement Table (Top Side)

| Ref | Placement Decision | Rationale |
| --- | --- | --- |
| J1 | Top-left edge | Short path to buck and VIN bulk cap |
| D1, C1, C2 | Immediately next to J1 | Protect and filter 12V input |
| U1 | Left-top quadrant | Keep buck close to VIN and away from I2C |
| C3, C4 | Near ESP32 VIN pin area | Stabilize 5V rail near load |
| J2, J3, J4 | Bottom-left edge | Pump wiring exits power zone |
| Q1-Q3 | Just above J2-J4 | Short drain paths to pump negatives |
| Df1-Df3 | Directly across J2-J4 pins | Minimize flyback loop area |
| Rg1-Rg3, Rpd1-Rpd3 | Adjacent to Q1-Q3 gates | Stable MOSFET control |
| H1/H2 | Right-center | Keeps ESP32 in dry/logic zone |
| J6 | Right edge near H1/H2 | Short I2C traces |
| Rpu1/Rpu2 | Beside J6 | Clean I2C pullups (optional) |
| Q4, Rb, Rled1/2 | Right edge near J5 | LED driver grouped together |
| J5 | Right edge near sensor window | Short LED wiring |
| J7 | Top-right edge | Optional programming access |
| TP_12V, TP_5V, TP_3V3, TP_GND | Near board top | Easy probe access |

### 5.3 ESP32 Antenna Keep-Out

Place the DevKit so its antenna end hangs over the PCB edge or has a 15 mm keep-out region with no copper or parts directly under the antenna.

## 6. Routing Rules (2-Layer)

- Bottom layer: continuous GND plane with via stitching around the pump driver area and near the ESP32 headers.
- Top layer: route signals and power, keep pump supply short and wide.
- Use 40 to 60 mil traces for VIN_12V and pump currents (0.7A load case).
- Use 10 to 15 mil traces for logic and I2C.
- Keep I2C away from high-current pump traces; use GND plane for shielding.
- Place a via near each MOSFET source to tie into the GND plane with a short loop.

## 7. Power and Protection

- Set the LM2596 module output to 5.0V before installation.
- Feed 5.0V into the ESP32 DevKit VIN pin and use the DevKit 3.3V pin for sensor and LED power.
- D1 prevents damage from reverse polarity input.
- C1/C2 damp pump switching noise; C3/C4 stabilize the logic rail.

## 8. Bring-Up Checklist

- Verify 12V and 5V rails at TP_12V and TP_5V before inserting the ESP32.
- Confirm ESP32 3.3V rail at TP_3V3 with the DevKit installed.
- Confirm MOSFET gate pulldowns (Rpd1-3) read 0V at boot.
- Test each pump output with a dummy load before connecting pumps.
- Confirm LED current (adjust Rled1/2 if your LEDs have lower forward voltage).

## 9. References (All Files and Folders)

Root files:
- [custome_Ph.code-workspace](custome_Ph.code-workspace) - workspace definition.
- [platformio.ini](platformio.ini) - libraries and board target.
- [README.md](README.md) - project overview and firmware usage.
- [optical_ph_sensor_v21_schematic.svg](optical_ph_sensor_v21_schematic.svg) - block-level schematic graphic.
- [.gitignore](.gitignore) - ignored build artifacts.

Root folders:
- [.git/](.git/) - git metadata (no PCB content).
- [.pio/](.pio/) - PlatformIO build output (generated).
- [.vscode/](.vscode/) - editor settings.
- [data/](data/) - web UI assets for the device.
- [include/](include/) - header file placeholder.
- [Instructions/](Instructions/) - hardware and process references.
- [lib/](lib/) - project libraries placeholder.
- [src/](src/) - firmware source.
- [test/](test/) - test placeholder.

Instruction files:
- [Instructions/Complete project document 2.1.md](Instructions/Complete%20project%20document%202.1.md) - pump models, LED pairing, optical method.
- [Instructions/Electronics Wiring, Component Layout & Timing Guide.md](Instructions/Electronics%20Wiring,%20Component%20Layout%20%26%20Timing%20Guide.md) - wiring and isolation zones.
- [Instructions/End-to-End Process Flow, Wiring, & Timing Guide.md](Instructions/End-to-End%20Process%20Flow,%20Wiring,%20%26%20Timing%20Guide.md) - process flow and timing assumptions.
- [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md) - schematic plan, net names, and review checklist.

Other files:
- [data/index.html](data/index.html) - web UI (status and timing context).
- [include/README](include/README) - PlatformIO include guidance.
- [lib/README](lib/README) - PlatformIO library guidance.
- [src/main.cpp](src/main.cpp) - firmware pin map and timing.
- [test/README](test/README) - PlatformIO test guidance.
