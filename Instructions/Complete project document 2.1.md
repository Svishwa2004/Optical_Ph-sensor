# OPTICAL pH SENSOR FOR HYDROPONIC SYSTEMS

## Complete Project Document: Version 2.1 (Locally Sourced Portable Architecture)

**Configuration:** 21.36 mL Internal Chamber | Portable 3-Pump Stack | Dynamic Blanking FSM

**Accuracy Target:** ±0.025 pH

**Measurement Method:** Transmissive Colorimetry (Beer-Lambert Absorbance)

## 1\. EXECUTIVE SUMMARY

This document outlines the revised, production-ready architecture for an open-source, fully automated optical pH sensor. Following a rigorous review of the physical STL files and internal geometries, the fluidic volume of the main mixing chamber was verified at exactly 21.36 mL.

To safely accommodate this larger fluid capacity while simultaneously ensuring field portability and lab-grade measurement accuracy, the system architecture utilizes a **Portable 3-Pump Stack** and a highly advanced **Dynamic Blanking** operation sequence.

Unlike early prototypes that relied on clear distilled water, this Version 2.1 system is engineered specifically for real-world hydroponic environments. It dynamically nullifies the unpredictable background tint of active nutrient solutions (which are often stained by iron chelates, humic acids, and root exudates) before dosing. It forces mechanical turbulent mixing via an automated high-velocity agitation blast, and utilizes an active diaphragm wet-vacuum to pull the chamber bone-dry after testing. This renders the device completely independent of physical orientation, leveling, or gravity, allowing for true handheld portability.

## 2\. HARDWARE ARCHITECTURE & FLUIDICS

### 2.1 The Portable Pump Stack

To achieve maximum portability, volumetric precision, and ease of local component sourcing, the fluidics system utilizes three specialized DC pumps. All three are independently driven by **IRLZ44N Logic-Level MOSFETs** controlled by the ESP32 microcontroller, ensuring high-current isolation from the logic circuitry.

| Function | Component | Detailed Justification & Mechanics |
| --- | --- | --- |
| Water Inlet | NKP-DC-S06B 12V Peristaltic Pump | Provides reliable, timed volumetric delivery (~37 mL/min). Crucially, the internal rollers continuously pinch the silicone tube closed against the pump housing when the motor is unpowered. This acts as an automated mechanical check-valve, preventing any accidental back-flow or gravity siphoning into the clean reservoir when the portable unit is carried, dropped, or tilted. |
| Dye Dosing | NKP-DC-S06B 12V Peristaltic Pump (PWM Controlled) | To maximize component availability and maintain a uniform hardware stack, a second NKP pump is used. Because its native 37 mL/min flow rate is too fast for microliter precision, the ESP32 drives its MOSFET at a strict 35% PWM duty cycle. This electronically gear-reduces the motor to ~13 mL/min, allowing for highly precise 1.19 mL micro-dosing while retaining the mechanical check-valve benefits of a peristaltic head. |
| Drain | R385 12V Micro Diaphragm Pump (PWM Controlled) | Acts as a high-suction wet-vacuum. Unlike centrifugal pumps that lose their prime when they suck in air, diaphragm pumps can run completely dry without damage and seamlessly pump mixtures of air and liquid. The ESP32 drives the MOSFET at 5 kHz PWM (default 40% duty) to reduce heat and noise while maintaining vacuum. Duty can be adjusted at runtime to tune suction. It forcefully evacuates the 17.09 mL of fluid in ~1.5 seconds. By overcoming surface tension and capillary pooling in the rectangular chamber floor, it allows the device to be used at any angle. |

### 2.2 Tubing & Connections

*   **Ports:** All 3D-printed inlet, outlet, and dye injection ports feature a rigid **4.5 mm outer diameter**.
*   **Tubing:** Exclusively use **Food-Grade Silicone Tubing (3 mm Inner Diameter / 5 mm Outer Diameter)**.
*   **Mechanical & Chemical Principle:** The highly elastic nature of silicone (typically Shore 50A hardness) allows the 3 mm ID to stretch tightly over the 4.5 mm rigid plastic ports. This creates a permanent, self-healing, leak-proof mechanical seal without the need for bulky zip-ties, metal hose clamps, or chemical adhesives that could off-gas and contaminate the testing chamber. Furthermore, silicone is chemically inert, lacks leachable plasticizers, and will not degrade when exposed repeatedly to the acidic/basic pH indicator dyes.

