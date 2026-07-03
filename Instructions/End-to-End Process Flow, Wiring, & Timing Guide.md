# OPTICAL pH SENSOR FOR HYDROPONIC SYSTEMS

## End-to-End Process Flow, Wiring, & Timing Guide (Version 2.1)

This document provides a complete technical walkthrough of the Optical pH Sensor's lifecycle. It details how components are physically placed, how they wire into the ESP32, and the exact millisecond-by-millisecond progression of the automated chemical testing cycle.

## 1\. HARDWARE PLACEMENT & PHYSICAL LAYOUT

To prevent electrical interference and ensure flawless fluid dynamics, the physical placement of components within your enclosure is strictly defined.

### 1.1 Electrical Isolation Zones

*   **The Wet/Power Zone (Left Side):** House the 12V Power Supply, the 3x IRLZ44N MOSFETs, the LM2596 Buck Converter, and all three inductive pumps here.
*   **The Dry/Logic Zone (Right Side):** House the ESP32, the TCS34725 Color Sensor, and the 3D-printed flow cell here.
*   _Rule:_ Never route the 12V pump motor wires parallel to or touching the delicate I2C sensor wires (SDA/SCL) to prevent electromagnetic interference (EMI).

### 1.2 Flow Cell & Tubing Orientation

*   **The 5-Degree Tilt:** Mount the 3D-printed main chamber so it tilts approximately 5° downward toward the drain outlet. This assists the R385 vacuum pump in clearing the rectangular corners completely.
*   **Dye Tubing Length:** The silicone tube connecting Pump 2 (Dye) to the top injection port of the chamber must be **as short as physically possible** (< 10 cm) to prevent residual dye droplets from clinging to the inside of the tube between tests.

## 2\. SYSTEM WIRING & PIN ASSIGNMENTS

The ESP32 orchestrates the entire process. Here is how the physical hardware connects to the microcontroller.

### 2.1 Power Distribution

*   **12V Main Rail:** Powers the positive (+) terminals of Pump 1, Pump 2, and Pump 3.
*   **5V Logic Rail:** A Buck Converter steps 12V down to 5V, connecting to the ESP32 VIN pin to power the brain.
*   **Common Ground:** **CRITICAL.** The grounds for the 12V supply, the 5V buck converter, the ESP32, the TCS34725, and all three MOSFETs must be tied together.

### 2.2 ESP32 Pinout Connections

| Component | ESP32 Pin | Interface Type | Technical Detail |
| --- | --- | --- | --- |
| TCS34725 Sensor | GPIO 21 (SDA) | I2C Data | Connects to SDA on sensor. |
| TCS34725 Sensor | GPIO 22 (SCL) | I2C Clock | Connects to SCL on sensor. |
| Dual LEDs (6500K+3000K) | GPIO 2 | Digital Out | Triggers the 2N2222 Transistor Base. |
| Pump 1 (Water Fill) | GPIO 4 | Digital Out | Triggers MOSFET 1 Gate. |
| Pump 2 (Dye Dose) | GPIO 32 | PWM Out | Triggers MOSFET 2 Gate (35% Duty Cycle). |
| Pump 3 (Vacuum Drain) | GPIO 18 | PWM Out | 5 kHz PWM, default 40% duty (runtime adjustable). |

_(Note: Every pump motor MUST have a 1N4007 flyback diode soldered across its terminals to prevent voltage spikes from destroying the MOSFETs)._

## 3\. THE STEP-BY-STEP PROCESS FLOW (THE 8-STATE FSM)

Once the system boots, the ESP32 enters an infinite loop, executing a perfect chemical test using the **17.09 mL** safe capacity limit of the 21.36 mL chamber. Here is exactly what happens during one complete cycle.

### STATE 1: The Base Fill (Zeroing Prep)

*   **Objective:** Fill the chamber with raw hydroponic water just above the optical windows.
*   **Hardware Action:** ESP32 pulls GPIO 4 HIGH. Pump 1 (NKP) turns on at 100% power (37 mL/min).
*   **Firmware Timing:** The pump runs for exactly **24,000 milliseconds (24.0s)**.
*   **Volume Delivered:** **14.80 mL** of water.
*   **Physical Reality:** The water level rises to Y=16.1 mm. The 6mm acrylic optical windows are now fully submerged in the raw nutrient solution. ESP32 pulls GPIO 4 LOW to stop the pump.

