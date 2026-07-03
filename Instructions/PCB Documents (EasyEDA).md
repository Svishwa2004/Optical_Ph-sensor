# OPTICAL pH SENSOR FOR HYDROPONIC SYSTEMS

## PCB Documents (EasyEDA, 1-Layer, Onboard Buck + 3.3V Reg)

This document defines the PCB capture plan, pin map, BOM, layout rules, fabrication checklist, and review checklist for the optical pH sensor controller.

### Assumptions

- EDA tool: EasyEDA.
- Board: 1-layer (copper bottom), top used for components and jumpers as needed.
- Power: onboard 12V to 5V buck, then 5V to 3.3V LDO.
- Controller: ESP32 module footprint (ESP32-WROOM-32). If you prefer a DevKit header footprint, keep the same net names and connector pin map.
- Dye pump uses ESP32 GPIO32.

---

## 1. Schematic Capture Plan

### 1.1 Power Tree

- J1: 12V input (screw terminal).
- D1: reverse polarity protection (SS14 or SS34 Schottky).
- C1/C2: bulk input caps (100 uF + 0.1 uF).
- U1: buck regulator 12V to 5V (LM2596S-ADJ or MP1584EN).
- L1, D2, C3/C4: per buck datasheet (see BOM for values).
- U2: 3.3V LDO (AMS1117-3.3 or AP2112K-3.3).
- C5/C6: LDO input/output caps per datasheet.
- Net names: VIN_12V, +5V, +3V3, GND.

### 1.2 ESP32 Core

- U3: ESP32-WROOM-32 module.
- EN and IO0 pulled up (10k) to +3V3.
- Reset button to EN (optional).
- Boot button to IO0 (optional).
- UART programming header: TX0, RX0, +3V3, GND (optional).

### 1.3 Pump Drivers (3x)

- Q1/Q2/Q3: IRLZ44N N-MOSFET (through-hole) for Pump 1/2/3.
- Rg1/Rg2/Rg3: 100 ohm gate resistor (series) to reduce ringing.
- Rpd1/Rpd2/Rpd3: 10k pulldown gate-to-GND.
- Df1/Df2/Df3: 1N4007 flyback across each pump motor.
- Pump connectors: J2 (PUMP1), J3 (PUMP2), J4 (PUMP3).
- Net names: PUMP1_GATE, PUMP2_GATE, PUMP3_GATE, PUMP1_NEG, PUMP2_NEG, PUMP3_NEG, VIN_12V.

### 1.4 LED Driver

- Q4: 2N2222 NPN to switch LED cathodes.
- Rb: 1k base resistor from LED_GATE to Q4 base.
- Rled1/Rled2: 100 ohm series resistors to each LED anode.
- LED connector: J5 (LED_A1, LED_A2, LED_K) or place LEDs on board.
- Net names: LED_GATE, LED_A1, LED_A2, LED_K, +3V3.

### 1.5 Sensor I2C Header

- J6: 4-pin header for TCS34725 breakout.
- Pins: +3V3, GND, SDA, SCL.
- Optional pullups: 4.7k to +3V3 on SDA/SCL if your breakout does not already include them.
- Net names: I2C_SDA, I2C_SCL.

---

## 2. Connector Pin Map + Net Names

### J1: 12V Input (Screw Terminal)

- Pin 1: VIN_12V
- Pin 2: GND

### J2: Pump 1 (Water Fill)

- Pin 1: VIN_12V
- Pin 2: PUMP1_NEG

### J3: Pump 2 (Dye)

- Pin 1: VIN_12V
- Pin 2: PUMP2_NEG

### J4: Pump 3 (Drain)

- Pin 1: VIN_12V
- Pin 2: PUMP3_NEG

### J5: LED Pair

- Pin 1: LED_A1 (+3V3 through Rled1)
- Pin 2: LED_A2 (+3V3 through Rled2)
- Pin 3: LED_K (to Q4 collector)

### J6: TCS34725 I2C

- Pin 1: +3V3
- Pin 2: GND
- Pin 3: I2C_SDA (ESP32 GPIO21)
- Pin 4: I2C_SCL (ESP32 GPIO22)

### J7: Optional UART

