# Exhibit Q&A Prep — Optical pH Sensor

A study sheet for defending the project at the university exhibit. It is written to be
**honest**: the device is a working proof-of-concept, not a calibrated instrument, and the
strongest thing you can do in front of examiners is explain exactly what it does, why, and
where its limits are. Judges reward students who know their design's weaknesses better than
students who oversell.

---

## 1. The 30-second pitch

> "It measures pH by **colour** instead of a glass electrode. A drop of universal indicator
> is added to the water sample inside a flow cell, an RGB sensor reads the colour the
> indicator turns, and the firmware converts that colour to a pH value. The whole
> fill → dose → mix → measure → drain cycle is automated by an ESP32 and shown live on a
> web dashboard."

Key phrase to use: **"colorimetric pH measurement with an automated fluidic cycle."**

---

## 2. How it works (core science)

**Q: What is the actual measurement principle?**
Universal indicator changes colour continuously with pH — red/orange at acidic, green near
neutral, blue/violet at alkaline. We shine a stable white light through the dyed sample and a
TCS34725 RGB colour sensor reads how much red, green and blue pass through. From that we
compute the colour and map it to pH.

**Q: What hardware is in it?**
An ESP32 microcontroller, a TCS34725 RGB light sensor, two LEDs (a 3000 K "warm" and a
6500 K "cool") as the light source, a transmissive flow cell with a ~30 mm optical path, and
three small peristaltic pumps — one to fill the sample water, one to dose the indicator dye,
one to drain. The ESP32 also serves the live dashboard over WiFi.

**Q: Why do you convert the colour to "hue" instead of using the raw RGB numbers?**
Raw RGB mixes together *what colour* the sample is with *how bright* the light is. If the LED
dims or the sample is cloudy, all three channels shift and the RGB triplet moves even though
the pH has not changed. We convert to **HSV** and use the **hue** — hue captures "what colour"
on a single circular axis (red → yellow → green → blue) and is far less sensitive to
brightness. Since the indicator sweeps through those colours as pH goes from acidic to
alkaline, hue is close to a single-variable stand-in for pH. That makes the calibration a
simple hue → pH curve.

**Q: What is "dynamic blanking" and why do you do it every cycle?**
Before adding dye, we measure the *clear* sample — that reading is the "blank" or baseline.
Then we measure again *after* the dye. We divide the dyed reading by the blank, per colour
channel, to get **transmittance** (T = sample ÷ baseline). Dividing cancels out anything that
was already there before the dye: the exact LED brightness and any fixed dimming of the
windows. It is the same "zero the instrument on a blank" step done in a lab spectrophotometer,
except we redo it on every run so slow drift cannot accumulate. From transmittance we also get
**absorbance, A = −log₁₀(T)**, which is the standard way chemists express how much light a
sample absorbs.

**Q: Walk me through one measurement cycle.**
The firmware runs a fixed state machine. In order:

1. **Base fill** — pump 1 fills the cell with the sample until the liquid rises above the
   two optical windows (about 14.8 mL). If it did not crest the windows there would be
   nothing to read through, so this step is not shortened.
2. **Blanking** — LEDs on, let them reach a steady brightness, then take the clear-sample
   baseline (an average of 10 readings).
