#include "ClockPages.h"
#include "ColorSchemes.h"
#include <time.h>

static const char* TAG = "ClockPages";

// NVS namespace/keys for persisted settings.
// (Names are prefixed to avoid colliding with EasyWiFi's NVS_* macros.)
static const char* CLOCK_NVS_NAMESPACE = "clock";
static const char* CLOCK_NVS_KEY_TZ = "tz";
static const char* CLOCK_NVS_KEY_SCHEME = "scheme";
static const char* CLOCK_NVS_KEY_BRI = "bri";
static const char* CLOCK_NVS_KEY_PARTY_EN = "party_en";
static const char* CLOCK_NVS_KEY_PARTY_H = "party_h";
static const char* CLOCK_NVS_KEY_PARTY_M = "party_m";

// Default timezone: US Pacific.
const char* DEFAULT_TIMEZONE = "PST8PDT,M3.2.0,M11.1.0";

// A short, friendly list of common zones. POSIX TZ strings carry their own DST
// rules, so the clock stays correct year-round.
const TimezoneOption TIMEZONE_OPTIONS[] = {
    {"Pacific (US)",        "PST8PDT,M3.2.0,M11.1.0"},
    {"Mountain (US)",       "MST7MDT,M3.2.0,M11.1.0"},
    {"Arizona (no DST)",    "MST7"},
    {"Central (US)",        "CST6CDT,M3.2.0,M11.1.0"},
    {"Eastern (US)",        "EST5EDT,M3.2.0,M11.1.0"},
    {"UK / Ireland",        "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"UTC",                 "UTC0"},
};
const size_t TIMEZONE_OPTION_COUNT = sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]);

void ClockPages::begin() {
    prefs.begin(CLOCK_NVS_NAMESPACE, /*readOnly=*/false);

    // Only read when a value exists; getString() on a missing key logs a
    // spurious [E] "NOT_FOUND" even though the default is returned fine.
    if (prefs.isKey(CLOCK_NVS_KEY_TZ)) {
        timezone = prefs.getString(CLOCK_NVS_KEY_TZ, DEFAULT_TIMEZONE);
    } else {
        timezone = DEFAULT_TIMEZONE;
    }
    schemeIndex = prefs.getUChar(CLOCK_NVS_KEY_SCHEME, 0);
    if (schemeIndex >= COLOR_SCHEME_COUNT) schemeIndex = 0;
    brightness = prefs.getUChar(CLOCK_NVS_KEY_BRI, 140);

    partyEnabled = prefs.getBool(CLOCK_NVS_KEY_PARTY_EN, false);
    partyHour = prefs.getUChar(CLOCK_NVS_KEY_PARTY_H, 18);
    if (partyHour > 23) partyHour = 18;
    partyMinute = prefs.getUChar(CLOCK_NVS_KEY_PARTY_M, 0);
    if (partyMinute > 59) partyMinute = 0;

    applyTimezone();
    applyToLeds();
    ESP_LOGI(TAG, "Loaded: tz=%s scheme=%u bri=%u party=%s %02u:%02u",
             timezone.c_str(), schemeIndex, brightness,
             partyEnabled ? "on" : "off", partyHour, partyMinute);
}

void ClockPages::applyTimezone() {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
}

void ClockPages::applyToLeds() {
    ledClock.setScheme(schemeIndex);
    ledClock.setBrightness(brightness);
}

void ClockPages::registerRoutes() {
    WebServer& server = configServer.getServer();
    server.on("/", HTTP_GET, std::bind(&ClockPages::handleRoot, this));
    server.on("/save", HTTP_POST, std::bind(&ClockPages::handleSave, this));
    server.on("/apply", HTTP_GET, std::bind(&ClockPages::handleApply, this));
    server.on("/all", HTTP_GET, std::bind(&ClockPages::handleAll, this));
    server.on("/party", HTTP_GET, std::bind(&ClockPages::handleParty, this));
    server.on("/state", HTTP_GET, std::bind(&ClockPages::handleState, this));
}