- Pin 1: +3V3
- Pin 2: GND
- Pin 3: ESP_TX0
- Pin 4: ESP_RX0

---

## 3. ESP32 Pin Map (Firmware-Consistent)

- GPIO21: I2C_SDA
- GPIO22: I2C_SCL
- GPIO2: LED_GATE
- GPIO4: PUMP1_GATE
- GPIO32: PUMP2_GATE (PWM 5 kHz, 35% duty)
- GPIO18: PUMP3_GATE (PWM 5 kHz, 40% duty)

---

## 4. BOM (Suggested Parts)

### Power

- U1: LM2596S-ADJ (buck regulator, TO-263) or MP1584EN (SOP-8)
- L1: 33 uH, 3A shielded inductor (for LM2596) or per MP1584 datasheet
- D2: SS34 Schottky diode (buck freewheel)
- C1: 100 uF, 25V electrolytic (input bulk)
- C2: 0.1 uF, 50V ceramic (input bypass)
- C3: 220 uF, 10V electrolytic (buck output bulk)
- C4: 0.1 uF, 25V ceramic (buck output bypass)
- U2: AMS1117-3.3 (SOT-223) or AP2112K-3.3 (SOT-23-5)
- C5: 10 uF, 10V ceramic (LDO input)
- C6: 10 uF, 10V ceramic (LDO output)

### MCU and IO

- U3: ESP32-WROOM-32 module
- R_EN: 10k, pullup for EN
- R_IO0: 10k, pullup for IO0
- SW_RST: momentary switch (optional)
- SW_BOOT: momentary switch (optional)

### Pump Drivers

- Q1/Q2/Q3: IRLZ44N MOSFET (TO-220)
- Rg1/Rg2/Rg3: 100 ohm gate series
- Rpd1/Rpd2/Rpd3: 10k gate pulldown
- Df1/Df2/Df3: 1N4007 flyback diode

### LED Driver

- Q4: 2N2222 NPN transistor
- Rb: 1k base resistor
- Rled1/Rled2: 100 ohm LED series resistors

### Connectors

- J1: 2-pin screw terminal, 5.08 mm pitch
- J2/J3/J4: 2-pin screw terminals, 3.5 or 5.08 mm pitch
- J5: 3-pin JST-XH or 2.54 mm header
- J6: 4-pin JST-XH or 2.54 mm header
- J7: 4-pin 2.54 mm header (optional)

---

## 5. Layout Guidance (1-Layer)

- Keep the power zone (12V input, buck, MOSFETs, pump connectors) on one edge.
- Keep the logic zone (ESP32, I2C header, LED driver) on the opposite edge.
- Route I2C traces short and away from pump traces.
- Place flyback diodes as close as possible to pump connectors.
- Use wide traces for VIN_12V and pump current paths (at least 60 mil for pump supply).
- Star ground: join pump grounds and logic ground at a single point near the input bulk cap.
- Add a ground fill on the bottom layer and isolate high-current returns from I2C.
- Avoid long stubs on GPIO2 (LED_GATE) to reduce boot-strapping issues.

---

## 6. Fabrication Outputs Checklist (EasyEDA)

- Board stack: 1-layer, 1 oz copper.
- Generate Gerber + Drill.
- Verify drill sizes for screw terminals and TO-220 leads.
- Ensure silkscreen includes connector labels and pin-1 markers.
- Export BOM and CPL (if assembly is planned).
- Check DRC for minimum clearances and trace widths.

---

## 7. Review Checklist

- Pump 2 gate is GPIO32 in schematic and labels.
- All pumps have flyback diodes across motor terminals.
- Gate pulldowns are installed on all MOSFETs.
- 12V and logic grounds are tied at a defined star point.
- I2C pullups are correct (or confirmed on sensor breakout).
- Buck and LDO are placed with correct caps and inductor value.
- LED driver transistor and resistors match the LED current requirement.

---

## 8. Silkscreen Labels (Suggested)

- J1: VIN_12V / GND
- J2: PUMP1
- J3: PUMP2
- J4: PUMP3
- J5: LEDS
- J6: I2C
- J7: UART
- Test pads: TP_12V, TP_5V, TP_3V3, TP_GND
