#include "LedClock.h"
#include "ColorSchemes.h"
#include <math.h>

// Data pin for the WS2812b strip. GPIO 4 is an ordinary GPIO on the ESP32-C3
// (not a strapping pin), so it is safe to drive the strip from here. (Matches
// the wiring used by the GlowKitchen project.)
#define LED_DATA_PIN 4

// ===== Animation tuning =====
// The strip should feel alive but calm. Two independent slow oscillators give
// each LED a gentle drift in hue and brightness; the eased blend smooths every
// change (hour ticks, scheme switches) over a few seconds.
static const float HUE_PERIOD_S = 16.0f;  // hue drift cycle
static const float HUE_PHASE_STEP = 0.55f; // per-LED phase offset (hue)
static const float HUE_AMP_HOURS = 0.6f;   // how far along the palette to drift

static const float BRI_PERIOD_S = 11.0f;  // brightness shimmer cycle
static const float BRI_PHASE_STEP = 0.80f; // per-LED phase offset (brightness)
static const float BRI_DEPTH = 0.28f;      // shimmer depth (0..1)

static const float EASE_TAU_S = 2.0f;       // normal blend time constant
static const float PARTY_EASE_TAU_S = 0.10f; // snappier blend so dancing stays crisp
static const float TWO_PI_F = 6.2831853f;

// Brightness of the in-progress hour's LED at the very start of the hour. It
// ramps linearly from here up to full by the end of the hour.
static const float LEAD_FLOOR = 0.05f;

void LedClock::begin() {
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds_, NUM_LEDS);
    FastLED.clear(true);
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        cur_[i][0] = cur_[i][1] = cur_[i][2] = 0.0f;
    }
}

// Color for one LED at full liveliness: the palette color at sampleHour, with a
// slow per-LED hue drift (a yellow LED breathes toward orange and back) and an
// independent brightness shimmer. No clock logic (ramps/cutoffs) is applied.
void LedClock::shimmerColor(uint16_t i, float sampleHour, float t, float out[3]) {
    float hueOffset = HUE_AMP_HOURS * sinf(TWO_PI_F * (t / HUE_PERIOD_S) + i * HUE_PHASE_STEP);
    float rgb[3];
    sampleScheme(schemeIndex_, sampleHour + hueOffset, rgb);

    float bri = 1.0f - BRI_DEPTH * (0.5f - 0.5f * sinf(TWO_PI_F * (t / BRI_PERIOD_S) + i * BRI_PHASE_STEP));

    out[0] = rgb[0] * bri;
    out[1] = rgb[1] * bri;
    out[2] = rgb[2] * bri;
}

// Build the per-LED target colors for the clock display.
void LedClock::buildClockTargets(float hourFloat, float t, float target[][3]) {
    // The current hour selects the topmost lit LED. hourFloat in [0,24).
    int topIndex = (int)hourFloat;            // 0..23
    if (topIndex > NUM_LEDS - 1) topIndex = NUM_LEDS - 1;
    float frac = hourFloat - topIndex;        // progress through the current hour

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if ((int)i > topIndex) {
            // Future hours: dark.
            target[i][0] = target[i][1] = target[i][2] = 0.0f;
            continue;
        }

        // Each LED is colored by the palette at the hour it stands for; the
        // leading LED follows the exact current time.
        float sampleHour = (i == topIndex) ? hourFloat : (i + 0.5f);
        shimmerColor(i, sampleHour, t, target[i]);

        // The current hour's LED fills in gradually across the hour: it begins
        // as a faint seed at the top of the hour and reaches full brightness
        // only as the hour ends. So a partly-lit top LED always means "this hour
        // is still in progress"; once the hour passes it joins the fully-lit
        // column below. (LEAD_FLOOR keeps a barely-visible hint at :00 so the
        // active LED never disappears completely.)
        if (i == topIndex) {
            float k = LEAD_FLOOR + (1.0f - LEAD_FLOOR) * frac;
            target[i][0] *= k;
            target[i][1] *= k;
            target[i][2] *= k;
        }
    }
}

// Build targets with every LED lit at full in the current scheme's colors — the
// "Light all" preview. Same shimmer as the clock, but no ramp and no cutoff, so
// you see the whole day's gradient at once.
void LedClock::buildAllOnTargets(float t, float target[][3]) {
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        shimmerColor(i, i + 0.5f, t, target[i]);
    }
}