void ClockPages::handleRoot() {
    WebServer& server = configServer.getServer();

    String html = webPages->getHTMLHeader("Vertical Clock");
    html += "<h1>Vertical Clock</h1>";

    // Two columns: the vertical strip preview on the left (top = hour 24),
    // everything else on the right, so the page stays short.
    html += "<div style='display:flex;align-items:flex-start;gap:24px;flex-wrap:wrap;'>";

    // Left column: live preview of the physical strip.
    html += "<div id='strip' style='display:flex;flex-direction:column-reverse;"
            "gap:3px;padding:6px;background:#111;border-radius:8px;'></div>";

    // Right column: readout + controls + actions.
    html += "<div style='flex:1;min-width:280px;'>";

    html += "<div id='clock' style='font-size:40px;font-weight:bold;font-family:monospace;'>--:--:--</div>";
    html += "<div id='hint' style='font-size:16px;color:#888;margin-bottom:14px;'>waiting for time...</div>";

    html += "<form method='POST' action='/save'>";

    html += "<label>Color scheme</label>";
    html += "<select name='scheme' style='font-size:16px;padding:8px;width:100%;'>";
    for (uint8_t i = 0; i < COLOR_SCHEME_COUNT; i++) {
        html += "<option value='" + String(i) + "'";
        if (i == schemeIndex) html += " selected";
        html += ">" + String(COLOR_SCHEMES[i].name) + "</option>";
    }
    html += "</select>";

    html += "<label style='display:block;margin-top:12px;'>Brightness</label>";
    html += "<input type='range' name='bri' min='5' max='255' value='" +
            String(brightness) + "' style='width:100%;'>";

    html += "<label style='display:block;margin-top:12px;'>Timezone</label>";
    html += "<select name='tz' style='font-size:16px;padding:8px;width:100%;'>";
    for (size_t i = 0; i < TIMEZONE_OPTION_COUNT; i++) {
        const TimezoneOption& opt = TIMEZONE_OPTIONS[i];
        html += "<option value='" + String(opt.posix) + "'";
        if (timezone == opt.posix) html += " selected";
        html += ">" + String(opt.label) + "</option>";
    }
    html += "</select>";

    // Party time: a daily one-minute light show at the chosen 24-hour time.
    html += "<label style='display:block;margin-top:12px;'>"
            "<input type='checkbox' name='party_en'";
    if (partyEnabled) html += " checked";
    html += "> Party time</label>";
    html += "<div style='display:flex;gap:8px;align-items:center;'>";
    html += "<select name='party_h' style='font-size:16px;padding:8px;'>";
    for (uint8_t h = 0; h < 24; h++) {
        char b[3]; snprintf(b, sizeof(b), "%02u", h);
        html += "<option value='" + String(h) + "'";
        if (h == partyHour) html += " selected";
        html += ">" + String(b) + "</option>";
    }
    html += "</select><span>:</span>";
    html += "<select name='party_m' style='font-size:16px;padding:8px;'>";
    for (uint8_t m = 0; m < 60; m++) {
        char b[3]; snprintf(b, sizeof(b), "%02u", m);
        html += "<option value='" + String(m) + "'";
        if (m == partyMinute) html += " selected";
        html += ">" + String(b) + "</option>";
    }
    html += "</select></div>";

    // All actions share one row. Save submits the form; the others are
    // type=button so they run on the device without submitting.
    html += "<div style='display:flex;gap:8px;flex-wrap:wrap;margin-top:14px;'>";
    html += "<button type='submit' class='button primary'>Save</button>";
    html += "<button type='button' id='allbtn' class='button small'>Light all (60s)</button>";
    html += "<button type='button' id='partybtn' class='button small'>Party now (1 min)</button>";
    html += "</div>";

    html += "</form>";

    html += "<p style='margin-top:12px;'><a href='/wifi' class='button small'>WiFi Settings</a></p>";

    html += "</div>";   // end right column
    html += "</div>";   // end columns

    // Build the 24 preview cells once, then poll /state to recolor them and
    // update the clock readout in place.
    html += "<script>"
            "var strip=document.getElementById('strip');"
            "var cells=[];"
            "for(var i=0;i<24;i++){var d=document.createElement('div');"
            "d.style.width='42px';d.style.height='14px';d.style.borderRadius='3px';"
            "d.style.background='#000';strip.appendChild(d);cells.push(d);}"
            "function tick(){"
            "fetch('/state').then(function(r){return r.json();}).then(function(d){"
            "var c=document.getElementById('clock');var h=document.getElementById('hint');"
            "if(d.party){c.textContent=d.valid?d.time:'--:--:--';h.textContent='\\uD83C\\uDF89 party time!';h.style.color='#a0f';}"
            "else if(d.valid){c.textContent=d.time;h.textContent='hour '+d.hour+' of 24';h.style.color='#222';}"
            "else{h.textContent='waiting for network time...';h.style.color='#888';}"
            "if(d.leds){for(var i=0;i<24&&i<d.leds.length;i++){cells[i].style.background=d.leds[i];}}"
            "}).catch(function(){});"
            "}"
            // Apply scheme/brightness to the strip the instant they change, then
            // refresh the preview. Requests coalesce so dragging the slider stays
            // responsive and the final value always lands.
            "var schemeSel=document.querySelector('select[name=scheme]');"
            "var briInput=document.querySelector('input[name=bri]');"
            "var pending=false,inflight=false;"
            "function send(){if(!pending)return;pending=false;inflight=true;"
            "fetch('/apply?scheme='+schemeSel.value+'&bri='+briInput.value)"
            ".then(function(){inflight=false;tick();send();})"
            ".catch(function(){inflight=false;});}"
            "function applyLive(){pending=true;if(!inflight)send();}"
            "schemeSel.addEventListener('change',applyLive);"
            "briInput.addEventListener('input',applyLive);"
            // Action buttons: trigger on the device, then refresh quickly.
            "document.getElementById('allbtn').addEventListener('click',function(){"
            "fetch('/all').then(function(){tick();}).catch(function(){});});"
            "document.getElementById('partybtn').addEventListener('click',function(){"
            "fetch('/party').then(function(){tick();}).catch(function(){});});"
            "setInterval(tick,1000);tick();"
            "</script>";

    html += webPages->getHTMLFooter();

    server.send(200, "text/html", html);
}

