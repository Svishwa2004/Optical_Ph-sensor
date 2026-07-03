**Optical pH Sensor for**

**Hydroponic Systems**

Team Agro

245077U - RASV Perera

Bachelor of Science Honours in Information Technology and Management

Microcontroller Based ICT Project (IS1901)

**Supervisor Name: Mr. B. H. Sudantha**

**1. Introduction**

Measuring pH is an essential process in hydroponics and precision agriculture, as it directly determines the availability of dissolved nutrients to plants. Without accurate and continuous pH monitoring, hydroponic nutrient solutions can drift outside the optimal growing range of pH 5.5 to 6.5, causing nutrient lockout, stunted growth, and crop loss.

The primary challenge in the current landscape is that commercial electrochemical pH probes are expensive, fragile, and have a notoriously short operational lifetime due to glass electrode degradation and reference junction fouling in nutrient solutions. This project details the design and implementation of a custom-built, microcontroller-based optical pH sensor. By using colorimetric detection with a mixed Bromothymol Blue and Methyl Red indicator, a TCS34725 RGB sensor, and an ESP32 controller, this project creates a durable, cost-effective, and repairable alternative to commercial pH electrodes.

**2. Aim and Objectives**

**Aim: **To design, develop, and validate a low-cost optical pH sensor for continuous monitoring of hydroponic nutrient solutions using dual LED colorimetry, an ESP32 microcontroller, and DIY chemical indicators, achieving measurement accuracy of ±0.025–0.03 pH at a cost reduction of over 90% compared to commercial electrochemical pH sensors.

**Objectives:**

Design and fabricate a modular 5-component flow cell (55×32×28mm) using PETG 3D-printed parts and separate acrylic optical windows, featuring dual LED illumination and a 30mm open optical path.

Implement dual LED configuration using warm white (3000K) and cool white (6500K) LEDs to provide broad-spectrum illumination for both Bromothymol Blue and Methyl Red indicators simultaneously.

Develop an ESP32-based control system with an automated Finite State Machine (FSM) controlling sample handling through three peristaltic pumps covering fill, dose, measure, drain, and rinse cycles.

Integrate a TCS34725 RGB colour sensor (MD0670) for high-resolution colorimetric detection with baseline correction and piecewise calibration against three pH buffer solutions.

Formulate and prepare a DIY pH indicator solution combining Bromothymol Blue (0.1% stock) and Methyl Red (0.1% stock) with preservatives, achieving a shelf life of 6–9 months at room temperature.

Calibrate the sensor system using standard pH buffer solutions (pH 4.01, 6.86, and 9.18) and validate accuracy using real-time temperature compensation via a DS18B20 sensor.

Achieve target specifications: ±0.025–0.03 pH accuracy, pH 4.0–7.6 measurement range, and less than 30 seconds per measurement cycle.

**3. Problem and Proposed Solution**

**Problem: **Standard commercial pH electrodes available on the market have a notoriously short lifetime, typically lasting 6–18 months before the glass bulb deteriorates, the reference junction becomes clogged, or calibration drift renders readings unreliable. In nutrient-rich hydroponic solutions, this degradation is accelerated by organic contamination and ionic interference, making continuous electrode-based pH monitoring a costly and maintenance-heavy investment for small-scale farmers.

**Proposed Solution: **The proposed solution is a custom optical pH sensor based on transmissive colorimetry. A mixed Bromothymol Blue and Methyl Red indicator solution is injected into a custom 3D-printed flow cell, and a calibrated dual white LED system illuminates the coloured sample. A TCS34725 RGB sensor reads the transmitted light and an ESP32 calculates the pH from the resulting colour shift using the Beer–Lambert law. Because there is no glass electrode or reference junction, the sensor has no degrading electrochemical components. The indicator solution costs Rs. 0.07 per test compared to Rs. 15 per test for commercial dye, and the entire flow cell can be opened and cleaned in under five minutes.

**4. Implementation**

