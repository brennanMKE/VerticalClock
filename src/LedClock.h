#ifndef LEDCLOCK_H
#define LEDCLOCK_H

#include <Arduino.h>
#include <FastLED.h>

// Drives a vertical WS2812b strip of 24 LEDs as a 24-hour clock.
//
// Reading the time: the strip fills from the bottom (hour 1) toward the top
// (hour 24) as the day progresses. The number of lit LEDs is the current hour;
// the topmost lit LED brightens across its hour so the column grows smoothly.
//
// The look: every lit LED is colored by the active scheme at the hour it
// represents (see ColorSchemes), so the column becomes a painting of the day so
// far — blue night at the bottom, sunrise, midday yellow, and so on. A slow
// per-LED shimmer drifts each color between nearby shades, and all changes ease
// in over a few seconds, so the display feels alive rather than static.
class LedClock {
public:
    static const uint16_t NUM_LEDS = 24;

    // Initialize FastLED. Call once from setup().
    void begin();

    void setScheme(uint8_t index) { schemeIndex_ = index; }
    void setBrightness(uint8_t b) { masterBrightness_ = b; }

    // Temporarily light every LED at full (current scheme colors) for the given
    // duration, then automatically return to the normal clock display. Used by
    // the web UI's "Light all" preview button.
    void lightAllFor(uint32_t durationMs);

    // Suspend the clock and run a lively "party" light show for the given
    // duration, then return to the clock on its own. Triggered at the scheduled
    // party time (and by the web UI's "Party now" button).
    void partyFor(uint32_t durationMs);

    // True while a party is currently running (so callers can report status).
    bool isPartying() const { return partying_; }

    // Render and push one animation frame. Call frequently (~60fps).
    //   hourFloat  current local time as hours since midnight (e.g. 13.5 = 13:30)
    //   haveTime   false until NTP has delivered a real time -> shows a gentle
    //              "waiting" animation instead of the clock.
    void render(float hourFloat, bool haveTime);

    // Snapshot of what's currently on the strip (after master brightness), as a
    // JSON array of "#rrggbb" strings, bottom (hour 1) first. Used by the web UI
    // to mirror the physical strip.
    String colorsJson() const;

private:
    CRGB leds_[NUM_LEDS];
    float cur_[NUM_LEDS][3];   // eased on-screen color, 0..255 per channel
    bool primed_ = false;      // cur_ initialized to first target (no fade-in)

    uint8_t schemeIndex_ = 0;
    uint8_t masterBrightness_ = 140;
    uint32_t lastFrameMs_ = 0;
    uint32_t lightAllUntilMs_ = 0;  // 0 = inactive; else "light all" expiry time
    uint32_t partyUntilMs_ = 0;     // 0 = inactive; else party expiry time
    bool partying_ = false;         // reflects whether the last frame was a party

    void shimmerColor(uint16_t i, float sampleHour, float t, float out[3]);
    void buildClockTargets(float hourFloat, float t, float target[][3]);
    void buildAllOnTargets(float t, float target[][3]);
    void buildPartyTargets(float t, float target[][3]);
    void buildWaitingTargets(float t, float target[][3]);
    void easeAndShow(const float target[][3], float dt, float tau);
};

#endif // LEDCLOCK_H