void ClockPages::handleState() {
    WebServer& server = configServer.getServer();

    const char* party = ledClock.isPartying() ? "true" : "false";

    struct tm timeinfo;
    String json;
    if (getLocalTime(&timeinfo, 0) && timeinfo.tm_year > 120) {  // year > 2020
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        int displayHour = timeinfo.tm_hour + 1;  // 1..24, matches the strip
        json = String("{\"valid\":true,\"time\":\"") + buf +
               "\",\"hour\":" + displayHour +
               ",\"party\":" + party +
               ",\"leds\":" + ledClock.colorsJson() + "}";
    } else {
        json = String("{\"valid\":false,\"party\":") + party +
               ",\"leds\":" + ledClock.colorsJson() + "}";
    }

    server.send(200, "application/json", json);
}

void ClockPages::handleApply() {
    WebServer& server = configServer.getServer();

    // Apply live to the strip for instant feedback, but do NOT write to NVS:
    // this fires on every dropdown change and slider drag, and we don't want to
    // wear the flash. The choice only persists when the user clicks Save.
    if (server.hasArg("scheme")) {
        int s = server.arg("scheme").toInt();
        if (s < 0) s = 0;
        if (s >= COLOR_SCHEME_COUNT) s = COLOR_SCHEME_COUNT - 1;
        schemeIndex = (uint8_t)s;
    }
    if (server.hasArg("bri")) {
        int b = server.arg("bri").toInt();
        if (b < 5) b = 5;
        if (b > 255) b = 255;
        brightness = (uint8_t)b;
    }
    applyToLeds();

    server.send(200, "text/plain", "ok");
}

void ClockPages::handleAll() {
    // Light every LED at full (current scheme colors) for 60s, then the clock
    // resumes on its own. Nothing is persisted.
    ledClock.lightAllFor(60000);
    configServer.getServer().send(200, "text/plain", "ok");
}

void ClockPages::handleParty() {
    // Kick off a one-minute party right now (manual test of the show).
    ledClock.partyFor(60000);
    configServer.getServer().send(200, "text/plain", "ok");
}

void ClockPages::handleSave() {
    WebServer& server = configServer.getServer();

    // Timezone: accept only values from our known list.
    String tz = server.arg("tz");
    for (size_t i = 0; i < TIMEZONE_OPTION_COUNT; i++) {
        if (tz == TIMEZONE_OPTIONS[i].posix) {
            timezone = tz;
            prefs.putString(CLOCK_NVS_KEY_TZ, timezone);
            applyTimezone();
            break;
        }
    }

    // Scheme: clamp to a valid index.
    if (server.hasArg("scheme")) {
        int s = server.arg("scheme").toInt();
        if (s < 0) s = 0;
        if (s >= COLOR_SCHEME_COUNT) s = COLOR_SCHEME_COUNT - 1;
        schemeIndex = (uint8_t)s;
        prefs.putUChar(CLOCK_NVS_KEY_SCHEME, schemeIndex);
    }

    // Brightness: clamp to a visible range.
    if (server.hasArg("bri")) {
        int b = server.arg("bri").toInt();
        if (b < 5) b = 5;
        if (b > 255) b = 255;
        brightness = (uint8_t)b;
        prefs.putUChar(CLOCK_NVS_KEY_BRI, brightness);
    }

    // Party time. An unchecked checkbox sends no arg at all, so its presence is
    // the enabled flag. The hour/minute selects always submit a value.
    partyEnabled = server.hasArg("party_en");
    prefs.putBool(CLOCK_NVS_KEY_PARTY_EN, partyEnabled);
    if (server.hasArg("party_h")) {
        int h = server.arg("party_h").toInt();
        if (h < 0) h = 0;
        if (h > 23) h = 23;
        partyHour = (uint8_t)h;
        prefs.putUChar(CLOCK_NVS_KEY_PARTY_H, partyHour);
    }
    if (server.hasArg("party_m")) {
        int m = server.arg("party_m").toInt();
        if (m < 0) m = 0;
        if (m > 59) m = 59;
        partyMinute = (uint8_t)m;
        prefs.putUChar(CLOCK_NVS_KEY_PARTY_M, partyMinute);
    }

    applyToLeds();
    ESP_LOGI(TAG, "Saved: tz=%s scheme=%u bri=%u party=%s %02u:%02u",
             timezone.c_str(), schemeIndex, brightness,
             partyEnabled ? "on" : "off", partyHour, partyMinute);

    // Redirect back to the form (303 -> GET).
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Saved");
}