### 2.3 Electronics & Optics

*   **Microcontroller:** ESP32 DevKit. This acts as the system's brain: it manages the strict millisecond timing of the FSM, interfaces with the I2C color sensor, and performs the heavy floating-point logarithmic math required for Beer-Lambert Absorbance calculations.
*   **Color Sensor:** TCS34725 RGB Sensor. Mounted securely flush against the right 6mm acrylic window to capture maximum light transmission. It features an integrated IR-blocking filter, preventing ambient infrared heat from skewing the colorimetric data.
*   **Light Source:** Dual LEDs (1x 6500K Cool White + 1x 3000K Warm White) wired in parallel. This pairing is vital because standard 6500K LEDs have a severe spectral gap in the red/orange wavelengths (around 600-650nm). The 3000K LED fills this gap perfectly, ensuring the sensor can accurately detect the Methyl Red chemical transition at lower pH levels (pH 4.0 - 5.5).
*   **CRITICAL HARDWARE FIX:** Do _not_ power the LEDs directly from an ESP32 GPIO pin. Combined, they draw ~40mA, which pushes the ESP32 pins to their absolute maximum thermal threshold, risking long-term silicon degradation and GPIO burnout. Instead, use a 2N2222 NPN transistor or an extra IRLZ44N MOSFET to safely switch the 3.3V power to the LEDs using a low-current (micro-amp) logic signal from the ESP32.

## 3\. CHAMBER VOLUME & DOSING CALCULATIONS

Based on the actual manufactured geometry of the STL files (specifically excluding the volume displaced by the sealed 6mm acrylic windows), the absolute maximum internal capacity of the main body is **21.36 mL**.

To prevent fluid from overflowing into the upper dye injection port, and to provide the necessary air-space (headspace) to absorb fluid displacement during turbulent mixing, the operational limit is strictly capped at **80% capacity (17.09 mL)**.

### The 0.04% Mixed Indicator Ratio

Transmissive colorimetry relies entirely on maintaining a consistent optical density across a set path length (in this case, the exceptional 30mm gap between the acrylic windows). To maintain this molecular density, the exact volumetric breakdown per test must strictly adhere to the following scale:

*   **Total Fluid per Test:** 17.09 mL
*   **Hydroponic Water Dose (93.02%):** 15.90 mL
*   **Dye Indicator Dose (6.98%):** 1.19 mL

## 4\. THE "DYNAMIC BLANKING & DIFFUSION" FSM

Because the STL features a wide, flat rectangular floor, injecting dye directly into a still pool of water will cause the heavier dye (which has a slightly higher specific gravity) to sink and pool unevenly. Furthermore, raw hydroponic water has a natural, unpredictable tint that must be mathematically "zeroed out" to prevent skewed pH readings.

To overcome these fluid dynamics and optical challenges without adding physical mechanical stirrers, the firmware must execute this precise 8-State Finite State Machine (FSM):

### State 1: The Base Fill

*   **Action:** The main NKP water pump runs at 100% duty cycle to inject **14.80 mL** of raw hydroponic water into the chamber.
*   **Physics:** The fluid level rises to exactly Y=16.1 mm, just cresting over and completely submerging the 6mm optical windows.

### State 2: Dynamic Blanking (Zeroing)

*   **Action:** The ESP32 turns on the Dual LEDs. The system pauses for 2 seconds to allow the LEDs to reach thermal stability (as LEDs heat up, their forward voltage drops, which slightly alters their brightness). The TCS34725 then takes 10 averaged RGB readings of the _raw, undyed_ hydroponic water.
*   **Logic:** The firmware saves these raw values in RAM as I\_0 (Baseline). This dynamic zeroing effectively mimics a $10,000 dual-beam spectrophotometer. It perfectly nullifies the variable nutrient tint, any micro-algae film on the acrylic, and the gradual dimming of the LEDs over their thousands of hours of lifespan. The LEDs are then turned off.

### State 3: The Micro-Dose

*   **Action:** The second NKP peristaltic pump (running electronically geared down at 35% PWM) activates to inject exactly **1.19 mL** of the BTB/Methyl Red indicator dye.
*   **Physics:** Due to its higher concentration and specific gravity, the dye sinks through the water column and pools heavily in the center of the rectangular chamber floor.

### State 4: The Agitation Blast