// Build targets for party mode: a fast scrolling rainbow with a 2-ish Hz beat
// pulse and random white sparkles. With the short party ease the sparkles
// twinkle and the rainbow flows — a lively dance that ignores the clock.
void LedClock::buildPartyTargets(float t, float target[][3]) {
    float scroll = t * 210.0f;                                  // rainbow scroll
    float beat = 0.45f + 0.55f * fabsf(sinf(TWO_PI_F * t * 2.2f));  // pulse

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        if (random8() < 8) {  // ~3% chance per LED per frame: a white sparkle
            target[i][0] = target[i][1] = target[i][2] = 255.0f;
            continue;
        }
        uint8_t hue = (uint8_t)((int)(i * 16 + scroll) & 0xFF);
        CRGB c = CHSV(hue, 255, 255);
        target[i][0] = c.r * beat;
        target[i][1] = c.g * beat;
        target[i][2] = c.b * beat;
    }
}

// A calm rising blue dot shown until we have a real network time.
void LedClock::buildWaitingTargets(float t, float target[][3]) {
    float pos = fmodf(t * 2.0f, (float)NUM_LEDS);  // rises ~2 LEDs/sec, wraps
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        float dist = fabsf((float)i - pos);
        float glow = dist < 1.5f ? (1.5f - dist) / 1.5f : 0.0f;  // soft edges
        target[i][0] = 6.0f * glow;
        target[i][1] = 12.0f * glow;
        target[i][2] = 60.0f * glow;
    }
}

void LedClock::render(float hourFloat, bool haveTime) {
    uint32_t now = millis();
    float dt = lastFrameMs_ ? (now - lastFrameMs_) / 1000.0f : 0.0f;
    lastFrameMs_ = now;
    if (dt > 0.1f) dt = 0.1f;  // clamp after pauses so colors don't jump

    float t = now / 1000.0f;

    // Timed overrides, highest priority first: a scheduled party beats the
    // "Light all" preview, which beats the normal clock.
    bool party = partyUntilMs_ && (int32_t)(partyUntilMs_ - now) > 0;
    if (!party) partyUntilMs_ = 0;
    bool lightAll = !party && lightAllUntilMs_ && (int32_t)(lightAllUntilMs_ - now) > 0;
    if (!lightAll) lightAllUntilMs_ = 0;

    partying_ = party;

    float target[NUM_LEDS][3];
    float tau = EASE_TAU_S;
    if (party) {
        buildPartyTargets(t, target);
        tau = PARTY_EASE_TAU_S;
    } else if (lightAll) {
        buildAllOnTargets(t, target);
    } else if (haveTime) {
        buildClockTargets(hourFloat, t, target);
    } else {
        buildWaitingTargets(t, target);
    }

    easeAndShow(target, dt, tau);
}

void LedClock::partyFor(uint32_t durationMs) {
    uint32_t until = millis() + durationMs;
    if (until == 0) until = 1;  // 0 is our "inactive" sentinel
    partyUntilMs_ = until;
}

void LedClock::lightAllFor(uint32_t durationMs) {
    uint32_t until = millis() + durationMs;
    if (until == 0) until = 1;  // 0 is our "inactive" sentinel
    lightAllUntilMs_ = until;
}

void LedClock::easeAndShow(const float target[][3], float dt, float tau) {
    // Exponential ease toward the target: feels organic and naturally smooths
    // both the shimmer and any larger jumps (hour change, new scheme). A smaller
    // tau (party mode) tracks fast motion; the normal tau is calm and gentle.
    float alpha = primed_ ? (1.0f - expf(-dt / tau)) : 1.0f;
    primed_ = true;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        for (uint8_t c = 0; c < 3; c++) {
            cur_[i][c] += (target[i][c] - cur_[i][c]) * alpha;
        }
        leds_[i] = CRGB((uint8_t)(cur_[i][0] + 0.5f),
                        (uint8_t)(cur_[i][1] + 0.5f),
                        (uint8_t)(cur_[i][2] + 0.5f));
    }

    FastLED.setBrightness(masterBrightness_);
    FastLED.show();
}

String LedClock::colorsJson() const {
    // Mirror what the eye sees: apply master brightness to the live colors.
    String out = "[";
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        uint8_t r = (uint8_t)(cur_[i][0] * masterBrightness_ / 255.0f + 0.5f);
        uint8_t g = (uint8_t)(cur_[i][1] * masterBrightness_ / 255.0f + 0.5f);
        uint8_t b = (uint8_t)(cur_[i][2] * masterBrightness_ / 255.0f + 0.5f);
        char hex[10];
        snprintf(hex, sizeof(hex), "\"#%02x%02x%02x\"", r, g, b);
        if (i) out += ",";
        out += hex;
    }
    out += "]";
    return out;
}
