// Standalone unit test for the pH-sensor color-math + calibration logic.
// Mirrors the function bodies from src/main.cpp (the Arduino-independent parts)
// so we can verify them with a host g++ compiler. If a body changes in main.cpp,
// update it here too.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>

// ---- mirrored constants ----
static const uint8_t CAL_MAX_POINTS = 8;
static const uint8_t CAL_MIN_POINTS = 2;
struct CalPoint { float ph; float hue; };
static CalPoint calPoints[CAL_MAX_POINTS] = {
    {2.0f, 0.0f}, {4.0f, 35.0f}, {6.0f, 65.0f},
    {7.0f, 95.0f}, {9.0f, 170.0f}, {12.0f, 280.0f},
};
static uint8_t calCount = 6;
static float hueBranchCut = 320.0f;
static const float MIN_VALID_SATURATION = 0.10f;
static const float MIN_VALID_VALUE = 0.04f;

struct Hsv { float h; float s; float v; };

// safeTransmittance stub (matches main.cpp clamping). Kept for documentation and
// future tests even though the simplified hsvFromT below feeds transmittance
// directly, so mark it maybe_unused to keep -Wall quiet.
[[maybe_unused]] static float safeTransmittance(float test, float base) {
    if (base <= 0.0f) return 0.0001f;
    float t = test / base;
    if (t < 0.0001f) t = 0.0001f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

// Simplified computeHsv that takes transmittance values directly (skips the
// RawReading division so we can feed known colors). The HSV math is identical.
static Hsv hsvFromT(float tr, float tg, float tb) {
    Hsv out = {NAN, NAN, NAN};
    float cmax = fmaxf(tr, fmaxf(tg, tb));
    float cmin = fminf(tr, fminf(tg, tb));
    float delta = cmax - cmin;
    out.v = cmax;
    out.s = (cmax <= 0.0f) ? 0.0f : (delta / cmax);
    if (delta <= 1e-6f) { out.h = 0.0f; return out; }
    float h;
    if (cmax == tr)      h = 60.0f * fmodf(((tg - tb) / delta), 6.0f);
    else if (cmax == tg) h = 60.0f * (((tb - tr) / delta) + 2.0f);
    else                 h = 60.0f * (((tr - tg) / delta) + 4.0f);
    if (h < 0.0f) h += 360.0f;
    out.h = h;
    return out;
}

static float unwrapHue(float hue) {
    if (std::isnan(hue)) return NAN;
    return (hue >= hueBranchCut) ? hue - 360.0f : hue;
}

static float hueToPh(float unwrappedHue, bool &outExtrapolated) {
    outExtrapolated = false;
    if (std::isnan(unwrappedHue) || calCount < CAL_MIN_POINTS) return NAN;
    if (unwrappedHue <= calPoints[0].hue) {
        outExtrapolated = (unwrappedHue < calPoints[0].hue);
        float h0 = calPoints[0].hue, h1 = calPoints[1].hue;
        float p0 = calPoints[0].ph, p1 = calPoints[1].ph;
        if (h1 == h0) return p0;
        return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
    }
    uint8_t last = calCount - 1;
    if (unwrappedHue >= calPoints[last].hue) {
        outExtrapolated = (unwrappedHue > calPoints[last].hue);
        float h0 = calPoints[last - 1].hue, h1 = calPoints[last].hue;
        float p0 = calPoints[last - 1].ph, p1 = calPoints[last].ph;
        if (h1 == h0) return p1;
        return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
    }
    for (uint8_t i = 0; i < last; i++) {
        float h0 = calPoints[i].hue, h1 = calPoints[i + 1].hue;
        if (unwrappedHue >= h0 && unwrappedHue <= h1) {
            float p0 = calPoints[i].ph, p1 = calPoints[i + 1].ph;
            if (h1 == h0) return p0;
            return p0 + (unwrappedHue - h0) * (p1 - p0) / (h1 - h0);
        }
    }
    return NAN;
}

static bool validateCalPoints(const CalPoint *pts, uint8_t count) {
    if (count < CAL_MIN_POINTS || count > CAL_MAX_POINTS) return false;
    for (uint8_t i = 0; i < count; i++) {
        if (!std::isfinite(pts[i].ph) || !std::isfinite(pts[i].hue)) return false;
        if (i > 0) {
            if (pts[i].ph <= pts[i - 1].ph) return false;
            if (pts[i].hue <= pts[i - 1].hue) return false;
        }
    }
    return true;
}

static uint8_t parseCalPoints(const std::string &s, CalPoint *out) {
    uint8_t n = 0;
    size_t start = 0;
    while (start < s.length() && n < CAL_MAX_POINTS) {
        size_t comma = s.find(',', start);
        std::string token = (comma == std::string::npos) ? s.substr(start) : s.substr(start, comma - start);
        // trim
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        token = (a == std::string::npos) ? "" : token.substr(a, b - a + 1);
        if (token.length() > 0) {
            size_t colon = token.find(':');
            if (colon == std::string::npos) return 0;
            out[n].ph = strtof(token.substr(0, colon).c_str(), nullptr);
            out[n].hue = strtof(token.substr(colon + 1).c_str(), nullptr);
            n++;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return n;
}

static uint8_t upsertCalPoint(float ph, float hue, CalPoint *out) {
    const float PH_EPS = 0.05f;
    uint8_t n = 0;
    bool replaced = false;
    for (uint8_t i = 0; i < calCount; i++) {
        if (fabsf(calPoints[i].ph - ph) <= PH_EPS) {
            out[n].ph = ph; out[n].hue = hue; n++; replaced = true;
        } else {
            if (n >= CAL_MAX_POINTS) return 0;
            out[n++] = calPoints[i];
        }
    }
    if (!replaced) {
        if (n >= CAL_MAX_POINTS) return 0;
        out[n].ph = ph; out[n].hue = hue; n++;
        for (uint8_t i = 1; i < n; i++) {
            CalPoint key = out[i]; int j = i - 1;
            while (j >= 0 && out[j].ph > key.ph) { out[j + 1] = out[j]; j--; }
            out[j + 1] = key;
        }
    }
    return n;
}

// ---- test helpers ----
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("ok:   %s\n", msg); } } while (0)
static bool approx(float a, float b, float eps = 0.5f) { return fabsf(a - b) <= eps; }

int main() {
    // --- HSV primary colors ---
    Hsv red = hsvFromT(1.0f, 0.0f, 0.0f);
    CHECK(approx(red.h, 0.0f) && red.s > 0.9f, "red -> hue ~0, high sat");
    Hsv green = hsvFromT(0.0f, 1.0f, 0.0f);
    CHECK(approx(green.h, 120.0f), "green -> hue ~120");
    Hsv blue = hsvFromT(0.0f, 0.0f, 1.0f);
    CHECK(approx(blue.h, 240.0f), "blue -> hue ~240");
    Hsv yellow = hsvFromT(1.0f, 1.0f, 0.0f);
    CHECK(approx(yellow.h, 60.0f), "yellow -> hue ~60");

    // --- achromatic (grey) rejected by sat gate ---
    Hsv grey = hsvFromT(0.5f, 0.5f, 0.5f);
    CHECK(grey.s < MIN_VALID_SATURATION, "grey -> low saturation (rejected)");

    // --- unwrapHue branch cut ---
    CHECK(approx(unwrapHue(350.0f), -10.0f), "hue 350 wraps to -10 (below cut 320)");
    CHECK(approx(unwrapHue(100.0f), 100.0f), "hue 100 stays 100");

    // --- hueToPh interpolation (default table) ---
    bool ex;
    // Between (4.0,35) and (6.0,65): hue 50 -> pH 5.0
    float p = hueToPh(50.0f, ex);
    CHECK(approx(p, 5.0f, 0.05f) && !ex, "hue 50 -> pH 5.0, not extrapolated");
    // Exactly on a point
    p = hueToPh(95.0f, ex);
    CHECK(approx(p, 7.0f, 0.01f) && !ex, "hue 95 -> pH 7.0 exact");
    // Below span -> extrapolated low
    p = hueToPh(-20.0f, ex);
    CHECK(ex, "hue -20 flagged extrapolated (below span)");
    // Above span -> extrapolated high
    p = hueToPh(400.0f, ex);
    CHECK(ex, "hue 400 flagged extrapolated (above span)");
    // Monotonic: higher hue -> higher pH
    bool e1, e2;
    CHECK(hueToPh(40.0f, e1) < hueToPh(200.0f, e2), "pH increases monotonically with hue");

    // --- validateCalPoints ---
    CalPoint good[3] = {{4.01f, 30.0f}, {6.86f, 95.0f}, {9.18f, 180.0f}};
    CHECK(validateCalPoints(good, 3), "valid ascending table accepted");
    CalPoint badPh[3] = {{6.0f, 30.0f}, {4.0f, 95.0f}, {9.0f, 180.0f}};
    CHECK(!validateCalPoints(badPh, 3), "non-ascending pH rejected");
    CalPoint badHue[3] = {{4.0f, 95.0f}, {6.0f, 30.0f}, {9.0f, 180.0f}};
    CHECK(!validateCalPoints(badHue, 3), "non-ascending hue rejected");
    CalPoint one[1] = {{7.0f, 90.0f}};
    CHECK(!validateCalPoints(one, 1), "single point rejected (need >=2)");
    CalPoint nanp[2] = {{4.0f, 30.0f}, {NAN, 90.0f}};
    CHECK(!validateCalPoints(nanp, 2), "NaN entry rejected");

    // --- parseCalPoints ---
    CalPoint parsed[CAL_MAX_POINTS];
    uint8_t n = parseCalPoints("4.01:35,6.86:95,9.18:180", parsed);
    CHECK(n == 3 && approx(parsed[0].ph, 4.01f, 0.001f) && approx(parsed[2].hue, 180.0f),
          "parse 3 points correctly");
    CHECK(validateCalPoints(parsed, n), "parsed points validate");
    n = parseCalPoints("4.01,6.86:95", parsed);
    CHECK(n == 0, "malformed pair (missing colon) -> 0");
    n = parseCalPoints(" 4.0 : 30 , 6.0 : 90 ", parsed);
    CHECK(n == 2, "whitespace-tolerant parse");

    // --- upsertCalPoint: replace existing pH ---
    CalPoint out[CAL_MAX_POINTS];
    n = upsertCalPoint(6.0f, 70.0f, out);  // 6.0 exists in default table
    CHECK(n == calCount, "upsert replace keeps count");
    bool found = false;
    for (uint8_t i = 0; i < n; i++) if (approx(out[i].ph, 6.0f, 0.01f)) { found = approx(out[i].hue, 70.0f); }
    CHECK(found, "upsert replaced hue at pH 6.0");

    // --- upsertCalPoint: insert new pH keeps sorted ---
    n = upsertCalPoint(5.0f, 50.0f, out);  // new, between 4 and 6
    CHECK(n == calCount + 1, "upsert insert grows count");
    bool sorted = true;
    for (uint8_t i = 1; i < n; i++) if (out[i].ph <= out[i-1].ph) sorted = false;
    CHECK(sorted, "upsert insert keeps pH sorted ascending");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
