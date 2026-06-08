#include "ColorSchemes.h"
#include <FastLED.h>   // for CHSV -> CRGB conversion (matches GlowKitchen's colors)
#include <math.h>

// ===== Natural Day (default) =====
// RGB keyframes tuned to feel like the real sky: blue night, warm sunrise,
// yellow midday, sunset, twilight. The night blues are kept bright enough to
// read clearly even at lower master-brightness settings.
static const ColorKey NATURAL_KEYS[] = {
    { 0.0f,   20,  38, 120},   // deep night blue
    { 4.0f,   30,  50, 140},   // late night
    { 5.5f,   95,  75, 140},   // pre-dawn violet
    { 6.5f,  225,  95,  65},   // sunrise orange-pink
    { 8.0f,  235, 160,  60},   // warm morning gold
    {11.0f,  255, 210,  70},   // bright yellow
    {13.0f,  255, 228,  95},   // midday
    {16.0f,  255, 190,  80},   // afternoon gold
    {18.0f,  235, 110,  50},   // sunset orange
    {19.5f,  160,  60,  95},   // dusk magenta
    {21.0f,   60,  55, 150},   // twilight blue-violet
    {23.0f,   30,  50, 140},   // night
};

// ===== Themes ported from GlowKitchen =====
// FastLED HSV hue palettes (saturation/value full). Spread across the 24 hours
// and blended, they reproduce GlowKitchen's looks on the vertical strip.

// Rainbow - full spectrum cycling
static const uint8_t RAINBOW_HUES[] = {
    0, 32, 64, 96, 128, 160, 192, 224,
};

// Pink Pony Club - pink, magenta, and pony colors
static const uint8_t PINK_PONY_HUES[] = {
    200, 210, 220, 230, 240, 245, 250, 255,
};

// Ocean Waves - deep blues and teal
static const uint8_t OCEAN_HUES[] = {
    190, 185, 180, 175, 170, 165, 160, 155,
};

// Sunset - warm oranges, pinks, and purple
static const uint8_t SUNSET_HUES[] = {
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45,
    50, 55, 60, 65, 70, 75, 80, 85,
};

// Forest - natural greens and earth tones
static const uint8_t FOREST_HUES[] = {
    70, 75, 80, 85, 90, 95, 100, 105, 110, 115,
    120, 125, 130, 135, 140, 145,
};

// Green - candle-like greens
static const uint8_t GREEN_HUES[] = {
    85, 90, 95, 100, 105, 100, 95, 90,
};

#define KEY_SCHEME(name, arr) \
    { name, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), nullptr, 0 }
#define HUE_SCHEME(name, arr) \
    { name, nullptr, 0, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])) }

const ColorScheme COLOR_SCHEMES[] = {
    KEY_SCHEME("Natural Day",    NATURAL_KEYS),
    HUE_SCHEME("Rainbow",        RAINBOW_HUES),
    HUE_SCHEME("Pink Pony Club", PINK_PONY_HUES),
    HUE_SCHEME("Ocean Waves",    OCEAN_HUES),
    HUE_SCHEME("Sunset",         SUNSET_HUES),
    HUE_SCHEME("Forest",         FOREST_HUES),
    HUE_SCHEME("Green",          GREEN_HUES),
};
const uint8_t COLOR_SCHEME_COUNT = sizeof(COLOR_SCHEMES) / sizeof(COLOR_SCHEMES[0]);

// ---- RGB keyframe sampling ----

static void lerpKeys(const ColorKey& a, const ColorKey& b, float t, float out[3]) {
    out[0] = a.r + (b.r - a.r) * t;
    out[1] = a.g + (b.g - a.g) * t;
    out[2] = a.b + (b.b - a.b) * t;
}

static void sampleKeyframes(const ColorScheme& s, float hour, float out[3]) {
    const ColorKey* k = s.keys;
    const uint8_t n = s.count;
    const float lastHour = k[n - 1].hour;

    // Before the first keyframe: wrap segment from the last key across midnight.
    if (hour < k[0].hour) {
        float span = (k[0].hour + 24.0f) - lastHour;
        float t = (hour + 24.0f - lastHour) / span;
        lerpKeys(k[n - 1], k[0], t, out);
        return;
    }
    for (uint8_t i = 0; i + 1 < n; i++) {
        if (hour <= k[i + 1].hour) {
            float span = k[i + 1].hour - k[i].hour;
            float t = span > 0.0f ? (hour - k[i].hour) / span : 0.0f;
            lerpKeys(k[i], k[i + 1], t, out);
            return;
        }
    }
    // After the last keyframe: wrap back to the first.
    float span = (k[0].hour + 24.0f) - lastHour;
    float t = (hour - lastHour) / span;
    lerpKeys(k[n - 1], k[0], t, out);
}

// ---- Hue palette sampling ----

static void sampleHues(const ColorScheme& s, float hour, float out[3]) {
    const uint8_t* h = s.hues;
    const uint8_t n = s.hueCount;

    // Position within the palette; the list loops back to its start at midnight.
    float pos = (hour / 24.0f) * n;
    int i0 = (int)floorf(pos);
    float f = pos - i0;
    i0 %= n;
    if (i0 < 0) i0 += n;
    int i1 = (i0 + 1) % n;

    // Interpolate the hue along the shortest path around the color wheel so the
    // wrap segment (last hue back to the first) stays smooth.
    int diff = (int)h[i1] - (int)h[i0];
    if (diff > 128) diff -= 256;
    if (diff < -128) diff += 256;
    int hue = (int)lroundf(h[i0] + diff * f) & 0xFF;

    CRGB c = CHSV((uint8_t)hue, 255, 255);
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
}

void sampleScheme(uint8_t schemeIndex, float hour, float outRGB[3]) {
    if (schemeIndex >= COLOR_SCHEME_COUNT) schemeIndex = 0;
    const ColorScheme& s = COLOR_SCHEMES[schemeIndex];

    // Wrap the requested hour into [0, 24).
    hour = fmodf(hour, 24.0f);
    if (hour < 0.0f) hour += 24.0f;

    if (s.keys) {
        sampleKeyframes(s, hour, outRGB);
    } else {
        sampleHues(s, hour, outRGB);
    }
}