3. **Micro-dose** — pump 2 adds a small, metered dose of indicator (~0.34 mL, about 2% by
   volume, matching the indicator maker's "0.2 mL per 10 mL" guidance).
4. **Agitation** — pump 1 pulses to mix the dye through the sample.
5. **Diffusion** — a short settle so the colour is even and the liquid is still.
6. **Measure** — take the dyed reading (again 10 averaged samples), compute transmittance,
   absorbance, hue, and finally pH.
7. **Drain** — pump 3 empties the cell.
8. **Cool-down** — a brief rest before the next run.

**Q: Why average 10 samples?**
To beat down electrical noise. Averaging 10 readings roughly halves the random scatter versus
a single reading, which matters because the pH is derived from small differences between the
blank and the dyed sample.

---

## 3. The exhibit build (what is different for the demo)

**Q: Is this the same firmware you use in the lab?**
Almost. For the exhibit we set one flag, `EXHIBITION_MODE`, that shortens **only two waiting
steps that have no effect on the reading** — the diffusion settle (15 s → 4 s) and the
between-cycles cool-down (10 s → 3 s). Every step that touches the actual measurement — the
fill that covers the windows, the LED warm-up, the dose, the mixing, and the measurement
itself — is completely unchanged. So the demo lands a reading in about 35 seconds instead of
about 47, and nothing about *how* it measures is faked or altered. The original lab timings
are kept right next to the demo values in the code and it reverts with a one-line change.

**Q: (If pushed) So the demo is faster — does that make it less accurate?**
No. The two steps we trimmed are just *waiting* — no pump runs and no reading is taken during
them. The dye is already fully mixed well before the 4-second settle ends. We did not touch a
single step that affects the number.

---

## 4. Accuracy and limitations (be upfront — this is where you win points)

**Q: How accurate is it?**
Right now it is a **qualitative-to-semi-quantitative** device: it reliably tells acidic from
neutral from alkaline and tracks the right direction, but I am **not** claiming lab-grade
±0.01 pH accuracy. There are three honest reasons, and I can show you each one on the
hardware.

**Q: What are the main sources of error?**

1. **Cloudy optical windows.** The windows are clear acrylic. Our indicator reagent is about
   45% methanol, and methanol slowly *crazes* acrylic — it produces a fine network of surface
   microcracks that scatter light and trap a little dye. Scattered stray light pushes every
   reading toward "less absorbance," which weakens the colour signal the sensor sees. **The
   fix is to replace the acrylic windows with glass, which methanol does not attack.**

2. **Unequal LEDs.** Our cool (6500 K) LED is visibly brighter than the warm (3000 K) one, so
   the light source is short on red. Alkaline (blue) samples carry most of their pH
   information in the red channel — exactly the channel with the least light — so high-pH
   readings are the noisiest. The blanking step cancels a *steady* imbalance, but it still
   costs signal-to-noise in red. **The fix is a proper constant-current LED driver and
   rebalancing the two LEDs.**

3. **Dose repeatability and carryover.** The dye is delivered by a peristaltic pump through a
   short tube. If that tube is not primed, or a little previous sample is left in the fill
   line, the dose or the blank is off. We added prime and purge routines to manage this, but
   it is still a hand-operated bench process, not a sealed cartridge.

**Q: Why does it sometimes not show a pH number?**
That is deliberate and it is the honest part of the design. The firmware has a **quality
gate**: if the measured colour is too weak (low saturation) or the cell is too dark (low
brightness), it **refuses to quote a pH** and instead shows a short note explaining why — for
example "colour too weak or cell too dark." A cheap sensor that always prints a confident
number even from a bad reading is *worse* than one that admits when it cannot see. You will
still see the live colour swatch and the raw colour/absorbance data on those runs, so you can
watch the chemistry happen even when we withhold the number.

**Q: Is the pH number on screen calibrated?**
Be honest here: the dashboard marks it. If the calibration line says **"placeholder (not
bench-calibrated)"**, the number is coming from a rough factory table and is *provisional* —
it shows the method works end to end, but the exact value is not yet trusted. A real
calibration is captured by running known buffer solutions (pH 4.01, 6.86, 9.18) and letting
the device build its own hue → pH curve. Once that is saved, the "placeholder" note goes away.

## 5. Safety (know this cold — judges and lab supervisors ask)

**Q: Is there anything hazardous in the device?**
Yes, and we handle it deliberately. The indicator reagent concentrate is about **45%
methanol**, which is **flammable** (flash point around 19 °C) and toxic if swallowed or
inhaled in quantity. It also contains **phenolphthalein**, which is a listed carcinogen/mutagen
in its pure form. Our precautions:

- The reagent reservoir and dye line are kept **sealed**; the dye only ever moves inside
  tubing and the closed cell.
- The concentrate is kept **away from the powered electronics** — the LEDs and pump drivers
  are the only warm parts, and the reagent is not stored against them.
- The enclosure is **ventilated** so no methanol vapour builds up.
- Volumes are tiny (a fraction of a millilitre of dye per run) and the waste drains to a
  closed container.
- We use methanol-compatible silicone tubing.

**Q: Could someone touch the liquid at the exhibit?**
No — it is a closed fluidic loop. Nothing is open to the public. If you want to see the colour
change I can show it through the cell window and on the dashboard swatch.

**Q: What about the electrical side?**
Low voltage (USB / 5 V logic). One honest note: the two LEDs should be driven through a
transistor/MOSFET driver rather than straight off an ESP32 pin — running them off the bare pin
sits at the chip's current limit and is part of why our warm LED has dimmed. That driver is on
our fix list.

---

## 6. Why this is a research project, not a product

**Q: Could you sell this?**
Not yet, and I would not claim so. This is a **university research prototype** that proves the
method — automated colorimetric pH with live readout. To become a product it would need:
glass optics that survive the reagent, a regulated LED driver, a sealed replaceable reagent
cartridge instead of hand-priming, a factory calibration procedure, and validation against a
reference pH meter across the full range and across temperature. What we have demonstrates the
concept works and, just as importantly, we have *characterised its limits*, which is the
research contribution.

**Q: What would you do next if you had another month?**
In priority order: (1) swap the acrylic windows for glass to kill the scatter and trapped-dye
error; (2) add a proper LED driver and rebalance the warm/cool LEDs so red has real signal;
(3) run a clean calibration on the glass optics against the three buffers and save the curve;
(4) verify dose repeatability with an air-gapped dye tip so it cannot back-siphon; (5)
validate against a commercial pH meter and report the error band.

**Q: What is the novel / interesting part?**
That a *ratiometric, self-blanking* colour measurement lets a cheap RGB sensor behave a bit
like a single-wavelength spectrophotometer, because dividing every reading by a fresh blank
cancels the light source and fixed optical losses. The hard part was not the colour maths —
that checks out to the last digit — it was the **fluidics**: priming, carryover between
samples, and keeping the dose repeatable. That is a genuine, transferable lesson about where
the real error lives in a cheap automated instrument.

---

## 7. Fast-reference cheat sheet

| Judge asks… | One-line answer |
|---|---|
| How does it sense pH? | Universal indicator colour, read by an RGB sensor through the sample. |
| Why hue, not RGB? | Hue = "what colour," independent of brightness; pH maps to it directly. |
| What's blanking? | Divide the dyed reading by a fresh clear reading — cancels light source and fixed losses. |
| How accurate? | Semi-quantitative; reliable direction and range, not yet lab-grade — three known error sources. |
| Why no number sometimes? | A quality gate withholds pH on weak/dark readings instead of guessing. |
| Is the number calibrated? | Only if the dashboard doesn't say "placeholder"; otherwise it's provisional. |
| Biggest limitation? | Acrylic windows craze in methanol → light scatter; the fix is glass. |
| Safety? | Sealed line; reagent is ~45% methanol (flammable) + phenolphthalein; ventilated, low voltage. |
| Product or research? | Research prototype — proves the method and characterises its limits. |
| Next step? | Glass windows, LED driver, clean calibration, validate vs. a reference meter. |

---

## 8. Things NOT to say (they invite trouble)

- ❌ "It's accurate to ±0.01 pH." — You have not validated that; do not claim it.
- ❌ "It's ready to sell / for real water testing." — It's a prototype.
- ❌ "The number is always right." — The quality gate exists precisely because it isn't.
- ❌ Reading the provisional number aloud as fact when the dashboard says "placeholder."
- ✅ Instead: "This proves the method works end-to-end; here's exactly where the error comes
  from and how I'd fix each source." Confident about the concept, honest about the limits.