![placeholder](https://markdowntoword.io/placeholder.png)**Complete Block Diagram**

**Supportive Internal Diagrams**

The core of the hardware implementation relies on a transmissive optical measurement cell. Because the TCS34725 RGB sensor cannot measure pH directly, a pH-sensitive chemical indicator (Bromothymol Blue + Methyl Red) is mixed with the water sample inside the flow cell. The dual white LEDs illuminate the coloured sample, and the RGB sensor reads the transmitted light intensity. The change in colour across the red, green, and blue channels is used to calculate pH using the Colour Index (CI) metric and piecewise linear calibration.

**Optical Path Diagram**

**DUAL LEDs**

6500K + 3000K

→

**Acrylic**

Window

6mm

**MIXING CHAMBER**

30mm optical path | Ø12.2mm bore | 3.507 mL

Water Sample + BTB/MR Dye (7% v/v)

**Acrylic**

Window

6mm

→

**TCS34725**

RGB SENSOR

(MD0670)

Light travels left to right through coloured liquid sample (Beer–Lambert absorbance). Bore diameter: Ø12.2mm. Total path: ~50mm.

**View of Final Product**

The mechanical design of the optical pH sensor consists of a 5-component flow cell assembly: four PETG 3D-printed parts and two separate acrylic optical windows. The horizontal split architecture allows tube insertion without side-loading fragile components, provides a flat gasket sealing surface, and enables the chamber to be opened and cleaned in under five minutes.

**LEFT LED MOUNT**

**BODY TOP HALF**

**BODY BOTTOM HALF**

**SENSOR MOUNT**

**ACRYLIC WINDOWS**

**3 × 32 × 28 mm**

PETG Black

Oval LED pocket (10×6mm)

4× M3×12 screws

**55 × 16 × 28 mm**

PETG Black

Dye port (top)

Gasket groove on split face

**55 × 16 × 28 mm**

PETG Black

Inlet Ø4.5mm + Drain Ø4.2mm

Bore R6.1mm (Ø12.2mm)

**3.5 × 32 × 28 mm**

PETG Black

PCB recess 14×33×1mm

4× M3×12 screws

**2× Ø12mm × 6mm**

Clear PMMA Acrylic

Optical windows

Seated in split pockets

Overall assembly: ~61.5mm × 32mm × 28mm  |  Split join: 4× M3×10 bolts  |  Total heat-set inserts: 12× M3 + 2× M2  |  Optical path: 30mm open chamber

**Project Progress**

**Completed Tasks:**

Hardware design finalised: 4-part PETG flow cell CAD verified against drawings and STL files. All dimensions confirmed (body halves 55×16×28mm, LED mount 3mm, sensor mount 3.5mm).

Optical system assembled: Dual LED system (6500K cool white + 3000K warm white) installed in left cap. TCS34725 MD0670 sensor mounted in sensor mount recess with foam tape.

Electronics wiring complete: ESP32, TCS34725, DS18B20 temperature sensor, and dual LED circuit connected and tested on breadboard.

Firmware Phase 1 (Sketch 1) developed and verified: Hardware verification sketch confirms TCS34725 detection via I2C, LED control, and DS18B20 temperature reading via 1-Wire protocol.

Firmware Phase 2 (Sketch 2) developed: Calibration and pH measurement sketch implements 20-sample averaging, baseline correction, piecewise linear pH calculation, temperature compensation, and EEPROM calibration storage.

pH buffer solutions prepared: Three pH buffer sachets (pH 4.01, pH 6.86, and pH 9.18 at 25°C) dissolved in 250mL distilled water each and verified for colour response.

Pump timing analysed: NKP-DC-S06B pump flow rate (37 mL/min) evaluated. Dye pump assigned 35% PWM (ledcWrite = 89) to achieve reliable 0.223mL dosing over 1,033ms (>1 rotor revolution).

Bill of Materials finalised: All components sourced locally (Tronic.lk, Daraz.lk, hardware stores). Fastener errors identified and corrected across three cart reviews.

Chemical preparation documented: BTB + Methyl Red indicator system documented with corrected dosing (0.223mL per test, 448 tests per 100mL batch, Rs. 0.07 per test).

Comprehensive project documentation completed: Assembly guide, pump calibration guide, chemical preparation guide, pH colour reading guide, BOM, and progress report all produced.

**In Progress:**

**Leak sealing and chamber commissioning: **Water testing of the assembled flow cell identified a leak at the split face join. A 1mm silicone gasket is required between the top and bottom halves to provide a watertight seal. Silicone sealant is also needed at the three port fitting interfaces. This is the current critical path item before dye calibration can proceed.

**Pump procurement and fluidic system installation: **3× NKP-DC-S06B 12V peristaltic pumps, 4mm ID silicone tubing (2m), and 6× barbed fittings are pending procurement. Once installed, the pump timing calibration procedure (Section 4 of Pump Calibration Guide) will be executed for each pump individually.

**Next Steps / Pending Tasks:**

Complete chamber sealing using 1mm silicone sheet gasket on the split face and aquarium silicone sealant at all port fittings. Perform a water-only leak test before introducing any indicator solution.

Install and calibrate all three peristaltic pumps following the Pump Calibration Guide: measure actual flow rate for each pump, calculate timing constants (WATER_FILL_MS, DYE_PUMP_RUN_MS, DRAIN_RUN_MS, WATER_RINSE_MS), and verify dye dose accuracy to ±6% using a 0.01g precision scale.

Prepare the BTB + Methyl Red working solution in collaboration with the University Chemistry Department. Validate the prepared solution against all three pH buffer solutions before sensor calibration.

Execute full two-point (and optionally three-point) pH calibration using Sketch 2: record baseline, pH 4.01 calibration point, pH 6.86 calibration point, and optionally pH 9.18. Verify calibration by re-testing pH 6.86 buffer and confirming reported pH reads within ±0.05.

Perform 100-cycle reliability testing over multiple days to assess measurement consistency, dye degradation rate, and chamber cleanliness after drain and rinse cycles.

Assess long-term stability over a 30-day continuous operation period with hydroponic nutrient solution to validate the ±0.025 pH accuracy target under realistic field conditions.

**5. Components used and Technologies**

**Hardware Components:**

**ESP32 DevKit V1 (ESP-WROOM-32, 30-pin): **The central microcontroller used to run the FSM control logic, manage I2C communication with the TCS34725, control the three pump MOSFETs, read the DS18B20 temperature sensor via 1-Wire, store calibration values in EEPROM, and output pH readings via Serial (115200 baud).

**TCS34725 RGB Colour Sensor (MD0670 Rectangle): **The primary optical sensor. Communicates via I2C (address 0x29) at GPIO 21/22. Provides 16-bit resolution per channel (R, G, B, Clear) with a built-in IR blocking filter. The two onboard white LEDs are disabled by pulling the LED pin LOW at GPIO 4, ensuring only the external dual LED system illuminates the sample.

**Dual White LEDs (6500K Cool White + 3000K Warm White, 5mm): **Used as the broadband light source for transmissive colorimetry. Both LEDs are wired in parallel with individual 100Ω current-limiting resistors to GPIO 2. The dual-temperature configuration achieves ~CRI 85 equivalent, improving sensitivity for Methyl Red (red–yellow transition) and Bromothymol Blue (yellow–blue transition) simultaneously.

**NKP-DC-S06B 12V Mini Peristaltic Pumps (×3): **Used for automated fluid handling. Pump 1 (GPIO 4) fills the chamber with sample water at 100% speed. Pump 2 (GPIO 5) injects the BTB+MR indicator at 35% PWM via ledcWrite to achieve a reliable 0.223mL dose. Pump 3 (GPIO 18) drains the used sample. Pump 1 also handles the post-measurement rinse cycle, eliminating the need for a fourth pump.

**DS18B20 Waterproof Temperature Sensor: **Monitors real-time liquid temperature via 1-Wire protocol at GPIO 5, with a 4.7kΩ pull-up resistor to 3.3V. Provides temperature readings used to correct the reference pH values of the buffer solutions during calibration (coefficients: 0.001 per °C for pH 4.01, 0.003 per °C for pH 6.86, 0.022 per °C for pH 9.18).

**IRFZ44N N-Channel Logic-Level MOSFET (×3): **Switches the 12V DC pump motors from the 3.3V GPIO logic of the ESP32. Each MOSFET gate is connected via a 10kΩ pull-down resistor to ensure the gate stays LOW when the GPIO is floating. A 1N4007 flyback diode protects each MOSFET from back-EMF generated when the pump motor is switched off.

**M3 Brass Heat-Set Inserts and Stainless Steel Bolts: **12× M3×3×4.2mm brass knurled heat-set inserts provide strong, reusable threaded connections in the PETG body. Assembly uses 4× M3×10mm bolts (body split join) and 8× M3×12mm bolts (cap joins). 2× M2 heat-set inserts and 2× M2×6mm bolts secure the TCS34725 PCB in its recess.

**1mm Silicone Sheet (100×100mm) and Aquarium Silicone Sealant: **The silicone sheet is cut to 54×28mm and placed in the gasket groove on the body split face to create a watertight seal. Aquarium-grade silicone sealant is applied to all three port fitting interfaces (water inlet, dye port, drain port).

**Clear PMMA Acrylic Rod (Ø12mm, 250mm length): **Cut into two 6mm pieces to form the optical windows. The windows are seated in split pockets (3mm deep in each half) and align the optical path through the 30mm mixing chamber. Surfaces are polished progressively using 400, 800, and 1000 grit sandpaper to achieve optical clarity.

**Technologies:**

**C/C++ (Arduino IDE / ESP32 Arduino Core): **For ESP32 firmware development. The firmware implements a deterministic Finite State Machine (IDLE → FILL → DOSE → SETTLE → MEASURE → DRAIN → RINSE → ERROR) with timeout protection and sensor read validation.

**I2C Communication Protocol: **Standard two-wire serial interface (SDA at GPIO 21, SCL at GPIO 22) connects the ESP32 to the TCS34725 sensor. The Wire.h library handles all I2C transactions at 400kHz Fast Mode, with the Adafruit_TCS34725 library providing high-level sensor control.

**1-Wire Protocol (DS18B20): **Single-wire digital temperature sensing using the OneWire and DallasTemperature libraries. Provides temperature accuracy of ±0.5°C for real-time buffer pH correction across the 15–40°C ambient range.

**ESP32 LEDC PWM (ledcSetup / ledcWrite): **Used to reduce the dye pump (Pump 2) speed to 35% of full speed (~13 mL/min from 37 mL/min rated). This extends the dye dose run time from 362ms (unreliable, < 1 rotor pass) to ~1,033ms (reliable, > 1 full rotor revolution), achieving ±5% dosing accuracy.

**EEPROM (Persistent Calibration Storage): **The ESP32 EEPROM library stores all calibration values — baseline R/G/B, CI at each pH calibration point, and temperature-corrected reference pH values — in non-volatile memory. Calibration survives power cycles and only needs to be repeated when the indicator solution batch is replaced.

**Piecewise Linear pH Calibration Model: **Two calibration segments are implemented: Segment 1 covers pH 4.01 to 6.86 using CI₄ and CI₆ reference points; Segment 2 covers pH 6.86 to 9.18 using CI₆ and CI₉. This allows the sensor to handle the non-linear combined indicator response across the full hydroponic range.

**Work Distribution:**

Literature review and optical pH sensing approach research: Individual — 1 week

CAD modelling of flow cell (4 parts, drawings, STL export): Individual (with CAD tool support) — 2 weeks

Hardware procurement, assembly, and wiring: Individual — 2 weeks

Firmware development (Sketch 1 verification + Sketch 2 calibration): Individual — 1 week

Chemical documentation and indicator formulation research: Individual — 1 week

Documentation (assembly guide, calibration guide, chemical guide, BOM): Individual — 4 days

Pump installation and calibration: Individual — pending (Week 7)

Chemical preparation (BTB + MR stocks): University Chemistry Department collaboration — pending (Week 7)

Full system integration testing and 100-cycle reliability validation: Individual — pending (Weeks 8–13)