*   **Action:** The main NKP water pump kicks back on at 100% maximum velocity to inject the remaining **1.10 mL** of hydroponic water.
*   **Physics:** This creates a high-velocity jet of water that acts as turbulent kinetic energy. The shear forces of the incoming water violently disrupt the pooled dye, forcing it to swirl and mix upward into the surrounding water matrix. The total volume reaches the absolute 17.09 mL safe limit, with the 20% air headspace absorbing the turbulence without overflowing.

### State 5: The Diffusion Pause

*   **Action:** The system suspends all mechanical action and pauses for **10 to 15 seconds**.
*   **Physics:** This quiet period allows natural Brownian motion and osmotic diffusion to carry the dye molecules uniformly into the far corners of the rectangular chamber. It also crucially allows any light-scattering micro-bubbles generated by the high-velocity agitation blast to surface and pop, clearing the optical path.

### State 6: Measurement & Math

*   **Action:** Turn on the Dual LEDs (waiting 2 seconds for thermal stability). The TCS34725 takes 10 averaged RGB readings (I\_raw) through the fully mixed, colored solution.
*   **Math:** The ESP32 calculates true Absorbance (detailed in Section 5) and maps this data to the calibrated, non-linear pH curve to output the final reading.

### State 7: Vacuum Drain

*   **Action:** The R385 diaphragm pump activates for ~3 seconds at 5 kHz PWM (default 40% duty). Duty can be tuned at runtime (e.g., `/api/config?drainDuty=40`) for quieter operation or stronger suction.
*   **Physics:** The pump pulls a hard vacuum on the chamber, aggressively evacuating all 17.09 mL of dyed fluid directly to the waste reservoir. This leaves the chamber completely empty and prepped for the next test cycle, regardless of the physical tilt or orientation of the sensor enclosure.

### State 8: Cool Down

*   **Action:** All outputs remain off for a short cool-down window.
*   **Timing:** 10 seconds.
*   **Purpose:** Allows the R385 motor coils and cam assembly to dissipate heat before the long idle period.

## 5\. REVISED FIRMWARE MATHEMATICS (BEER-LAMBERT LAW)

