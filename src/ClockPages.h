#ifndef CLOCKPAGES_H
#define CLOCKPAGES_H

#include <Arduino.h>
#include <EasyWiFi.h>
#include <CustomPageHandler.h>  // not pulled in by EasyWiFi.h
#include <Preferences.h>
#include "LedClock.h"

// A selectable timezone: human-readable label + POSIX TZ string.
struct TimezoneOption {
    const char* label;
    const char* posix;
};

// Custom EasyWiFi web page for the Vertical Clock. Serves the device root ("/")
// with a live preview of the strip plus controls for timezone, color scheme,
// and brightness. Choices persist to NVS and apply immediately to the LEDs.
class ClockPages : public CustomPageHandler {
public:
    ClockPages(ConfigServer& cs, LedClock& clock) : configServer(cs), ledClock(clock) {}

    // Load saved settings from NVS (falling back to defaults) and apply them.
    // Call once after RunLoop::setup().
    void begin();

    // Accessors for the main loop.
    String getTimezone() const { return timezone; }
    uint8_t getSchemeIndex() const { return schemeIndex; }
    uint8_t getBrightness() const { return brightness; }

    // CustomPageHandler: register our routes on EasyWiFi's web server.
    void registerRoutes() override;

private:
    ConfigServer& configServer;
    LedClock& ledClock;
    Preferences prefs;

    String timezone;
    uint8_t schemeIndex = 0;
    uint8_t brightness = 140;

    void applyTimezone();   // setenv("TZ", ...) + tzset()
    void applyToLeds();     // push scheme + brightness into LedClock

    void handleRoot();      // GET /      -> controls + live strip preview
    void handleSave();      // POST /save -> persist + apply + redirect
    void handleApply();     // GET /apply -> apply scheme/brightness live (no NVS)
    void handleAll();       // GET /all   -> light every LED full for 10s
    void handleState();     // GET /state -> JSON {valid,time,hour,leds[]}
};

// Available timezones and the saved default, defined in ClockPages.cpp.
extern const TimezoneOption TIMEZONE_OPTIONS[];
extern const size_t TIMEZONE_OPTION_COUNT;
extern const char* DEFAULT_TIMEZONE;

#endif // CLOCKPAGES_H
