// Host-side verification of the fluidic geometry block in src/main.cpp.
// The block under test is EXTRACTED FROM main.cpp at build time (geom_block.inc)
// rather than copied, so these assertions always run against shipped source.
#include <cmath>
#include <cstdint>
#include <cstdio>

// Stub the one symbol the block references from earlier in main.cpp: PUMP2_KICK_MS
// is defined above the extracted range. COOL_DOWN_MS now lives inside the block
// (the extraction runs through the rinse constants), so it must NOT be stubbed.
static const uint32_t PUMP2_KICK_MS = 250;

#include "geom_block.inc"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("ok:   %s\n", msg); } } while (0)
static bool approx(float a, float b, float eps) { return fabsf(a - b) <= eps; }

int main() {
    // ---- tube cross-section: pi * 1.5mm^2 = 7.0686 mm^2 -> 0.070686 mL/cm ----
    CHECK(approx(TUBE_ML_PER_CM, (float)(M_PI * 1.5 * 1.5 * 10.0 / 1000.0), 1e-5f),
          "TUBE_ML_PER_CM matches 3mm ID geometry");

    // ---- measured line volumes ----
    CHECK(approx(lineMl(39.0f), 2.757f, 0.002f), "P1 inlet 39cm  = 2.757 mL");
    CHECK(approx(lineMl(12.0f), 0.848f, 0.002f), "P1 outlet 12cm = 0.848 mL");
    CHECK(approx(lineMl(8.0f),  0.565f, 0.002f), "P2 inlet 8cm   = 0.565 mL");
    CHECK(approx(lineMl(10.0f), 0.707f, 0.002f), "P2 outlet 10cm = 0.707 mL");
    CHECK(approx(lineMl(46.0f), 3.252f, 0.002f), "P3 outlet 46cm = 3.252 mL");
    CHECK(approx(P1_LINE_ML, 3.605f, 0.002f), "P1 full line = 3.605 mL");
    CHECK(approx(P2_LINE_ML, 1.272f, 0.002f), "P2 full line = 1.272 mL");
    CHECK(approx(P3_LINE_ML, 4.100f, 0.002f), "P3 full line = 4.100 mL");

    // ---- volume budget must close exactly on the operating cap ----
    CHECK(approx(BASE_FILL_ML + DYE_DOSE_ML + AGITATION_ML, CELL_OPERATING_ML, 1e-4f),
          "base + dye + agitation == 17.09 mL operating volume");
    CHECK(approx(BASE_FILL_ML, 15.65f, 0.001f), "base fill target = 15.65 mL");
    CHECK(CELL_OPERATING_ML < 21.36f, "operating volume stays under 21.36 mL absolute cap");
    CHECK(approx(DYE_DOSE_ML / CELL_OPERATING_ML, 0.02f, 0.0005f),
          "dye dose is 2.0% v/v per indicator spec");

    // ---- flow rates ----
    CHECK(approx(PUMP_ML_PER_S, 0.6167f, 0.001f), "pump 100% = 0.6167 mL/s");
    CHECK(approx(PUMP2_ML_PER_S, 0.2158f, 0.001f), "pump2 35% = 0.2158 mL/s");
    CHECK(approx(PUMP2_KICK_ML, 0.1542f, 0.001f), "250ms kick delivers 0.154 mL");

    // ---- derived durations ----
    // BASE_FILL_MS_DEFAULT is a manual 24000 ms override (user bench call), not
    // the geometry value pump1Ms(15.65) = 25378. Assert the override and, for
    // the record, that geometry still predicts ~25378 so the two stay comparable.
    CHECK(BASE_FILL_MS_DEFAULT == 24000u, "BASE_FILL_MS_DEFAULT = 24000 ms (manual override)");
    CHECK(pump1Ms(BASE_FILL_ML) == 25378u, "geometry still predicts 25378 ms for 15.65 mL");
    CHECK(BASE_FILL_MS_DEFAULT != LEGACY_BASE_FILL_MS, "base fill no longer the legacy 33000 ms");
    CHECK(BASE_FILL_MS_DEFAULT >= BASE_FILL_MS_MIN && BASE_FILL_MS_DEFAULT <= BASE_FILL_MS_MAX,
          "BASE_FILL_MS_DEFAULT inside clamp range");
    // 24000 ms at nominal flow is 14.80 mL; the implied real rate is ~0.652 mL/s.
    CHECK(BASE_FILL_MS_DEFAULT * PUMP_ML_PER_S / 1000.0f < BASE_FILL_ML,
          "24000 ms under-delivers at nominal rate (pump runs faster than nominal)");
    // The bug this change fixes: 33000 ms overshoots the operating cap on its own.
    CHECK(LEGACY_BASE_FILL_MS * PUMP_ML_PER_S / 1000.0f > CELL_OPERATING_ML,
          "legacy 33000 ms would overfill past 17.09 mL (regression guard)");

    CHECK(DOSE_MS_DEFAULT == 1111u, "DOSE_MS_DEFAULT = 1111 ms (primed line)");
    CHECK(DOSE_MS_DEFAULT != LEGACY_DOSE_MS, "dose no longer the legacy 2700 ms");
    CHECK(DOSE_MS_DEFAULT >= DOSE_MS_MIN && DOSE_MS_DEFAULT <= DOSE_MS_MAX,
          "DOSE_MS_DEFAULT inside clamp range");
    // Legacy 2700 ms against the real 0.707 mL outlet: 2x overdose if primed,
    // nothing delivered if drained. Both failure modes asserted.
    float legacyMl = PUMP2_KICK_ML + ((LEGACY_DOSE_MS - PUMP2_KICK_MS) / 1000.0f) * PUMP2_ML_PER_S;
    CHECK(legacyMl > DYE_DOSE_ML * 1.9f, "legacy 2700 ms = >1.9x overdose on a primed line");
    CHECK(legacyMl - lineMl(P2_OUTLET_CM) < 0.05f, "legacy 2700 ms delivers ~nothing on a drained line");

    // Worst-case dead volumes must stay reachable within the clamp.
    CHECK(pump2Ms(DYE_DOSE_ML + lineMl(P2_OUTLET_CM)) <= DOSE_MS_MAX,
          "drained-outlet dose time (4386 ms) fits the clamp");
    CHECK(pump2Ms(DYE_DOSE_ML + P2_LINE_ML) <= DOSE_MS_MAX,
          "fully-empty dose time (7006 ms) fits the clamp");

    CHECK(AGITATION_MS == 1784u, "AGITATION_MS = 1784 ms for 1.10 mL");
    CHECK(approx(AGITATION_MS * PUMP_ML_PER_S / 1000.0f, AGITATION_ML, 0.01f),
          "AGITATION_MS round-trips to 1.10 mL");

    // ---- prime durations cover the line plus overshoot ----
    // ---- prime durations: line volume x overshoot, timed at the duty actually used ----
    // Both primes run their pump at 100% duty (pump1On is a plain digital HIGH;
    // the dye prime goes through pump2Full, not pump2On), so both must be timed
    // with the full-duty model. The overshoots differ on purpose: water is free
    // and gets the full 2x sweep, the methanol reagent gets 1.5x.
    CHECK(approx(PRIME_WATER_OVERSHOOT, 2.0f, 1e-4f), "water prime pushes 2x the line volume");
    CHECK(approx(PRIME_DYE_OVERSHOOT, 1.5f, 1e-4f), "dye prime pushes 1.5x the line volume");
    CHECK(PRIME_DYE_OVERSHOOT < PRIME_WATER_OVERSHOOT,
          "dye overshoot is the thriftier of the two (reagent costs money)");
    CHECK(PRIME_DYE_OVERSHOOT > 1.0f, "dye prime still overshoots enough to sweep the tip");
    CHECK(PRIME_DYE_MS_DEFAULT == pumpFullMs(P2_LINE_ML * PRIME_DYE_OVERSHOOT),
          "dye prime timed at 100% duty (pumpFullMs), not the 35% metering model");
    CHECK(PRIME_WATER_MS_DEFAULT == pumpFullMs(P1_LINE_ML * PRIME_WATER_OVERSHOOT),
          "water prime timed at 100% duty");
    // Regression guard on the bug this fixes: pump2Ms() models 35% duty, so using
    // it for a prime that actually runs at 100% over-delivers by ~2.86x. Assert we
    // are no longer using that figure.
    CHECK(PRIME_DYE_MS_DEFAULT != pump2Ms(P2_LINE_ML * PRIME_DYE_OVERSHOOT),
          "dye prime no longer uses the 35%-duty pump2Ms figure");
    float dyePrimeDeliveredMl = PRIME_DYE_MS_DEFAULT / 1000.0f * PUMP_ML_PER_S;
    CHECK(approx(dyePrimeDeliveredMl, P2_LINE_ML * PRIME_DYE_OVERSHOOT, 0.01f),
          "dye prime really delivers 1.5x the line volume at its true 100% flow");
    // The saving that motivated the change: 0.5x of a 1.272 mL line = ~0.64 mL of
    // reagent kept out of the waste bottle on every prime.
    CHECK(approx(P2_LINE_ML * (2.0f - PRIME_DYE_OVERSHOOT), 0.636f, 0.01f),
          "shortening to 1.5x saves ~0.64 mL of reagent per prime");
    // Both primes must still fill the line at minimum, and fit the clamp.
    CHECK(PRIME_DYE_MS_DEFAULT >= pumpFullMs(P2_LINE_ML),
          "dye prime covers at least the full 1.272 mL line");
    CHECK(PRIME_WATER_MS_DEFAULT >= pumpFullMs(P1_LINE_ML),
          "water prime covers at least the full 3.605 mL line");
    CHECK(PRIME_DYE_MS_DEFAULT <= PRIME_MS_MAX && PRIME_WATER_MS_DEFAULT <= PRIME_MS_MAX,
          "both prime defaults inside clamp range");
    CHECK(PRIME_DYE_MS_DEFAULT >= PRIME_MS_MIN && PRIME_WATER_MS_DEFAULT >= PRIME_MS_MIN,
          "both prime defaults above the clamp floor");
    CHECK(PRIME_DYE_MS_DEFAULT > DOSE_MS_DEFAULT,
          "priming takes longer than a dose (sanity)");
    printf("      [info] dye prime   = %u ms (%.2f mL)\n",
           PRIME_DYE_MS_DEFAULT, dyePrimeDeliveredMl);
    printf("      [info] water prime = %u ms (%.2f mL)\n",
           PRIME_WATER_MS_DEFAULT, PRIME_WATER_MS_DEFAULT / 1000.0f * PUMP_ML_PER_S);

    // ---- drain has margin for cell + line at R385 throughput ----
    const float R385_ML_PER_S = 13.33f;
    float drainNeededMs = ((CELL_OPERATING_ML + P3_LINE_ML) / R385_ML_PER_S) * 1000.0f;
    CHECK(drainNeededMs < DRAIN_MS,
          "DRAIN_MS clears cell + 4.10 mL drain path with margin");
    CHECK(DRAIN_MS - drainNeededMs > 3000.0f, "at least 3 s of air purge remains");

    // ---- post-cycle rinse ----
    // The rinse now mirrors a real fill/drain so it empties the whole wetted zone
    // (including the top-of-fill dye tidemark) rather than just splashing the
    // bottom of the cell. Fill == operating level, drain == full DRAIN_MS.
    CHECK(RINSE_CYCLES_DEFAULT == 0, "auto-rinse defaults to 0 passes (off; rinse manually)");
    CHECK(RINSE_CYCLES_MAX >= RINSE_CYCLES_DEFAULT, "rinse clamp admits the default");
    CHECK(approx(RINSE_FILL_ML, BASE_FILL_ML, 1e-4f), "rinse fills to the full operating level");
    CHECK(RINSE_FILL_MS_DEFAULT == pump1Ms(RINSE_FILL_ML), "rinse fill time derives from the fill volume");
    CHECK(RINSE_FILL_MS_DEFAULT >= RINSE_FILL_MS_MIN && RINSE_FILL_MS_DEFAULT <= RINSE_FILL_MS_MAX,
          "rinse fill default inside clamp range");
    // The rinse washes the same zone a measurement wets, so the dye ring at the
    // top of the fill line gets cleared instead of left above a partial rinse.
    CHECK(approx(RINSE_FILL_ML, BASE_FILL_ML, 1e-4f), "rinse charge matches the measurement fill");
    // A rinse drain must clear its own charge plus the 4.10 mL line with margin.
    float rinseDrainNeededMs = ((RINSE_FILL_ML + P3_LINE_ML) / R385_ML_PER_S) * 1000.0f;
    CHECK(rinseDrainNeededMs < RINSE_DRAIN_MS, "RINSE_DRAIN_MS clears the full charge + drain line");
    // ...and it runs the full drain so the cell comes out empty, not the old
    // shortened "quick" drain.
    CHECK(RINSE_DRAIN_MS == DRAIN_MS, "rinse drain runs the full measurement drain time");

    // ---- sample-changeover purge ----
    // The purge exists because pump 1's line stays charged between runs: whatever
    // was measured last leads the next BASE_FILL. It pushes 2x that line volume so
    // the first line-volume displaces the old sample and the second sweeps behind
    // it, then drains — the same 1-then-sweep logic as a prime.
    CHECK(approx(PURGE_OVERSHOOT, 2.0f, 1e-4f), "purge pushes 2x pump 1's line volume");
    CHECK(approx(PURGE_FILL_ML, 7.21f, 0.01f), "purge charge = 7.21 mL");
    CHECK(PURGE_FILL_MS_DEFAULT == pump1Ms(PURGE_FILL_ML), "purge fill time derives from its volume");
    CHECK(PURGE_FILL_ML > P1_LINE_ML,
          "purge more than clears the 3.605 mL line it is displacing");
    // The whole point is that it is cheaper than a rinse pass in both liquid and
    // time; if that stops being true the routine is pointless.
    CHECK(PURGE_FILL_ML < RINSE_FILL_ML, "purge spends less sample than a rinse pass");
    CHECK(PURGE_FILL_MS_DEFAULT < RINSE_FILL_MS_DEFAULT, "purge is quicker than a rinse pass");
    CHECK(PURGE_FILL_MS_DEFAULT + RINSE_DRAIN_MS < RINSE_FILL_MS_DEFAULT + RINSE_DRAIN_MS,
          "a full purge pass is shorter than a full rinse pass");
    // It shares the rinse's drain, which must still clear its charge + the line.
    float purgeDrainNeededMs = ((PURGE_FILL_ML + P3_LINE_ML) / R385_ML_PER_S) * 1000.0f;
    CHECK(purgeDrainNeededMs < RINSE_DRAIN_MS, "the shared drain clears the purge charge too");
    // A purge charge must stay inside the cell — it is a fill, not a flush-through.
    CHECK(PURGE_FILL_ML < CELL_OPERATING_ML, "purge charge fits under the operating cap");
    printf("      [info] purge       = %u ms (%.2f mL) vs rinse %u ms (%.2f mL)\n",
           PURGE_FILL_MS_DEFAULT, PURGE_FILL_ML, RINSE_FILL_MS_DEFAULT, RINSE_FILL_ML);

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
