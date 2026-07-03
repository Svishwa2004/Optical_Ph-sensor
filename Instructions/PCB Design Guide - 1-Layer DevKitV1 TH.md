# PCB Design Guide - 1-Layer (Beginner Friendly, ESP32 DevKit V1, Through-Hole)

This guide is written for beginners using EasyEDA. It keeps the same design (ESP32 DevKit V1 plugged into the PCB) but explains it in simple steps. It still follows the pin map in [src/main.cpp](src/main.cpp) and the wiring/layout rules in [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md), [Instructions/Electronics Wiring, Component Layout & Timing Guide.md](Instructions/Electronics%20Wiring,%20Component%20Layout%20%26%20Timing%20Guide.md), and [Instructions/End-to-End Process Flow, Wiring, & Timing Guide.md](Instructions/End-to-End%20Process%20Flow,%20Wiring,%20%26%20Timing%20Guide.md).

## 1. What You Are Building (Simple View)

You will make a one-layer PCB that:
- Takes 12V power.
- Uses a small buck module to make 5V.
- Holds a plug-in ESP32 DevKit V1 (the brain and Wi-Fi).
- Connects three pumps, two LEDs, and one color sensor.
- Lets you screw wires in and out using terminals.

Diagram idea: one simple block picture showing 12V in -> buck -> ESP32 -> pumps, LEDs, sensor.

## 2. Quick Facts (Keep These the Same)

- Board: 1-layer copper on bottom, parts on top, jumpers on top.
- Controller: ESP32 DevKit V1 (DOIT style, 30-pin, 2x15 headers, 2.54 mm pitch).
- Power: 12V -> LM2596 buck -> 5V -> ESP32 makes 3.3V.
- Pumps: NKP-DC-S06B x2, R385 x1 (see [Instructions/Complete project document 2.1.md](Instructions/Complete%20project%20document%202.1.md)).
- Sensor: TCS34725 on I2C (see [platformio.ini](platformio.ini)).
- LED driver: PN2222A transistor + 1k base resistor + 100 ohm LED resistors.

## 3. Tools and EasyEDA Setup

- EasyEDA account (free).
- Basic soldering tools.
- A ruler or caliper for checking the ESP32 header spacing.

EasyEDA steps (high level):
1. Create a new project.
2. Draw the schematic first.
3. Assign footprints (through-hole only).
4. Convert to PCB.
5. Place parts, then route on one layer.
6. Run DRC, then export Gerber files.

Diagram idea: a simple 4-step flow: Schematic -> Footprints -> PCB -> Gerbers.

## 4. Pin Map (Must Match the Firmware)

Use these exact names so the code matches the board:

| Net | ESP32 Pin | What It Does |
| --- | --- | --- |
| I2C_SDA | GPIO21 | Sensor data (TCS34725 SDA) |
| I2C_SCL | GPIO22 | Sensor clock (TCS34725 SCL) |
| LED_GATE | GPIO2 | Turns LED driver on/off |
| PUMP1_GATE | GPIO4 | Pump 1 control |
| PUMP2_GATE | GPIO32 | Pump 2 control (PWM 5 kHz, 35% duty) |
| PUMP3_GATE | GPIO18 | Pump 3 control (PWM 5 kHz, 40% duty) |

Connector net names you should keep (from [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md)):
- VIN_12V, GND (J1)
- PUMP1_NEG, PUMP2_NEG, PUMP3_NEG (J2 to J4)
- LED_A1, LED_A2, LED_K (J5)
- I2C_SDA, I2C_SCL, +3V3, GND (J6)
- ESP_TX0, ESP_RX0, +3V3, GND (J7 optional)

## 5. Parts List (Through-Hole Only)

This keeps the same parts as the original guide. Use these exact parts to avoid mistakes.

### 5.1 On the PCB

