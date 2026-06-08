#include <Arduino.h>
#include <EasyWiFi.h>
#include <time.h>
#include "LedClock.h"
#include "ClockPages.h"

// ===== Time Configuration =====
// The timezone is chosen in the web UI (see ClockPages) and stored in NVS.
// NTP servers used for the initial sync:
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

static const char* TAG = "VerticalClock";

// Non-blocking timer helper (see esp32-guidance: never delay() in loop()).
#define EVERY_N_MILLIS_ID(ID, N) \
    static uint32_t _timer_##ID = 0; \
    if (millis() - _timer_##ID >= (N) ? (_timer_##ID = millis(), true) : false)

// EasyWiFi run loop: owns station connect + AP-fallback web config portal.
RunLoop runloop;

// The 24-LED vertical strip, and the web page that configures it.
LedClock ledClock;
ClockPages clockPages(runloop.getConfigServer(), ledClock);

bool ntpConfigured = false;   // configTzTime() called once after first connect
bool haveValidTime = false;   // true once NTP has delivered a real time
float currentHourFloat = 0.0f; // local time as hours since midnight

void setup() {
    Serial.begin(115200);
    delay(500);  // give USB CDC time to initialize

    Serial.println("\n=== Vertical Clock (24h WS2812b) ===");

    ledClock.begin();  // FastLED init; strip starts dark

    // EasyWiFi handles WiFi.mode/begin and the captive config portal.
    runloop.setup("VerticalClock");

    // Load saved settings (timezone, scheme, brightness) and register "/".
    clockPages.begin();
    runloop.getConfigServer().registerCustomHandler(&clockPages);

    Serial.println("WiFi managed by EasyWiFi. If unconfigured, join the");
    Serial.println("'VerticalClock-Setup-xxxx' AP to enter credentials.");
    Serial.println("Open the device web page to pick a color scheme.");
}

void loop() {
    // Keep EasyWiFi's state machine (connect / AP portal / recovery) running.
    runloop.loop();

    // Once WiFi is up, configure NTP exactly once.
    if (!ntpConfigured && WiFi.status() == WL_CONNECTED) {
        String tz = clockPages.getTimezone();
        configTzTime(tz.c_str(), NTP_SERVER_1, NTP_SERVER_2);
        ntpConfigured = true;
        ESP_LOGI(TAG, "NTP configured (TZ=%s)", tz.c_str());
        Serial.println("Connected. Network time configured, waiting for sync...");
    }

    // Refresh local time once per second; the LEDs interpolate between updates.
    EVERY_N_MILLIS_ID(clock_poll, 1000) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > 120) {  // year > 2020
            currentHourFloat = timeinfo.tm_hour +
                               timeinfo.tm_min / 60.0f +
                               timeinfo.tm_sec / 3600.0f;
            if (!haveValidTime) {
                haveValidTime = true;
                Serial.println("Network time acquired. Clock is live.");
            }
            ESP_LOGD(TAG, "%02d:%02d:%02d (hour %.3f)",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     currentHourFloat);

            // Fire the scheduled party once when local time reaches the chosen
            // hour:minute. "Armed" prevents re-triggering every second within
            // that minute; it re-arms once the minute passes.
            static bool partyArmed = true;
            bool atPartyTime = clockPages.getPartyEnabled() &&
                               timeinfo.tm_hour == clockPages.getPartyHour() &&
                               timeinfo.tm_min == clockPages.getPartyMinute();
            if (atPartyTime) {
                if (partyArmed) {
                    ledClock.partyFor(60000);  // a full minute of dancing lights
                    partyArmed = false;
                    Serial.println("Party time! 🎉");
                }
            } else {
                partyArmed = true;
            }
        }
    }

    // Render an animation frame ~60fps for smooth shimmer and blending.
    EVERY_N_MILLIS_ID(frame, 16) {
        ledClock.render(currentHourFloat, haveValidTime);
    }

    yield();  // feed the watchdog, let background tasks run
}