The previous firmware iteration utilized a simple arithmetic subtraction method (raw - baseline). This is mathematically incorrect in optics and will result in values clamping to zero, as dyed water will always absorb more light and return a lower raw value than clear water. The firmware must adhere to the principles of the Beer-Lambert Law by calculating **Transmittance (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAgCAYAAAAffCjxAAAB0klEQVR4AeyUv0tCURTH37tq0FCUFDWIOjywJgmN/oDG2hyChqaag4a2XJoaWmsI+gPqT2huERoCCQIpNcFMKHDJB/7ocx690u5TSaLJOF/Oud9z3veee+5NZfzR30ho8CCdGSUSiUAkEtkDZ79BOBxOh0KhoGzjCFUqlQUW+yBlmuYk3jHiOYItsAnsdrt9BXLwUpOC2yaexhuOkN/vX2ORbTabVj6f3ygUCjuCVqt1CT/Gx1lwUCwWL8CR1LDeJVdG6BVvKFobh0xCHJZKJYeUBDDBKjDIZRB4k9iFUqoGX8W/CycdWQR2o9HI4L+M80+xWAQGG3XlhKPbAP6R7up4Q/l8vmWCa7pxlIkd42OLHWWTMsQN6DJyEXDnkopZnIMTl3A9RXIBQQSf4EQM9218c0w3py4jR3PjTt93Pp2FbuwpZFnWDB0tSREdafMR/ic8hWzbjlIYAnIkbT7wmnkKcQFJKnvOh5xmXkImV7silRxPez/Ce0ETGmY+IqwJDTMfTSgWi00wn3US8h/9wtG6Hil8T1PRaHSen4570K7X6zWuO/1ZHcc/CA9uOfIs656meJ3PvNIYMPsgnsvlqj1VSGgzghvKRkKDxzaa0T/O6AMAAP//q6DYDwAAAAZJREFUAwBMQdRB8ncaAQAAAABJRU5ErkJggg==))** and **Absorbance (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAgCAYAAAAffCjxAAACCElEQVR4AeyUz0sbURDHdzfZQ0pbaUppSzYJKS2BXkrdnnps0YtnBQ+CF2/qwYPg/yAiiPgneBE8iUfBg4gSPXkIKCaHBCFZFMQY1GT9zMsPdzWYiOIpYb7vzUxmvjvv+zYxtBf6dIlqQtq2bcZisb5IJGLVMnfrkzQqFArDuq6vm6Zp31HUvI6JEonEZ0imaAu4rvud3WedEunVanWSzl9Ag9CU3YuOiNCll6Z/YBtoTPRTdi/aEonAhmFM07xE4xpoaW2JHMcZgORNKBRa5Ug3dRYrmUy+q/tqe5SIa/4IyTgEs+l0+hydDlVXi+VRIq55DJKDTCazKb0c8Vp2ECuVSp1NZFnWD6bpB/M0ukDshOUUGEznG8IXUKBMBA4EAtMEK9ls9phdWaVSqeIIaQ/TfcVvWksiBP5LxRBYjMfjbrwOmnfIhYHOg3y9voACTW6DsWdoGmQa3QvyEWqOwAfwBTTtAVG5XB5B4Hw4HN5oVtUd8pdo5kgI6XvZG/ARicAUj1I0l0qlGjfUqNV4l274/kISTCzTiaugiETcaDT6h3Mv80S5kTP17b2Fad+SEsHlZ/JN+oiVGQj5u1gs5nnCLhn5e7CDweABeRGclKbJi0m8R5AH/4HYGH1X5BckMBBzH3wCXmF7iLekQJDL5RziXuCtafgTUqOOJs5z0SVqr2BXo1fU6BYAAP//IciKGAAAAAZJREFUAwAp08lB/3QdsgAAAABJRU5ErkJggg==))**.

### Step 1: Calculate Transmittance

Transmittance is the ratio of light that successfully passes through the sample compared to the baseline light. For each color channel (Red, Green, Blue):

float T\_green = (float)raw\_g / (float)baseline\_g;

_(Where raw\_g is the dyed State 6 reading, and baseline\_g is the blanked State 2 reading)._

### Step 2: Calculate Absorbance

Because human vision and chemical light absorption scale logarithmically (not linearly), we must convert Transmittance to Absorbance:

float A\_green = -log10(T\_green);

Absorbance scales perfectly linearly with the concentration of the protonated/deprotonated dye molecules.

### Step 3: Ratio-Based pH Calculation

Do not use a single color channel for linear interpolation. Indicator dyes transition along a sigmoidal (S-shaped) curve dictated by the Henderson-Hasselbalch equation. Relying on a single channel makes the sensor highly vulnerable to micro-fluctuations in total fluid volume or LED power. Instead, use a ratio between two complementary channels (e.g., Green/Red or Blue/Green) to map the pH.

float color\_ratio = A\_green / A\_red;

_Note: You must perform a multi-point calibration (e.g., pH 4.0, pH 5.5, pH 7.0) and map the resulting color\_ratio values against these known pH points to generate an accurate calibration curve._

## 6\. UPDATED BILL OF MATERIALS (Key Components)

| Component | Specification | Purpose |
| --- | --- | --- |
| ESP32 DevKit | 3.3V Logic, WiFi/BT | Main microcontroller for strict FSM automation, sensor interfacing, and complex floating-point data processing. |
| TCS34725 | I2C, 3.3V | High-precision RGB Color Sensor with integrated IR blocking filter for optical clarity. |
| Dual LEDs | 6500K + 3000K, 5mm | Broad-spectrum light source carefully chosen to capture the full chemical transition of both the Methyl Red and BTB dyes. |
| 2x NKP-DC-S06B | 12V Peristaltic | One acts as the primary water fill and high-velocity dynamic agitation blast. The second is driven via PWM for precise 1.19 mL dye dosing. Both provide reliable backflow prevention. |
| R385 Diaphragm | 12V Diaphragm Pump | Active vacuum chamber drain to ensure complete liquid evacuation at any physical angle, safely passing air and fluid mixtures. |
| IRLZ44N MOSFET | Logic-Level N-Channel | Switch 12V power safely and efficiently to the three inductive pump motors and the LED cluster without overheating. |
| Silicone Tubing | 3mm ID / 5mm OD | Creates a tight, self-healing, chemical-resistant mechanical seal over the 4.5mm printed ports without hose clamps. |
| Acrylic Rod | 12mm Diameter, Clear | Cut and polished into 2x 6mm long optical windows, creating the highly sensitive 30mm transmissive light path. |
| Indicator Dye | 0.04% BTB + Methyl Red | Custom pH reagent engineered to transition smoothly from pH 4.0 to 7.6. |

_END OF DOCUMENT_