| Ref | Qty | Part | Footprint | Why It Is Here |
| --- | --- | --- | --- | --- |
| J1 | 1 | KF301-2P 5.08 mm | 2-pin screw terminal | 12V power in |
| J2, J3, J4 | 3 | KF301-2P 5.08 mm | 2-pin screw terminal | Pump outputs |
| J5 | 1 | 1x3 2.54 mm header | TH header | LED connector |
| J6 | 1 | 1x4 2.54 mm header | TH header | Sensor connector |
| J7 | 1 | 1x4 2.54 mm header | TH header | Optional UART |
| H1, H2 | 2 | 1x15 2.54 mm female header | TH header | ESP32 socket |
| U1 | 1 | LM2596S-ADJ module | TH module, 43 x 21 mm typical | 12V to 5V |
| D1 | 1 | 1N5822 Schottky | DO-201AD | Reverse polarity safety |
| C1 | 1 | 100 uF, 25V | Radial electrolytic | 12V smoothing |
| C2 | 1 | 0.1 uF, 50V | Radial ceramic | 12V noise filter |
| C3 | 1 | 100 uF, 10V | Radial electrolytic | 5V smoothing |
| C4 | 1 | 0.1 uF, 50V | Radial ceramic | 5V noise filter |
| Q1, Q2, Q3 | 3 | IRLZ44N | TO-220 | Pump switches |
| Rg1, Rg2, Rg3 | 3 | 100 ohm | Axial, 7.62 mm pitch | Gate resistors |
| Rpd1, Rpd2, Rpd3 | 3 | 10k | Axial, 7.62 mm pitch | Keep pumps off at boot |
| Df1, Df2, Df3 | 3 | 1N4007 | DO-41 | Protect from pump kickback |
| Q4 | 1 | PN2222A | TO-92 | LED driver |
| Rb | 1 | 1k | Axial, 7.62 mm pitch | LED driver base resistor |
| Rled1, Rled2 | 2 | 100 ohm | Axial, 7.62 mm pitch | LED current limit |
| Rpu1, Rpu2 | 2 | 4.7k | Axial, 7.62 mm pitch | I2C pullups (optional) |
| TP_12V, TP_5V, TP_3V3, TP_GND | 4 | Keystone 5000 | TH test point | Easy voltage checks |

### 5.2 Plug-In and Off-Board Parts

| Item | Qty | Part | Connection |
| --- | --- | --- | --- |
| ESP32 DevKit V1 | 1 | DOIT ESP32 DevKit V1 30-pin | Plugs into H1/H2 |
| TCS34725 | 1 | Adafruit TCS34725 breakout (PID 1334) | J6 (I2C) |
| Pump 1 | 1 | NKP-DC-S06B | J2 |
| Pump 2 | 1 | NKP-DC-S06B | J3 |
| Pump 3 | 1 | R385 diaphragm pump | J4 |
| LED 1 | 1 | 5 mm cool white LED, 6500K | J5 |
| LED 2 | 1 | 5 mm warm white LED, 3000K | J5 |
| Tubing | As needed | 3 mm ID / 5 mm OD silicone | Flow cell |

## 6. EasyEDA Step-by-Step (Beginner Version)

### 6.1 Make the Schematic

1. Place all parts from the list above.
2. Use net labels (names) instead of long wires. Use the exact names in the pin map.
3. Connect power: VIN_12V -> buck module -> 5V -> ESP32 VIN.
4. Connect the ESP32 pins to the nets in the table (I2C_SDA, I2C_SCL, LED_GATE, PUMP1_GATE, PUMP2_GATE, PUMP3_GATE).
5. Add the pump connectors and flyback diodes (Df1-Df3) across the pump terminals.
6. Add the LED driver (Q4 + Rb + Rled1/2) and LED connector (J5).
7. Add the I2C connector (J6) for the TCS34725.
8. Add test points (TP_12V, TP_5V, TP_3V3, TP_GND).

Diagram idea: a small schematic snapshot showing how one pump channel is wired (MOSFET + diode + connector).

### 6.2 Assign Footprints

1. For each part, choose a through-hole footprint that matches the part size.
2. Use the same footprint sizes listed in the parts table above.
3. For the ESP32 socket, use two 1x15 female headers (2.54 mm pitch).

### 6.3 Make the PCB

