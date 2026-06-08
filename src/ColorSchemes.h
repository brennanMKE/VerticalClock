#ifndef COLORSCHEMES_H
#define COLORSCHEMES_H

#include <Arduino.h>

// A color scheme paints the 24-hour day as a smooth gradient up the strip.
// There are two flavors:
//
//  * RGB keyframes (`keys`): "Natural Day" maps hour-of-day to a real-sky color
//    (dark blue night, sunrise, midday yellow, ...). Luminance varies with the
//    hour, so night is genuinely dim and midday is bright.
//
//  * Hue palettes (`hues`): the themes ported from the GlowKitchen project are
//    lists of FastLED HSV hues (full saturation/value) that we spread across the
//    day and blend between. These are pure color themes — vivid all day.
//
// Exactly one of `keys` / `hues` is set per scheme. Both wrap around midnight so
// colors flow continuously.
struct ColorKey {
    float hour;     // 0.0 .. 24.0
    uint8_t r, g, b;
};

struct ColorScheme {
    const char* name;
    const ColorKey* keys;   // RGB keyframe table, or nullptr for a hue palette
    uint8_t count;          // number of keyframes
    const uint8_t* hues;    // FastLED HSV hue palette, or nullptr for keyframes
    uint8_t hueCount;       // number of hues
};

// All selectable schemes, defined in ColorSchemes.cpp.
extern const ColorScheme COLOR_SCHEMES[];
extern const uint8_t COLOR_SCHEME_COUNT;

// Sample a scheme at a fractional hour (any value; wrapped into [0,24)) and
// write the interpolated color into outRGB[3] (each channel 0..255 as a float).
void sampleScheme(uint8_t schemeIndex, float hour, float outRGB[3]);

#endif // COLORSCHEMES_H