### STATE 2: Dynamic Blanking (Nullifying the Background)

*   **Objective:** Read the natural color of the hydroponic water to cancel out nutrient tints and LED aging.
*   **Hardware Action:** ESP32 pulls GPIO 2 HIGH, activating the transistor to turn on the Dual LEDs. The system pauses for 2,000 ms to let the LEDs reach thermal and lumen stability.
*   **Firmware Timing:** The ESP32 requests 10 consecutive RGB readings from the TCS34725 sensor over 1,000 ms.
*   **Data Processing:** The ESP32 averages these 10 readings and saves them into memory as baseline\_r, baseline\_g, and baseline\_b (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABUAAAAgCAYAAAD9oDOIAAACTklEQVR4AeyVS2sTURiGJ5NIiHhFF1ZzGSSSjeAiKHalCK7clG5c6EZx4b/QH+Cy4KJQ0IWIe0VBpaCIOze1MVjkJHGZaFUSGzIz6fMNJ9NOZ9LO9LJr+V7ec33ynW/OTE1jD/72obtf1FBNLcu6UyqV5tDsBs0Vi8XbcVIIQdlUGw6Hr/GD6B66jr7ImGmaH2lvGSGoUupzs9l8wc4VZACbbzQaj2WMOSVjWykElQ0c8ziw89JG79AQxY5IaCqVstBZKL+Af8MTRSQUkGR5El8CvpSIyOJIKKBLzBn4ArX8Le0kCkF1PT0omc4ngY3WhqBMTJBhAW/jC3jiiIJWoUyQ5Q8UukKFQuE0p3mgX4xb1Wr1AOsDEYKS3VW9okY9l3XbM942K51OP6fzlrt7Hy91Op2ZjeAAlAw2vZ9kfhe1+LFPAB3btp/ik+12+yLuRwBKlmPvZ7lcPgLwCjtryHsZyLrruu5/+pPIjwCUTd79ZPZnNpsN1LPf7x/iR88gm3kvcrmcTb9L5wLyw4dSr2MsmNIzcuH/6nZiM3mKN5BNlnLJpzVhejAYrDD+vlKpHNZjsc3kKb5EGZSK0LV6vf5PaNTPxR0UFX/WD/rHXz84pi3X6zsnOjea7/V6cooiZVscjYnHhvItle/rKzadyufzOVy+tSfwruM4H3A/YkNlBxk9w91MJvOQB3uZ/wSP6M+2Wq2vuB+JoEqpZeo+xd18AuEo0Jv0Z2h79xb3IhHU22EYDm/UolLqDZI66+E12w50bfeY1j50TGF2MLwKAAD//5g9CggAAAAGSURBVAMAaQX2QbW6JjEAAAAASUVORK5CYII=)). ESP32 turns the LEDs off.

### STATE 3: The Precision Micro-Dose

*   **Objective:** Inject the exact required amount of Bromothymol Blue + Methyl Red indicator dye.
*   **Hardware Action:** ESP32 activates the PWM channel on GPIO 32 at a **35% duty cycle**. Pump 2 (NKP) turns on, but its internal motor is electronically geared down to run incredibly slowly (~13 mL/min).
*   **Firmware Timing:** The pump runs for exactly **5,500 milliseconds (5.5s)**.
*   **Volume Delivered:** **1.19 mL** of dye.
*   **Physical Reality:** The highly concentrated dye drops from the top port and sinks heavily into the center of the rectangular chamber, resting on the bottom. ESP32 sets PWM to 0 to stop the pump.

### STATE 4: The Agitation Blast (Turbulent Mixing)

*   **Objective:** Force the sunken dye to mix uniformly with the water without using a mechanical stirrer.
*   **Hardware Action:** ESP32 pulls GPIO 4 HIGH again. Pump 1 (NKP) turns back on at 100% maximum velocity.
*   **Firmware Timing:** The pump runs for exactly **1,780 milliseconds (1.78s)**.
*   **Volume Delivered:** **1.10 mL** of water.
*   **Physical Reality:** A high-pressure jet of water blasts into the chamber, hitting the sunken dye. The kinetic shear force causes violent turbulence, instantly swirling the dye throughout the entire 46mm length of the chamber. Total chamber volume reaches the safe limit of **17.09 mL**. ESP32 pulls GPIO 4 LOW.

### STATE 5: The Diffusion Pause

*   **Objective:** Allow the chemistry to stabilize and optical path to clear.
*   **Hardware Action:** System remains idle.
*   **Firmware Timing:** Delay for **15,000 milliseconds (15.0s)**.
*   **Physical Reality:** Micro-bubbles generated by the agitation blast rise to the 20% air headspace and pop. Osmotic diffusion ensures the indicator molecules are perfectly uniform across the 30mm optical light path.

### STATE 6: Measurement & Math Calculation

*   **Objective:** Read the final color of the mixed solution and calculate the exact pH.
*   **Hardware Action:** ESP32 pulls GPIO 2 HIGH to turn on the Dual LEDs.
*   **Firmware Timing:** After a 1,000 ms warmup, the TCS34725 takes 10 consecutive RGB readings. The ESP32 turns the LEDs off.
*   **Mathematical Processing (Beer-Lambert Law):**
    1.  The ESP32 averages the new readings into test\_r, test\_g, test\_b (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAA0AAAAgCAYAAADJ2fKUAAABdElEQVR4AeyTPUvDUBSGm7hIQYdW/ELaVCIVFUEEJ8HJH+HuP/BPCOIoDoKr4ODqUB0ER7v5FVBoIljTQQQxYMyHz4VryeVGEMfSct6ec899H+7lJDEL//j1PGRZ1iDarlarh+ggq0qlsl+r1cbE3JRBlMvlOEmSczbO0BLaNAxjNE3TBvVJqVR6JRcUqNlsfnme18B0imIMn0B79I5RQ+zTUyHREMJooWnAThRFj6KXlXLSzwbmBeoRQCcMwxdqJXIhzCvCBXzl+/6HqLPSINu2hzEvYoqBL8laaBDXmcI8A/jMJO81goYG0ZtDE4APxWKxQ62FBmFek64bx3HeZa0kBarX60PszqMC17sQOU8KFASBePo2xja6RbmhQKZpznK9SU55wi1Akh4KhHkVywC647V5I+dGF+LtHuekZeHitGuRf5OJeYNPgEPSNn/r0rhDL0Fbcq0ks9VqHbmua7j52lXcctG9nlz/KfUhOaZeHMQ3AAAA//89dcreAAAABklEQVQDAFu1mUHIo59jAAAAAElFTkSuQmCC)).
    2.  **Transmittance (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAgCAYAAAAffCjxAAAB0klEQVR4AeyUv0tCURTH37tq0FCUFDWIOjywJgmN/oDG2hyChqaag4a2XJoaWmsI+gPqT2huERoCCQIpNcFMKHDJB/7ocx690u5TSaLJOF/Oud9z3veee+5NZfzR30ho8CCdGSUSiUAkEtkDZ79BOBxOh0KhoGzjCFUqlQUW+yBlmuYk3jHiOYItsAnsdrt9BXLwUpOC2yaexhuOkN/vX2ORbTabVj6f3ygUCjuCVqt1CT/Gx1lwUCwWL8CR1LDeJVdG6BVvKFobh0xCHJZKJYeUBDDBKjDIZRB4k9iFUqoGX8W/CycdWQR2o9HI4L+M80+xWAQGG3XlhKPbAP6R7up4Q/l8vmWCa7pxlIkd42OLHWWTMsQN6DJyEXDnkopZnIMTl3A9RXIBQQSf4EQM9218c0w3py4jR3PjTt93Pp2FbuwpZFnWDB0tSREdafMR/ic8hWzbjlIYAnIkbT7wmnkKcQFJKnvOh5xmXkImV7silRxPez/Ce0ETGmY+IqwJDTMfTSgWi00wn3US8h/9wtG6Hil8T1PRaHSen4570K7X6zWuO/1ZHcc/CA9uOfIs656meJ3PvNIYMPsgnsvlqj1VSGgzghvKRkKDxzaa0T/O6AMAAP//q6DYDwAAAAZJREFUAwBMQdRB8ncaAQAAAABJRU5ErkJggg==))** is calculated: ![](data:image/png;base64,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) (e.g., test\_g / baseline\_g).
    3.  **Absorbance (![](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABIAAAAgCAYAAAAffCjxAAACCElEQVR4AeyUz0sbURDHdzfZQ0pbaUppSzYJKS2BXkrdnnps0YtnBQ+CF2/qwYPg/yAiiPgneBE8iUfBg4gSPXkIKCaHBCFZFMQY1GT9zMsPdzWYiOIpYb7vzUxmvjvv+zYxtBf6dIlqQtq2bcZisb5IJGLVMnfrkzQqFArDuq6vm6Zp31HUvI6JEonEZ0imaAu4rvud3WedEunVanWSzl9Ag9CU3YuOiNCll6Z/YBtoTPRTdi/aEonAhmFM07xE4xpoaW2JHMcZgORNKBRa5Ug3dRYrmUy+q/tqe5SIa/4IyTgEs+l0+hydDlVXi+VRIq55DJKDTCazKb0c8Vp2ECuVSp1NZFnWD6bpB/M0ukDshOUUGEznG8IXUKBMBA4EAtMEK9ls9phdWaVSqeIIaQ/TfcVvWksiBP5LxRBYjMfjbrwOmnfIhYHOg3y9voACTW6DsWdoGmQa3QvyEWqOwAfwBTTtAVG5XB5B4Hw4HN5oVtUd8pdo5kgI6XvZG/ARicAUj1I0l0qlGjfUqNV4l274/kISTCzTiaugiETcaDT6h3Mv80S5kTP17b2Fad+SEsHlZ/JN+oiVGQj5u1gs5nnCLhn5e7CDweABeRGclKbJi0m8R5AH/4HYGH1X5BckMBBzH3wCXmF7iLekQJDL5RziXuCtafgTUqOOJs5z0SVqr2BXo1fU6BYAAP//IciKGAAAAAZJREFUAwAp08lB/3QdsgAAAABJRU5ErkJggg==))** is calculated: ![](data:image/png;base64,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).
    4.  **Color Ratio:** The ESP32 calculates the ratio between the Blue and Green Absorbance (![](data:image/png;base64,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)).
    5.  **Interpolation:** The ESP32 maps this ratio against the pre-programmed calibration curve (pH 4.0 and pH 7.0 anchors) to determine the exact pH value.
*   **Output:** The final pH value (e.g., "pH 6.15") is logged to the Serial Monitor or sent to your IoT dashboard.

### STATE 7: The Vacuum Drain (Evacuation)

*   **Objective:** Empty the chamber completely, prepping it for the next hour's test.
*   **Hardware Action:** ESP32 drives PWM on GPIO 18 (5 kHz, default 40% duty). Pump 3 (R385 Diaphragm) activates. Duty can be tuned at runtime (e.g., `/api/config?drainDuty=40`).
*   **Firmware Timing:** The pump runs for **3,000 milliseconds (3.0s)**.
*   **Physical Reality:** Operating at 13.33 mL/sec, the R385 aggressively sucks all 17.09 mL of dyed fluid out of the chamber in 1.3 seconds. For the remaining 1.7 seconds, it pulls a hard vacuum of air, aggressively sucking any residual droplets off the rectangular floor and corners. The chamber is left bone-dry. ESP32 pulls GPIO 18 LOW.

### STATE 8: Cool Down

*   **Objective:** Reduce motor heat before the long idle window.
*   **Hardware Action:** All outputs remain off.
*   **Firmware Timing:** **10,000 milliseconds (10.0s)**.

### SYSTEM IDLE

*   After the cool-down window, the ESP32 enters a long delay (e.g., 1 hour) before looping back to **STATE 1** to begin the process all over again.

_END OF DOCUMENT_