1. Convert to PCB.
2. Set the board to 1-layer (bottom copper only).
3. Draw a rectangular board outline big enough for the ESP32 and connectors.
4. Place parts using the placement guide in the next section.
5. Route power traces first (thick traces for 12V pump power).
6. Route signal traces next (thin traces for logic and I2C).
7. Use top-side jumpers where traces must cross.
8. Add a copper pour for GND on the bottom.

Diagram idea: a top-view picture with two zones labeled (power zone and logic zone).

## 7. Placement (Where Parts Go)

Keep the board in two zones:
- Left side = power and pumps (wet, noisy, high current).
- Right side = ESP32 and sensor (dry, quiet, low current).

Simple placement plan (top view):
- Top-left: J1, D1, C1, C2, U1.
- Bottom-left: J2, J3, J4 with Q1-Q3 above them.
- Right-center: H1/H2 (ESP32 socket).
- Right edge: J6 (I2C), J5 (LED), Q4, Rb, Rled1/2.
- Top-right: J7 (optional UART).
- Test points near the top edge.

ESP32 antenna keep-out:
- Keep the antenna end over the PCB edge or leave a 15 mm clear area under it (no copper, no parts).

## 8. Routing (How to Draw the Traces)

- Use thick traces for 12V pump power (>= 60 mil).
- Use thinner traces for logic and I2C (20 to 30 mil).
- Keep I2C traces short and away from pump traces.
- Make one star ground point near C1 where power ground and logic ground meet.
- Add a bottom copper pour for GND, but keep pump returns separate until the star point.
- Use jumpers on the top side if you must cross traces.

## 9. Power and Safety (Do Not Skip)

- Set the LM2596 output to 5.0V before soldering it in.
- Feed 5V into the ESP32 VIN pin (not 3.3V).
- Use D1 to protect against reversed 12V input.
- Always place flyback diodes (Df1-Df3) across each pump output.

## 10. Footprint and Drill Starting Sizes

Use these as a starting point, then check the part datasheet if you can:

| Package | Drill | Notes |
| --- | --- | --- |
| TO-220 | 1.1 mm | MOSFETs |
| TO-92 | 0.8 mm | PN2222A |
| DO-41 | 1.0 mm | 1N4007 |
| DO-201AD | 1.5 mm | 1N5822 |
| Axial 1/4W | 0.8 mm | Resistors |
| Radial electrolytic | 0.8 mm | C1, C3 |
| 2.54 mm header | 1.0 mm | All headers |
| 5.08 mm terminal | 1.4 mm | Screw terminals |

## 11. First Power-Up Checklist

1. Do not plug in the ESP32 yet.
2. Apply 12V and check TP_12V = 12V and TP_5V = 5V.
3. Remove 12V, then plug in the ESP32.
4. Power again and check TP_3V3 = 3.3V.
5. Check that the pumps stay off at boot (gate pulldown resistors).
6. Test each pump output with a dummy load before a real pump.
7. Test the LEDs and sensor last.

## 12. Common Beginner Mistakes

- Swapping VIN and GND on the 12V input.
- Forgetting the flyback diodes on pumps.
- Mixing up ESP32 header spacing (measure first).
- Routing I2C lines next to pump power traces.
- Using 3.3V input instead of 5V on the ESP32 VIN pin.

## 13. References (Keep for More Detail)

- [Instructions/Complete project document 2.1.md](Instructions/Complete%20project%20document%202.1.md) - pump models, LED pairing, optical method.
- [Instructions/Electronics Wiring, Component Layout & Timing Guide.md](Instructions/Electronics%20Wiring,%20Component%20Layout%20%26%20Timing%20Guide.md) - wiring and isolation zones.
- [Instructions/End-to-End Process Flow, Wiring, & Timing Guide.md](Instructions/End-to-End%20Process%20Flow,%20Wiring,%20%26%20Timing%20Guide.md) - process flow and timing assumptions.
- [Instructions/PCB Documents (EasyEDA).md](Instructions/PCB%20Documents%20%28EasyEDA%29.md) - schematic plan, net names, and review checklist.
- [src/main.cpp](src/main.cpp) - firmware pin map and timing.
