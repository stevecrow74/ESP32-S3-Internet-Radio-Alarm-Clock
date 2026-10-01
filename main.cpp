
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Audio.h>

// ============================================================
// ESP32-S3 RADIO ALARM CLOCK
// ============================================================

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  8

#define TOUCH_PREVIOUS  2
#define TOUCH_NEXT      6
#define TOUCH_VOL_DOWN  4
#define TOUCH_VOL_UP    5
#define TOUCH_THRESHOLD_PERCENT 125

#define BUTTON_PIN 1

#define I2S_BCLK 16
#define I2S_LRC  15
#define I2S_DOUT 17

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
Audio audio;
WebServer webServer(80);
Preferences prefs;

// ============================================================
// CONFIG
// ============================================================

const char *WIFI_SSID = "Your SSID";
const char *WIFI_PASSWORD = "Your WIFI PASSWORD";
const char *NTP_SERVER = "pool.ntp.org";
const char *TZ_INFO = "GMT0IST,M3.5.0/1,M10.5.0/2";

const uint16_t COLOR_BACKGROUND = ST77XX_BLACK;
const uint16_t COLOR_TEXT = ST77XX_WHITE;
const uint16_t COLOR_ACCENT = 0x05B9;
const uint16_t COLOR_MUTED = 0x7BEF;
const uint16_t COLOR_HEADER = 0x11A6;
const uint16_t COLOR_PANEL = 0x18E3;
const uint16_t COLOR_EDGE = 0x2A89;

enum Page {
    PAGE_CLOCK,
    PAGE_NOW_PLAYING,
    PAGE_ALARM_SETUP
};

Page currentPage = PAGE_CLOCK;

const unsigned long RESET_CHORD_HOLD_MS = 1000UL;
const unsigned long RESET_RESPONSE_DELAY_MS = 500UL;
bool deviceResetPending = false;
unsigned long deviceResetRequestedAt = 0;

// Forward declarations
// Forward declarations
void drawAlarmPage(bool full);
void drawClockPage(bool full);
void drawNowPlaying(bool full);
void drawCurrentPage(bool full);
void drawVolumeControl();
void saveAlarmSettings();
void stopAlarm();
void connectStation(int index);

// ============================================================
// STATIONS
// ============================================================

struct RadioStation {
    const char *name;
    const char *url;
};

RadioStation stations[] = {
    {"Bob FM", "http://sirius.shoutca.st:8011/stream"},
    {"Classic Hits", "http://www.radiofeeds.net/playlists/audioxi.pls?station=CLASSIC"},
    {"Darkwave ", "https://radio.webhosting4u.gr/stream/darkwaveradio"},
    {"Mellow", "https://stream.radioparadise.com/mellow-320"},
    {"Nova 80's", "https://25503.live.streamtheworld.com/NOVA_80S.mp3"},
    {"Nova Rock", "https://25703.live.streamtheworld.com/NOVA_CLASSIC_ROCK.mp3"},
    {"Onic 80's", "http://onic.dublin.live.stream.broadcasting.news/stream-80s?ref=RF"},
    {"Onic Alt", "http://onic.dublin.live.stream.broadcasting.news/stream-alternative-mobile?ref=RF"},
    {"Radio Nova", "https://25703.live.streamtheworld.com/RADIONOVA.mp3"},
    {"Paradise Rock", "https://stream.radioparadise.com/rock-320"},
    {"Radio X", "http://media-ice.musicradio.com/RadioXLondonMP3"},
    {"Antenne Alt", "https://stream.rockantenne.de/alternative"},
    {"Antenne Rock", "https://stream.rockantenne.de/classic-perlen"},
    {"Soma 70's", "https://ice5.somafm.com/seventies-128-mp3"},
    {"Soma Indie", "https://ice6.somafm.com/indiepop-128-mp3"},
    {"Underground 80's", "https://ice6.somafm.com/u80s-64-aac"},
    {"Velvet", "http://stream.btsstream.com:8012/velvet.mp3"},
    {"Zenith Rock", "http://91.189.64.188:3644/zenith128mp3"}
};

const int stationCount = sizeof(stations) / sizeof(stations[0]);

int currentStation = 10; 
String currentStationName = "Radio X";

// ============================================================
// VOLUME / ALARM
// ============================================================

int volumeLevel = 0;
const int VOLUME_MIN = 0;
const int VOLUME_MAX = 7;
const int ALARM_VOLUME = 1;

int alarmRestoreVolume = 2;
bool alarmVolumeBoosted = false;

// Alarm ramps one step every ALARM_RAMP_INTERVAL ms
// while ringing, up to VOLUME_MAX.
const unsigned long ALARM_RAMP_INTERVAL = 10UL * 1000UL;
unsigned long lastAlarmRampMillis = 0;

int alarmHour = 7;
int alarmMinute = 45;
bool alarmEnabled = false;
bool alarmPlaying = false;

int alarmPrimaryStation = 0;
int alarmFallbackStation = 1;

const unsigned long ALARM_MAX_DURATION = 30UL * 60UL * 1000UL;
unsigned long alarmStartMillis = 0;

int alarmTriggeredYear = -1;
int alarmTriggeredDay = -1;
bool alarmFallbackUsed = false;

// ============================================================
// DISPLAY / METADATA
// ============================================================

String lastClockText = "";
String lastDateText = "";
unsigned long lastClockUpdate = 0;

String currentStationText = "";
String currentSongText = "";
String currentStatusText = "Connecting...";
bool metadataChanged = false;

// ============================================================
// TOUCH
// ============================================================

uint16_t touchBasePrevious;
uint16_t touchBaseNext;
uint16_t touchBaseVolDown;
uint16_t touchBaseVolUp;

bool touchStatePrevious = false;
bool touchStateNext = false;
bool touchStateVolDown = false;
bool touchStateVolUp = false;

// ============================================================
// MARQUEE
// ============================================================

String marqueeArtistText = "";
String marqueeSongText = "";

int artistScrollX = 0;
int songScrollX = 0;

bool artistScrolling = false;
bool songScrolling = false;

bool artistMarqueeFinished = false;
bool songMarqueeFinished = false;

int artistPassCount = 0;
int songPassCount = 0;

unsigned long artistPauseUntil = 0;
unsigned long songPauseUntil = 0;
unsigned long lastMarqueeUpdate = 0;

const unsigned long MARQUEE_INTERVAL = 70;
const unsigned long MARQUEE_START_PAUSE = 1000;
const unsigned long MARQUEE_END_PAUSE = 700;

const int MARQUEE_LEFT = 7;
const int MARQUEE_RIGHT = 313;
const int MARQUEE_WIDTH = 306;
const int MARQUEE_MAX_PASSES = 2;

// ============================================================
// ALARM BITMAP - DO NOT ALTER
// ============================================================

const uint8_t alarmIconBitmap[] PROGMEM = {
    0x00,0x00,0x00,0x00,0x0F,0xE0,0x07,0xF0,0x1C,0xE0,0x07,0x78,0x39,0xE0,0x07,0x3C,
    0x73,0xFF,0xFF,0x9E,0x67,0xFF,0xFF,0xCE,0x6F,0xF0,0x0F,0xE6,0x7F,0xC0,0x03,0xF6,
    0x7F,0x80,0x01,0xFE,0x7F,0x00,0x00,0xFE,0x7E,0x00,0x00,0x7E,0x1C,0x00,0x00,0x38,
    0x18,0x00,0x00,0x18,0x38,0x00,0x00,0x1C,0x38,0x00,0x00,0x1C,0x38,0x00,0x00,0x1C,
    0x3B,0xFF,0x80,0x1C,0x3B,0xFF,0xC0,0x1C,0x38,0x00,0xC0,0x1C,0x38,0x01,0xC0,0x1C,
    0x38,0x03,0x80,0x1C,0x38,0x07,0x00,0x1C,0x1C,0x0E,0x00,0x38,0x0E,0x0C,0x00,0x70,
    0x0E,0x00,0x00,0x70,0x0F,0x00,0x00,0xF0,0x07,0x80,0x01,0xE0,0x0F,0xE0,0x07,0xF0,
    0x0F,0xF0,0x0F,0xF0,0x1E,0x7F,0xFE,0x78,0x18,0x1F,0xF8,0x18,0x00,0x00,0x00,0x00
};

// ============================================================
// HELPERS
// ============================================================

String getTimeString()
{
    struct tm t;

    if (!getLocalTime(&t))
        return "--:--";

    char b[12];

    strftime(
        b,
        sizeof(b),
        "%H:%M",
        &t
    );

    return String(b);
}

String getDateString()
{
    struct tm t;

    if (!getLocalTime(&t))
        return "Waiting for time";

    char b[40];

    strftime(
        b,
        sizeof(b),
        "%A %d %B",
        &t
    );

    return String(b);
}

String alarmTimeString()
{
    char b[8];

    snprintf(
        b,
        sizeof(b),
        "%02d:%02d",
        alarmHour,
        alarmMinute
    );

    return String(b);
}

String jsonEscape(const String &s)
{
    String o;

    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];

        if (c == '"')
            o += "\\\"";
        else if (c == '\\')
            o += "\\\\";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\r')
            o += "\\r";
        else
            o += c;
    }

    return o;
}

// ============================================================
// PREFERENCES
// ============================================================

void loadAlarmSettings()
{
    prefs.begin("alarm", false);

    alarmHour =
        prefs.getInt("hour", 7);

    alarmMinute =
        prefs.getInt("minute", 45);

    alarmEnabled =
        prefs.getBool("enabled", false);

    alarmPrimaryStation =
        prefs.getInt("primary", 6);

    alarmFallbackStation =
        prefs.getInt("fallback", 7);

    if (alarmHour < 0 || alarmHour > 23)
        alarmHour = 7;

    if (alarmMinute < 0 || alarmMinute > 59)
        alarmMinute = 45;

    if (
        alarmPrimaryStation < 0 ||
        alarmPrimaryStation >= stationCount
    )
        alarmPrimaryStation = 0;

    if (
        alarmFallbackStation < 0 ||
        alarmFallbackStation >= stationCount
    )
        alarmFallbackStation = 1;
}

void saveAlarmSettings()
{
    prefs.putInt(
        "hour",
        alarmHour
    );

    prefs.putInt(
        "minute",
        alarmMinute
    );

    prefs.putBool(
        "enabled",
        alarmEnabled
    );

    prefs.putInt(
        "primary",
        alarmPrimaryStation
    );

    prefs.putInt(
        "fallback",
        alarmFallbackStation
    );
}

// ============================================================
// WIFI ICON
// ============================================================

void drawWiFiIcon(bool connected)
{
    tft.fillRect(
        274,
        4,
        38,
        30,
        COLOR_HEADER
    );

    uint16_t c =
        connected ?
        COLOR_ACCENT :
        COLOR_MUTED;

    tft.drawLine(282,13,285,10,c);
    tft.drawLine(285,10,289,8,c);
    tft.drawLine(289,8,293,7,c);
    tft.drawLine(293,7,297,8,c);
    tft.drawLine(297,8,301,10,c);
    tft.drawLine(301,10,304,13,c);

    tft.drawLine(286,17,289,14,c);
    tft.drawLine(289,14,293,13,c);
    tft.drawLine(293,13,297,14,c);
    tft.drawLine(297,14,300,17,c);

    tft.drawLine(290,20,292,18,c);
    tft.drawLine(292,18,294,18,c);
    tft.drawLine(294,18,296,20,c);

    tft.fillCircle(
        293,
        24,
        2,
        c
    );
}

// ============================================================
// RADIO
// ============================================================

void connectStation(int index)
{
    if (
        index < 0 ||
        index >= stationCount
    )
        return;

    currentStation = index;

    currentStationName =
        stations[index].name;

    currentStationText =
        currentStationName;

    currentSongText = "";
    currentStatusText = "Connecting...";
    metadataChanged = true;

    marqueeArtistText = "";
    marqueeSongText = "";

    artistScrolling = false;
    songScrolling = false;

    artistMarqueeFinished = false;
    songMarqueeFinished = false;

    audio.stopSong();

    delay(1500);

    audio.setVolume(
        volumeLevel
    );

    audio.connecttohost(
        stations[index].url
    );

    Serial.print("Station: ");
    Serial.println(
        stations[index].name
    );
}

void nextStation()
{
    connectStation(
        (currentStation + 1) %
        stationCount
    );
}

void previousStation()
{
    connectStation(
        (currentStation - 1 + stationCount) %
        stationCount
    );
}

// ============================================================
// AUDIO CALLBACKS
// ============================================================

void audio_info(const char *info)
{
    if (!info)
        return;

    Serial.print("AUDIO: ");
    Serial.println(info);

    currentStatusText =
        String(info);

    metadataChanged = true;

    if (
        alarmPlaying &&
        !alarmFallbackUsed
    )
    {
        String msg =
            String(info);

        msg.toLowerCase();

        if (
            msg.indexOf("error") >= 0 ||
            msg.indexOf("failed") >= 0 ||
            msg.indexOf("fail") >= 0
        )
        {
            Serial.println(
                "Alarm audio error - switching to fallback"
            );

            alarmFallbackUsed = true;

            connectStation(
                alarmFallbackStation
            );
        }
    }
}

void audio_showstation(const char *info)
{
    if (
        info &&
        strlen(info)
    )
    {
        currentStationText =
            String(info);

        metadataChanged = true;
    }
}

void audio_showstreamtitle(const char *info)
{
    if (
        info &&
        strlen(info)
    )
    {
        currentSongText =
            String(info);

        metadataChanged = true;

        Serial.print("TITLE: ");
        Serial.println(info);
    }
}

// ============================================================
// MARQUEE
// ============================================================

void prepareArtistMarquee()
{
    String text =
        currentSongText;

    int s =
        text.indexOf(" - ");

    String artist =
        s > 0 ?
        text.substring(0, s) :
        text;

    if (
        artist !=
        marqueeArtistText
    )
    {
        marqueeArtistText =
            artist;

        artistScrollX =
            MARQUEE_RIGHT;

        artistScrolling =
            artist.length() > 25;

        artistMarqueeFinished =
            false;

        artistPassCount = 0;

        artistPauseUntil =
            millis() +
            MARQUEE_START_PAUSE;
    }
}

void prepareSongMarquee()
{
    String text =
        currentSongText;

    int s =
        text.indexOf(" - ");

    String song =
        s > 0 ?
        text.substring(s + 3) :
        text;

    if (
        song !=
        marqueeSongText
    )
    {
        marqueeSongText =
            song;

        songScrollX =
            MARQUEE_RIGHT;

        songScrolling =
            song.length() > 25;

        songMarqueeFinished =
            false;

        songPassCount = 0;

        songPauseUntil =
            millis() +
            MARQUEE_START_PAUSE;
    }
}

bool updateMarquee()
{
    prepareArtistMarquee();
    prepareSongMarquee();

    if (
        millis() -
        lastMarqueeUpdate <
        MARQUEE_INTERVAL
    )
        return false;

    lastMarqueeUpdate =
        millis();

    bool moved = false;

    if (
        artistScrolling &&
        !artistMarqueeFinished &&
        millis() >= artistPauseUntil
    )
    {
        artistScrollX -= 2;
        moved = true;

        int w =
            marqueeArtistText.length() *
            6;

        if (
            artistScrollX <
            MARQUEE_LEFT - w
        )
        {
            artistPassCount++;

            if (
                artistPassCount >=
                MARQUEE_MAX_PASSES
            )
            {
                artistScrolling = false;
                artistMarqueeFinished = true;

                artistScrollX =
                    MARQUEE_LEFT +
                    (MARQUEE_WIDTH - w) / 2;

                if (
                    artistScrollX <
                    MARQUEE_LEFT
                )
                    artistScrollX =
                        MARQUEE_LEFT;
            }
            else
            {
                artistScrollX =
                    MARQUEE_RIGHT;

                artistPauseUntil =
                    millis() +
                    MARQUEE_END_PAUSE;
            }
        }
    }

    if (
        songScrolling &&
        !songMarqueeFinished &&
        millis() >= songPauseUntil
    )
    {
        songScrollX -= 2;
        moved = true;

        int w =
            marqueeSongText.length() *
            6;

        if (
            songScrollX <
            MARQUEE_LEFT - w
        )
        {
            songPassCount++;

            if (
                songPassCount >=
                MARQUEE_MAX_PASSES
            )
            {
                songScrolling = false;
                songMarqueeFinished = true;

                songScrollX =
                    MARQUEE_LEFT +
                    (MARQUEE_WIDTH - w) / 2;

                if (
                    songScrollX <
                    MARQUEE_LEFT
                )
                    songScrollX =
                        MARQUEE_LEFT;
            }
            else
            {
                songScrollX =
                    MARQUEE_RIGHT;

                songPauseUntil =
                    millis() +
                    MARQUEE_END_PAUSE;
            }
        }
    }

    return moved;
}

// ============================================================
// ALARM
// ============================================================

void startAlarm()
{
    if (alarmPlaying)
        return;

    alarmPlaying = true;

    alarmStartMillis =
        millis();

    alarmFallbackUsed = false;

    alarmRestoreVolume =
        volumeLevel;

    // Start at the ALARM_VOLUME step (or the
    // current level if already above it) and
    // climb from there one step at a time.
    volumeLevel =
        max(
            ALARM_VOLUME,
            min(
                alarmRestoreVolume,
                VOLUME_MAX
            )
        );

    if (
        volumeLevel <
        ALARM_VOLUME
    )
        volumeLevel =
            ALARM_VOLUME;

    audio.setVolume(
        volumeLevel
    );

    alarmVolumeBoosted = true;

    lastAlarmRampMillis =
        millis();

    Serial.println(
        "================================"
    );

    Serial.println(
        "ALARM STARTED"
    );

    Serial.print(
        "Alarm volume: "
    );

    Serial.println(
        volumeLevel
    );

    Serial.println(
        "================================"
    );

    connectStation(
        alarmPrimaryStation
    );
}

void stopAlarm()
{
    alarmPlaying = false;

    audio.stopSong();

    if (alarmVolumeBoosted)
    {
        volumeLevel =
            alarmRestoreVolume;

        audio.setVolume(
            volumeLevel
        );

        alarmVolumeBoosted = false;
    }

    // The alarm stays armed and will fire again
    // tomorrow - only the ringing is stopped.
    alarmFallbackUsed = false;

    currentStatusText =
        "Alarm stopped";

    metadataChanged = true;

    Serial.println(
        "ALARM STOPPED"
    );
}

void checkAlarm()
{
    struct tm t;

    if (
        !getLocalTime(&t) ||
        !alarmEnabled
    )
        return;

    if (alarmPlaying)
    {
        if (
            millis() -
            alarmStartMillis >=
            ALARM_MAX_DURATION
        )
        {
            stopAlarm();
        }

        // Gradually increase the ringing volume
        // by one step every 10 seconds.
        if (
            volumeLevel <
                VOLUME_MAX &&
            millis() -
                lastAlarmRampMillis >=
            ALARM_RAMP_INTERVAL
        )
        {
            lastAlarmRampMillis =
                millis();

            volumeLevel++;

            audio.setVolume(
                volumeLevel
            );

            Serial.print(
                "Alarm volume ramped to: "
            );

            Serial.println(
                volumeLevel
            );

            if (
                currentPage ==
                PAGE_NOW_PLAYING
            )
            {
                drawVolumeControl();
            }
        }

        return;
    }

    if (
        t.tm_hour != alarmHour ||
        t.tm_min != alarmMinute
    )
        return;

    if (
        alarmTriggeredYear ==
            t.tm_year &&
        alarmTriggeredDay ==
            t.tm_yday
    )
        return;

    alarmTriggeredYear =
        t.tm_year;

    alarmTriggeredDay =
        t.tm_yday;

    startAlarm();
}
// ============================================================
// TOUCH
// ============================================================

uint16_t readTouchBase(uint8_t pin)
{
    uint32_t total = 0;

    for (int i = 0; i < 20; i++)
    {
        total += touchRead(pin);
        delay(5);
    }

    return total / 20;
}

bool touchPressed(
    uint8_t pin,
    uint16_t baseline,
    bool &state
)
{
    uint16_t threshold =
        baseline *
        TOUCH_THRESHOLD_PERCENT /
        100;

    bool pressed =
        touchRead(pin) >
        threshold;

    bool event =
        pressed &&
        !state;

    state = pressed;

    return event;
}
void calibrateTouch()
{
    Serial.println(
        "Calibrating touch..."
    );

    touchBasePrevious =
        readTouchBase(
            TOUCH_PREVIOUS
        );

    touchBaseNext =
        readTouchBase(
            TOUCH_NEXT
        );

    touchBaseVolDown =
        readTouchBase(
            TOUCH_VOL_DOWN
        );

    touchBaseVolUp =
        readTouchBase(
            TOUCH_VOL_UP
        );

    Serial.printf(
        "PREV: %u\nNEXT: %u\nVOL DOWN: %u\nVOL UP: %u\n",
        touchBasePrevious,
        touchBaseNext,
        touchBaseVolDown,
        touchBaseVolUp
    );
}

void handleAlarmTouch()
{
    bool p =
        touchPressed(
            TOUCH_PREVIOUS,
            touchBasePrevious,
            touchStatePrevious
        );

    bool n =
        touchPressed(
            TOUCH_NEXT,
            touchBaseNext,
            touchStateNext
        );

    bool d =
        touchPressed(
            TOUCH_VOL_DOWN,
            touchBaseVolDown,
            touchStateVolDown
        );

    bool u =
        touchPressed(
            TOUCH_VOL_UP,
            touchBaseVolUp,
            touchStateVolUp
        );

    if (p)
    {
        alarmHour =
            (alarmHour + 23) % 24;

        saveAlarmSettings();
        drawAlarmPage(false);
    }

    if (n)
    {
        alarmHour =
            (alarmHour + 1) % 24;

        saveAlarmSettings();
        drawAlarmPage(false);
    }

    if (
        d &&
        digitalRead(BUTTON_PIN) != LOW
    )
    {
        alarmMinute =
            (alarmMinute + 59) % 60;

        saveAlarmSettings();
        drawAlarmPage(false);
    }

    if (u)
    {
        alarmMinute =
            (alarmMinute + 1) % 60;

        saveAlarmSettings();
        drawAlarmPage(false);
    }
}

void handleTouch()
{
    if (
        currentPage ==
        PAGE_ALARM_SETUP
    )
    {
        handleAlarmTouch();
        return;
    }

    bool p =
        touchPressed(
            TOUCH_PREVIOUS,
            touchBasePrevious,
            touchStatePrevious
        );

    bool n =
        touchPressed(
            TOUCH_NEXT,
            touchBaseNext,
            touchStateNext
        );

    bool d =
        touchPressed(
            TOUCH_VOL_DOWN,
            touchBaseVolDown,
            touchStateVolDown
        );

    bool u =
        touchPressed(
            TOUCH_VOL_UP,
            touchBaseVolUp,
            touchStateVolUp
        );

    if (p)
        previousStation();

    if (n)
        nextStation();

    // --------------------------------------------------------
    // VOLUME DOWN
    // --------------------------------------------------------

    if (
        d &&
        digitalRead(BUTTON_PIN) != LOW
    )
    {
        volumeLevel =
            max(
                VOLUME_MIN,
                volumeLevel - 1
            );

        if (!alarmVolumeBoosted)
        {
            audio.setVolume(
                volumeLevel
            );
        }

        if (
            currentPage ==
            PAGE_NOW_PLAYING
        )
        {
            drawVolumeControl();
        }
    }

    // --------------------------------------------------------
    // VOLUME UP
    // --------------------------------------------------------

    if (u)
    {
        volumeLevel =
            min(
                VOLUME_MAX,
                volumeLevel + 1
            );

        if (!alarmVolumeBoosted)
        {
            audio.setVolume(
                volumeLevel
            );
        }

        if (
            currentPage ==
            PAGE_NOW_PLAYING
        )
        {
            drawVolumeControl();
        }
    }
}

// ============================================================
// ALARM PAGE
// ============================================================

void drawAlarmPage(bool full)
{
    if (full)
        tft.fillScreen(
            COLOR_BACKGROUND
        );

    tft.fillRect(
        0,
        0,
        320,
        38,
        COLOR_HEADER
    );

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.setTextSize(2);

    tft.setCursor(
        10,
        10
    );

    tft.print(
        "ALARM SETTINGS"
    );

    tft.setTextSize(1);

    tft.setCursor(
        245,
        15
    );

    tft.setTextColor(
        alarmEnabled ?
        COLOR_ACCENT :
        COLOR_MUTED
    );

    tft.print(
        alarmEnabled ?
        "ON" :
        "OFF"
    );

    tft.fillRect(
        40,
        52,
        240,
        55,
        COLOR_BACKGROUND
    );

    tft.setTextSize(4);

    tft.setTextColor(
        COLOR_TEXT
    );

    String tm =
        alarmTimeString();

    tft.setCursor(
        (320 - tm.length() * 24) / 2,
        60
    );

    tft.print(tm);

    tft.setTextSize(2);

    const int x[] = {
        15,
        85,
        170,
        250
    };

    const char *label[] = {
        "-H",
        "+H",
        "+M",
        "-M"
    };

    for (int i = 0; i < 4; i++)
    {
        tft.drawRect(
            x[i],
            120,
            65,
            45,
            COLOR_EDGE
        );

        tft.setCursor(
            x[i] + 16,
            135
        );

        tft.print(
            label[i]
        );
    }

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        10,
        180
    );

    tft.print(
        "Primary: "
    );

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.print(
        stations[
            alarmPrimaryStation
        ].name
    );

    tft.setCursor(
        10,
        195
    );

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.print(
        "Fallback: "
    );

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.print(
        stations[
            alarmFallbackStation
        ].name
    );

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        10,
        218
    );

    tft.print(
        "Touch: -H / +H / -M / +M"
    );

    tft.setCursor(
        225,
        218
    );

    tft.print(
        "BUTTON = SAVE"
    );
}

// ============================================================
// CLOCK PAGE
// ============================================================

void drawClockPage(bool full)
{
    if (full)
        tft.fillScreen(
            COLOR_BACKGROUND
        );

    tft.fillRect(
        0,
        0,
        320,
        38,
        COLOR_HEADER
    );

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.setTextSize(2);

    tft.setCursor(
        60,
        10
    );

    tft.print(
        "RADIO ALARM CLOCK"
    );

    drawWiFiIcon(
        WiFi.status() ==
        WL_CONNECTED
    );

    String now =
        getTimeString();

    if (
        full ||
        now != lastClockText
    )
    {
        tft.fillRect(
            0,
            43,
            320,
            63,
            COLOR_BACKGROUND
        );

        tft.setTextColor(
            COLOR_TEXT
        );

        tft.setTextSize(6);

        tft.setCursor(
            (320 - now.length() * 36) / 2,
            47
        );

        tft.print(now);

        lastClockText =
            now;
    }

    String date =
        getDateString();

    if (
        full ||
        date != lastDateText
    )
    {
        tft.fillRect(
            0,
            108,
            320,
            22,
            COLOR_BACKGROUND
        );

        tft.setTextSize(2);

        tft.setTextColor(
            COLOR_MUTED
        );

        tft.setCursor(
            (320 - date.length() * 12) / 2,
            110
        );

        tft.print(date);

        lastDateText =
            date;
    }

    tft.drawRoundRect(
        8,
        140,
        304,
        65,
        5,
        COLOR_EDGE
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        18,
        148
    );

    tft.print(
        "ALARM"
    );
    
        tft.fillRect(
            30,
            160,
            200,
            30,
            COLOR_BACKGROUND
        );
    String at =
        alarmTimeString();


        
    tft.setTextSize(3);

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.setCursor(
        110,
        164
    );

    tft.print(at);

    tft.drawBitmap(
        255,
        165,
        alarmIconBitmap,
        32,
        32,
        alarmEnabled ?
        COLOR_ACCENT :
        COLOR_MUTED
    );

    tft.fillRect(
        8,
        210,
        304,
        18,
        COLOR_BACKGROUND
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_TEXT
    );
    tft.setCursor(
            (320 - currentStationName.length() * 6) / 2,
            212
    
    );

    tft.print(
        currentStationName
    );

    tft.fillRect(
        0,
        230,
        320,
        10,
        COLOR_BACKGROUND
    );

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        20,
        231
    );

    tft.print(
        "short: Radio      hold 3s: Alarm"
    );
}

// ============================================================
// NOW PLAYING
// ============================================================
void drawVolumeControl()
{
    // Clear volume bar
    tft.fillRect(
        15,
        185,
        290,
        22,
        COLOR_BACKGROUND
    );

    // Border
    tft.drawRect(
        15,
        188,
        290,
        14,
        COLOR_EDGE
    );

    // Filled amount
    int bw =
        map(
            volumeLevel,
            VOLUME_MIN,
            VOLUME_MAX,
            0,
            286
        );

    if (bw > 0)
    {
        tft.fillRect(
            17,
            190,
            bw,
            10,
            COLOR_ACCENT
        );
    }

    // Volume text
    tft.fillRect(
        120,
        214,
        100,
        16,
        COLOR_BACKGROUND
    );

    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    tft.setCursor(
        145,
        218
    );

    tft.print("VOLUME ");
    tft.print(volumeLevel);
}

void drawNowPlaying(bool full)
{
    if (full)
        tft.fillScreen(
            COLOR_BACKGROUND
        );

    tft.fillRect(
        0,
        0,
        320,
        34,
        COLOR_HEADER
    );

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.setTextSize(2);

    // "NOW PLAYING" is 11 chars, 12px each at
    // text size 2, so centre it on the 320px
    // wide header.
    const char *title =
        "NOW PLAYING";

    int titleW =
        strlen(title) * 12;

    tft.setCursor(
        (320 - titleW) / 2,
        6
    );

    tft.print(
        title
    );

    drawWiFiIcon(
        WiFi.status() ==
        WL_CONNECTED
    );

    int prev =
        (currentStation - 1 +
         stationCount) %
        stationCount;

    int next =
        (currentStation + 1) %
        stationCount;

    tft.fillRect(
        0,
        38,
        320,
        45,
        COLOR_BACKGROUND
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        8,
        55
    );

    tft.print(
        stations[prev].name
    );

    tft.setTextColor(
        COLOR_ACCENT
    );

    tft.setTextSize(2);

    int cw =
        strlen(
            stations[
                currentStation
            ].name
        ) * 12;

    tft.setCursor(
        (320 - cw) / 2,
        50
    );

    tft.print(
        stations[
            currentStation
        ].name
    );

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_MUTED
    );

    int nw =
        strlen(
            stations[next].name
        ) * 6;

    tft.setCursor(
        312 - nw,
        55
    );

    tft.print(
        stations[next].name
    );

tft.fillRect(
    0,
    91,
    320,
    25,
    COLOR_BACKGROUND
);

tft.setTextColor(
    COLOR_TEXT
);

tft.setTextSize(2);

String artist =
    marqueeArtistText.length() ?
    marqueeArtistText :
    currentStationText;

int aw =
    artist.length() * 12;

int ax =
    artistScrolling ?
    artistScrollX :
    (320 - aw) / 2;

if (ax < 7)
    ax = 7;

tft.setCursor(
    ax,
    96
);

tft.print(
    artist
);

tft.fillRect(
    0,
    118,
    320,
    25,
    COLOR_BACKGROUND
);

String song =
    marqueeSongText.length() ?
    marqueeSongText :
    currentSongText;

int sw =
    song.length() * 12;

int sx =
    songScrolling ?
    songScrollX :
    (320 - sw) / 2;

if (sx < 7)
    sx = 7;

tft.setCursor(
    sx,
    123
);

tft.print(
    song
);

tft.fillRect(
    0,
    150,
    320,
    22,
    COLOR_BACKGROUND
);

tft.setTextSize(1);


    tft.fillRect(
        15,
        185,
        290,
        22,
        COLOR_BACKGROUND
    );

    tft.drawRect(
        15,
        188,
        290,
        14,
        COLOR_EDGE
    );

    int bw =
        map(
            volumeLevel,
            VOLUME_MIN,
            VOLUME_MAX,
            0,
            286
        );

    if (bw > 0)
    {
        tft.fillRect(
            17,
            190,
            bw,
            10,
            COLOR_ACCENT
        );
    }

    tft.setTextSize(1);

    tft.setTextColor(
        COLOR_TEXT
    );

    tft.setCursor(
        145,
        218
    );

    tft.print(
        "VOLUME "
    );

    tft.print(
        volumeLevel
    );

    tft.setTextColor(
        COLOR_MUTED
    );

    tft.setCursor(
        8,
        232
    );

    tft.print(

        "+ Station -                           + Volume -"
    );
}

// ============================================================
// PAGE CONTROL
// ============================================================

void drawCurrentPage(bool full)
{
    if (
        currentPage ==
        PAGE_CLOCK
    )
    {
        drawClockPage(full);
    }
    else if (
        currentPage ==
        PAGE_NOW_PLAYING
    )
    {
        drawNowPlaying(full);
    }
    else
    {
        drawAlarmPage(full);
    }
}

void requestDeviceReset()
{
    if (deviceResetPending)
        return;

    deviceResetPending = true;
    deviceResetRequestedAt = millis();
}

void processDeviceReset()
{
    if (
        deviceResetPending &&
        millis() - deviceResetRequestedAt >=
            RESET_RESPONSE_DELAY_MS
    )
    {
        ESP.restart();
    }
}

// ============================================================
// PHYSICAL BUTTON
// ============================================================

void handlePhysicalButton()
{
    static bool down = false;
    static unsigned long pressedAt = 0;
    static bool actionDone = false;
    static bool resetChordActive = false;
    static unsigned long resetChordStartedAt = 0;

    bool pressed =
        digitalRead(
            BUTTON_PIN
        ) == LOW;

    if (
        pressed &&
        !down
    )
    {
        down = true;

        pressedAt =
            millis();

        actionDone = false;
    }

    if (
        !pressed &&
        down
    )
    {
        unsigned long held =
            millis() -
            pressedAt;

        down = false;

        if (
            held < 1000 &&
            !actionDone
        )
        {
            if (alarmPlaying)
            {
                stopAlarm();
                return;
            }

            if (
                currentPage ==
                PAGE_ALARM_SETUP
            )
            {
                alarmEnabled =
                    !alarmEnabled;

                saveAlarmSettings();

                currentPage =
                    PAGE_CLOCK;

                drawClockPage(true);

                return;
            }

            currentPage =
                currentPage ==
                PAGE_CLOCK ?
                PAGE_NOW_PLAYING :
                PAGE_CLOCK;

            drawCurrentPage(true);
        }
    }

    if (
        down &&
        touchStateVolDown
    )
    {
        if (!resetChordActive)
        {
            resetChordActive = true;
            resetChordStartedAt = millis();
        }

        if (
            millis() - resetChordStartedAt >=
                RESET_CHORD_HOLD_MS
        )
        {
            actionDone = true;
            requestDeviceReset();
        }
    }
    else
    {
        resetChordActive = false;
    }

    if (
        down &&
        !actionDone
    )
    {
        unsigned long held =
            millis() -
            pressedAt;

        if (held >= 3000)
        {
            actionDone = true;

            currentPage =
                PAGE_ALARM_SETUP;

            drawAlarmPage(true);
        }
    }
}

// ============================================================
// WEBUI
// ============================================================

const char WEBUI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio Alarm Clock</title>

<style>
*{box-sizing:border-box}

body{
 margin:0;
 background:#07100d;
 color:#fff;
 font-family:Arial,sans-serif
}

.container{
 max-width:900px;
 margin:auto;
 padding:16px
}

.header,.card{
 background:#10231e;
 border:1px solid #2a89;
 border-radius:12px
}

.header{
 background:#11a6;
 padding:16px 18px;
 margin-bottom:14px
}

.header h1{
 margin:0;
 font-size:23px
}

.sub{
 margin-top:5px;
 color:#b8c8c3;
 font-size:13px
}

.grid{
 display:grid;
 grid-template-columns:
 repeat(auto-fit,minmax(280px,1fr));
 gap:14px
}

.card{
 padding:17px
}

.card h2{
 margin:0 0 14px;
 color:#05b9;
 font-size:17px
}

.now{
 text-align:center
}

.station{
 font-size:24px;
 font-weight:bold;
 color:#05b9;
 margin-bottom:7px
}

.song{
 font-size:18px;
 min-height:23px
}

.artist{
 color:#7bef
}

.status{
 margin-top:10px;
 font-size:12px;
 color:#7bef
}

.controls{
 display:flex;
 gap:8px;
 margin-top:14px
}

button{
 flex:1;
 min-height:43px;
 border:1px solid #2a89;
 border-radius:8px;
 background:#183b;
 color:#fff;
 font-size:14px
}

button:hover,
button.primary,
.pageNav button.active{
 background:#05b9;
 color:#00100b
}

.resetControl{
 margin:16px auto 0;
 max-width:420px
}

button.resetButton{
 width:100%;
 background:#71352f;
 border-color:#a65349
}

button.resetButton:hover{
 background:#a74438;
 color:#fff
}

.pageNav{
 display:flex;
 gap:8px;
 margin-bottom:14px
}

select,
input[type=time]{
 width:100%;
 padding:10px;
 border-radius:7px;
 border:1px solid #2a89;
 background:#07100d;
 color:#fff;
 font-size:15px
}

label{
 display:block;
 margin:11px 0 5px;
 color:#7bef;
 font-size:13px
}

input[type=range]{
 width:100%
}

.volumeValue{
 text-align:center;
 color:#05b9
}

.alarmTime{
 text-align:center;
 font-size:40px;
 margin:8px 0
}

.alarmState{
 text-align:center;
 font-weight:bold;
 margin-bottom:10px
}

.on{color:#05b9}
.off{color:#7bef}

.info{
 font-size:13px;
 line-height:1.9;
 color:#b8c8c3
}

.footer{
 text-align:center;
 margin-top:16px;
 color:#52645e;
 font-size:11px
}

@media(max-width:500px){
 .container{padding:9px}
 .station{font-size:21px}
 .alarmTime{font-size:35px}
}
</style>
</head>

<body>

<div class="container">

<div class="header">
<center><h1>RADIO ALARM CLOCK</h1>

<div class="sub" id="network">
Connecting...
</div></center>

</div>

<div class="pageNav" role="group" aria-label="Display page">
<button type="button" class="pageButton" data-page="clock" aria-pressed="false" onclick="selectPage('clock')">
CLOCK
</button>

<button type="button" class="pageButton" data-page="now-playing" aria-pressed="false" onclick="selectPage('now-playing')">
NOW PLAYING
</button>

<button type="button" class="pageButton" data-page="alarm" aria-pressed="false" onclick="selectPage('alarm')">
ALARM
</button>
</div>

<div class="grid">

<div class="card now">

<h2>NOW PLAYING</h2>

<div class="station" id="station">
---
</div>

<div class="artist" id="artist"></div>

<div class="song" id="song">
Connecting...
</div>

<div class="status" id="status"></div>

<div class="controls">

<button onclick="previousStation()">
PREVIOUS
</button>

<button class="primary"
onclick="nextStation()">
NEXT
</button>

</div>

<label>VOLUME</label>

<input
 type="range"
 id="volume"
 min="0"
 max="7"
 value="0"
 oninput="setVolume(this.value)">

<div class="volumeValue">
<span id="volumeValue">0</span> / 7
</div>

</div>

<div class="card">

<h2>RADIO STATIONS</h2>

<label>STATION</label>

<select
 id="stationSelect"
 onchange="selectStation(this.value)">
</select>

</div>

<div class="card">

<h2>ALARM</h2>

<div class="alarmTime"
 id="alarmTime">
--:--
</div>

<div class="alarmState off"
 id="alarmState">
ALARM OFF
</div>

<label>ALARM TIME</label>

<input
 type="time"
 id="alarmInput">

<div class="controls">

<button onclick="saveAlarm()">
SAVE TIME
</button>

<button class="primary"
 onclick="toggleAlarm()">
ON / OFF
</button>

</div>

<label>PRIMARY STATION</label>

<select
 id="primaryStation"
 onchange="setAlarmStations()">
</select>

<label>FALLBACK STATION</label>

<select
 id="fallbackStation"
 onchange="setAlarmStations()">
</select>

</div>

<div class="card">

<h2>DEVICE</h2>

<div class="info">

<div>
Wi-Fi:
<span id="wifi">---</span>
</div>

<div>
IP:
<span id="ip">---</span>
</div>

<div>
Time:
<span id="deviceTime">---</span>
</div>

<div>
Alarm:
<span id="alarmSummary">---</span>
</div>

</div>

</div>

</div>

<div class="footer">
ESP32-S3 Radio Alarm Clock
</div>

<div class="resetControl">
<button type="button" class="resetButton" onclick="confirmDeviceReset()">
RESTART DEVICE
</button>
</div>

</div>

<script>

let stationsLoaded = false;
let volumeTimer;


/* ============================================================
   API
   ============================================================ */

async function api(url)
{
    try
    {
        const response =
            await fetch(url);

        if (!response.ok)
            return null;

        return await response.json();
    }
    catch(e)
    {
        return null;
    }
}


/* ============================================================
   STATUS UPDATE
   ============================================================ */

async function updateStatus()
{
    const d =
        await api('/api/status');

    if (!d)
        return;

    document.querySelectorAll('.pageButton')
        .forEach(
            button =>
            {
                const active =
                    button.dataset.page === d.page;

                button.classList.toggle(
                    'active',
                    active
                );

                button.setAttribute(
                    'aria-pressed',
                    active ? 'true' : 'false'
                );
            }
        );

    document.getElementById('station')
        .textContent =
        d.station || '---';

    document.getElementById('artist')
        .textContent =
        d.artist || '';

    document.getElementById('song')
        .textContent =
        d.song || '';

    document.getElementById('status')
        .textContent =
        d.status || '';

    document.getElementById('volume')
        .value =
        d.volume;

    document.getElementById('volumeValue')
        .textContent =
        d.volume;

    document.getElementById('alarmTime')
        .textContent =
        d.alarmTime;


    /*
     * IMPORTANT:
     *
     * The editable alarmInput is deliberately NOT
     * touched here.
     *
     * This function runs every 7 seconds, but it will
     * never overwrite whatever the user is typing.
     */


    const alarmState =
        document.getElementById(
            'alarmState'
        );

    alarmState.textContent =
        d.alarmEnabled ?
        'ALARM ON' :
        'ALARM OFF';

    alarmState.className =
        d.alarmEnabled ?
        'alarmState on' :
        'alarmState off';


    document.getElementById('wifi')
        .textContent =
        d.wifi ?
        'CONNECTED' :
        'DISCONNECTED';


    document.getElementById('ip')
        .textContent =
        d.ip || '---';


    document.getElementById('deviceTime')
        .textContent =
        d.time || '--:--';


    document.getElementById('alarmSummary')
        .textContent =
        d.alarmTime +
        (
            d.alarmEnabled ?
            ' ON' :
            ' OFF'
        );


    document.getElementById('network')
        .textContent =
        d.wifi ?
        'Connected to Wi-Fi' :
        'Wi-Fi disconnected';


    if (!stationsLoaded)
        await loadStations();
}


/* ============================================================
   LOAD STATIONS
   ============================================================ */

async function loadStations()
{
    const d =
        await api('/api/stations');

    if (
        !d ||
        !Array.isArray(d.stations)
    )
        return;


    const stationSelect =
        document.getElementById(
            'stationSelect'
        );

    const primaryStation =
        document.getElementById(
            'primaryStation'
        );

    const fallbackStation =
        document.getElementById(
            'fallbackStation'
        );


    stationSelect.innerHTML = '';
    primaryStation.innerHTML = '';
    fallbackStation.innerHTML = '';


    d.stations.forEach(
        (s, i) =>
        {
            stationSelect.add(
                new Option(s, i)
            );

            primaryStation.add(
                new Option(s, i)
            );

            fallbackStation.add(
                new Option(s, i)
            );
        }
    );


    stationSelect.value =
        d.current;

    primaryStation.value =
        d.primary;

    fallbackStation.value =
        d.fallback;


    stationsLoaded = true;


    /*
     * Set the alarm input ONCE when the stations
     * have loaded.
     *
     * After this, normal status refreshes will
     * never touch the input.
     */

    const alarmInput =
        document.getElementById(
            'alarmInput'
        );

    if (
        alarmInput &&
        d.alarmTime
    )
    {
        alarmInput.value =
            d.alarmTime;
    }
}


/* ============================================================
   STATIONS
   ============================================================ */

async function previousStation()
{
    await api('/api/previous');
    await updateStatus();
}

async function nextStation()
{
    await api('/api/next');
    await updateStatus();
}

async function selectPage(page)
{
    const result =
        await api(
            '/api/page?page=' +
            encodeURIComponent(page)
        );

    if (result && result.ok)
        await updateStatus();
}

async function confirmDeviceReset()
{
    if (!window.confirm('Restart the radio alarm clock? Saved alarm settings will be kept.'))
        return;

    const result =
        await api('/api/reset');

    if (result && result.ok)
        window.alert('Restarting device...');
}

async function selectStation(i)
{
    await api(
        '/api/station?index=' +
        encodeURIComponent(i)
    );

    await updateStatus();
}


/* ============================================================
   VOLUME
   ============================================================ */

function setVolume(v)
{
    document.getElementById(
        'volumeValue'
    ).textContent = v;

    clearTimeout(
        volumeTimer
    );

    volumeTimer =
        setTimeout(
            () =>
            {
                api(
                    '/api/volume?value=' +
                    v
                );
            },
            100
        );
}


/* ============================================================
   ALARM TIME
   ============================================================ */

async function saveAlarm()
{
    const input =
        document.getElementById(
            'alarmInput'
        );

    const value =
        input.value;

    if (!value)
        return;


    const result =
        await api(
            '/api/alarm/time?value=' +
            encodeURIComponent(value)
        );


    if (
        result &&
        result.ok
    )
    {
        /*
         * Update the displayed alarm time
         * immediately.
         */

        document.getElementById(
            'alarmTime'
        ).textContent =
            value;


        /*
         * Keep exactly what the user entered
         * in the editable field.
         */

        input.value =
            value;


        /*
         * Update the alarm summary locally.
         * No updateStatus() here because that
         * would be unnecessary.
         */

        const alarmState =
            document.getElementById(
                'alarmState'
            );

        document.getElementById(
            'alarmSummary'
        ).textContent =
            value +
            (
                alarmState.textContent ===
                'ALARM ON'
                ? ' ON'
                : ' OFF'
            );
    }
}


/* ============================================================
   ALARM ON / OFF
   ============================================================ */

async function toggleAlarm()
{
    await api(
        '/api/alarm/toggle'
    );

    await updateStatus();
}


/* ============================================================
   ALARM STATIONS
   ============================================================ */

async function setAlarmStations()
{
    const primary =
        document.getElementById(
            'primaryStation'
        ).value;

    const fallback =
        document.getElementById(
            'fallbackStation'
        ).value;


    await api(
        '/api/alarm/stations?primary=' +
        encodeURIComponent(primary) +
        '&fallback=' +
        encodeURIComponent(fallback)
    );
}


/* ============================================================
   STARTUP
   ============================================================ */

async function initialiseWebUI()
{
    /*
     * Get the current status first so the alarm
     * time is available.
     */

    const d =
        await api('/api/status');

    if (d)
    {
        document.getElementById(
            'alarmInput'
        ).value =
            d.alarmTime || '';

        /*
         * Draw all other status information.
         */

        document.getElementById(
            'station'
        ).textContent =
            d.station || '---';

        document.getElementById(
            'artist'
        ).textContent =
            d.artist || '';

        document.getElementById(
            'song'
        ).textContent =
            d.song || '';

        document.getElementById(
            'status'
        ).textContent =
            d.status || '';

        document.getElementById(
            'volume'
        ).value =
            d.volume;

        document.getElementById(
            'volumeValue'
        ).textContent =
            d.volume;

        document.getElementById(
            'alarmTime'
        ).textContent =
            d.alarmTime;

        document.getElementById(
            'alarmState'
        ).textContent =
            d.alarmEnabled ?
            'ALARM ON' :
            'ALARM OFF';

        document.getElementById(
            'alarmState'
        ).className =
            d.alarmEnabled ?
            'alarmState on' :
            'alarmState off';

        document.getElementById(
            'wifi'
        ).textContent =
            d.wifi ?
            'CONNECTED' :
            'DISCONNECTED';

        document.getElementById(
            'ip'
        ).textContent =
            d.ip || '---';

        document.getElementById(
            'deviceTime'
        ).textContent =
            d.time || '--:--';

        document.getElementById(
            'alarmSummary'
        ).textContent =
            d.alarmTime +
            (
                d.alarmEnabled ?
                ' ON' :
                ' OFF'
            );

        document.getElementById(
            'network'
        ).textContent =
            d.wifi ?
            'Connected to Wi-Fi' :
            'Wi-Fi disconnected';
    }


    await loadStations();
}


/*
 * Initial page load.
 */

initialiseWebUI();


/*
 * Live refresh.
 *
 * This updates radio, clock, Wi-Fi, volume,
 * alarm state, etc.
 *
 * It NEVER writes to alarmInput.
 */

setInterval(
    updateStatus,
    2000
);

</script>

</body>
</html>
)rawliteral";

// ============================================================
// RADIO ARTIST / SONG
// ============================================================

void getRadioArtistSong(
    String &artist,
    String &song
)
{
    artist = "";
    song = "";

    String text =
        currentSongText;

    text.trim();

    if (!text.length())
        return;

    int separator =
        text.indexOf(" - ");

    if (separator > 0)
    {
        artist =
            text.substring(
                0,
                separator
            );

        song =
            text.substring(
                separator + 3
            );

        artist.trim();
        song.trim();
    }
    else
    {
        song = text;
    }
}

// ============================================================
// WEB ROOT
// ============================================================

void handleWebRoot()
{
    webServer.send_P(
        200,
        "text/html",
        WEBUI_HTML
    );
}

// ============================================================
// WEB STATUS
// ============================================================

void handleWebStatus()
{
    String artist;
    String song;

    getRadioArtistSong(
        artist,
        song
    );

    char alarm[8];

    snprintf(
        alarm,
        sizeof(alarm),
        "%02d:%02d",
        alarmHour,
        alarmMinute
    );

    bool wifiConnected =
        WiFi.status() ==
        WL_CONNECTED;

    String json =
        "{\"station\":\"" +
        jsonEscape(
            currentStationName
        ) +

        "\",\"artist\":\"" +
        jsonEscape(
            artist
        ) +

        "\",\"song\":\"" +
        jsonEscape(
            song
        ) +

        "\",\"status\":\"" +
        jsonEscape(
            currentStatusText
        ) +

        "\",\"volume\":" +
        String(
            volumeLevel
        ) +

        ",\"alarmEnabled\":" +
        (
            alarmEnabled ?
            "true" :
            "false"
        ) +

        ",\"alarmTime\":\"" +
        String(alarm) +

        "\",\"wifi\":" +
        (
            wifiConnected ?
            "true" :
            "false"
        ) +

        ",\"ip\":\"" +
        (
            wifiConnected ?
            WiFi.localIP().toString() :
            "---"
        ) +

        "\",\"page\":\"" +
        (
            currentPage == PAGE_CLOCK ?
            "clock" :
            currentPage == PAGE_NOW_PLAYING ?
            "now-playing" :
            "alarm"
        ) +

        "\",\"time\":\"" +
        jsonEscape(
            getTimeString()
        ) +

        "\"}";

    webServer.send(
        200,
        "application/json",
        json
    );
}

// ============================================================
// WEB PAGE SELECTION
// ============================================================

void handleWebPage()
{
    if (!webServer.hasArg("page"))
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    String page =
        webServer.arg("page");

    if (page == "clock")
        currentPage = PAGE_CLOCK;
    else if (page == "now-playing")
        currentPage = PAGE_NOW_PLAYING;
    else if (page == "alarm")
        currentPage = PAGE_ALARM_SETUP;
    else
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    drawCurrentPage(true);

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

void handleWebReset()
{
    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );

    requestDeviceReset();
}

// ============================================================
// WEB STATIONS
// ============================================================

void handleWebStations()
{
    String json =
        "{\"current\":" +
        String(
            currentStation
        ) +

        ",\"primary\":" +
        String(
            alarmPrimaryStation
        ) +

        ",\"fallback\":" +
        String(
            alarmFallbackStation
        ) +

        ",\"stations\":[";


    for (
        int i = 0;
        i < stationCount;
        i++
    )
    {
        if (i > 0)
            json += ",";

        json += "\"";

        json +=
            jsonEscape(
                String(
                    stations[i].name
                )
            );

        json += "\"";
    }

    json += "]}";


    webServer.send(
        200,
        "application/json",
        json
    );
}

// ============================================================
// WEB SELECT STATION
// ============================================================

void handleWebStation()
{
    if (
        !webServer.hasArg(
            "index"
        )
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    int index =
        webServer
            .arg("index")
            .toInt();

    if (
        index < 0 ||
        index >= stationCount
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    currentStation =
        index;

    currentStationName =
        stations[
            currentStation
        ].name;

    connectStation(
        currentStation
    );

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WEB PREVIOUS STATION
// ============================================================

void handleWebPrevious()
{
    currentStation--;

    if (currentStation < 0)
        currentStation =
            stationCount - 1;

    currentStationName =
        stations[
            currentStation
        ].name;

    connectStation(
        currentStation
    );

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WEB NEXT STATION
// ============================================================

void handleWebNext()
{
    currentStation++;

    if (
        currentStation >=
        stationCount
    )
    {
        currentStation = 0;
    }

    currentStationName =
        stations[
            currentStation
        ].name;

    connectStation(
        currentStation
    );

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WEB VOLUME
// ============================================================

void handleWebVolume()
{
    if (
        !webServer.hasArg(
            "value"
        )
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    int value =
        webServer
            .arg("value")
            .toInt();

    value =
        constrain(
            value,
            VOLUME_MIN,
            VOLUME_MAX
        );

    volumeLevel =
        value;

    if (!alarmPlaying)
    {
        audio.setVolume(
            volumeLevel
        );
    }

    // Update the TFT immediately without
    // redrawing the whole screen.
    if (
        currentPage ==
        PAGE_NOW_PLAYING
    )
    {
        drawVolumeControl();
    }

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}
// ============================================================
// WEB ALARM TIME
// ============================================================

void handleWebAlarmTime()
{
    if (
        !webServer.hasArg(
            "value"
        )
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    String value =
        webServer.arg(
            "value"
        );

    int colon =
        value.indexOf(':');

    if (
        colon < 0 ||
        value.length() != 5
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    int hour =
        value.substring(
            0,
            colon
        ).toInt();

    int minute =
        value.substring(
            colon + 1
        ).toInt();

    if (
        hour < 0 ||
        hour > 23 ||
        minute < 0 ||
        minute > 59
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    alarmHour =
        hour;

    alarmMinute =
        minute;

    saveAlarmSettings();

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WEB ALARM TOGGLE
// ============================================================

void handleWebAlarmToggle()
{
    alarmEnabled =
        !alarmEnabled;

    saveAlarmSettings();

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WEB ALARM STATIONS
// ============================================================

void handleWebAlarmStations()
{
    if (
        !webServer.hasArg("primary") ||
        !webServer.hasArg("fallback")
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    int primary =
        webServer
            .arg("primary")
            .toInt();

    int fallback =
        webServer
            .arg("fallback")
            .toInt();

    if (
        primary < 0 ||
        primary >= stationCount ||
        fallback < 0 ||
        fallback >= stationCount
    )
    {
        webServer.send(
            400,
            "application/json",
            "{\"ok\":false}"
        );

        return;
    }

    alarmPrimaryStation =
        primary;

    alarmFallbackStation =
        fallback;

    saveAlarmSettings();

    webServer.send(
        200,
        "application/json",
        "{\"ok\":true}"
    );
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi()
{
    WiFi.mode(
        WIFI_STA
    );

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    Serial.print(
        "Connecting WiFi"
    );

    unsigned long start =
        millis();

    while (
        WiFi.status() !=
            WL_CONNECTED &&
        millis() - start <
            20000
    )
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        Serial.print(
            "WiFi connected: "
        );

        Serial.println(
            WiFi.localIP()
        );
    }
    else
    {
        Serial.println(
            "WiFi connection failed"
        );
    }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(
        115200
    );

    delay(500);

    Serial.println();
    Serial.println(
        "ESP32-S3 Radio Alarm Clock"
    );

    pinMode(
        BUTTON_PIN,
        INPUT_PULLUP
    );

    tft.init(
        240,
        320
    );

    tft.setRotation(3);

    tft.setTextWrap(
        false
    );

    tft.fillScreen(
        COLOR_BACKGROUND
    );

    calibrateTouch();

    loadAlarmSettings();

    connectWiFi();

    configTzTime(
        TZ_INFO,
        NTP_SERVER
    );

    audio.setPinout(
        I2S_BCLK,
        I2S_LRC,
        I2S_DOUT
    );

    audio.setVolume(
        volumeLevel
    );

    connectStation(
        currentStation
    );

    // ========================================================
    // WEB SERVER ROUTES
    // ========================================================

    webServer.on(
        "/",
        HTTP_GET,
        handleWebRoot
    );

    webServer.on(
        "/api/status",
        HTTP_GET,
        handleWebStatus
    );

    webServer.on(
        "/api/page",
        HTTP_GET,
        handleWebPage
    );

    webServer.on(
        "/api/reset",
        HTTP_GET,
        handleWebReset
    );

    webServer.on(
        "/api/stations",
        HTTP_GET,
        handleWebStations
    );

    webServer.on(
        "/api/station",
        HTTP_GET,
        handleWebStation
    );

    webServer.on(
        "/api/previous",
        HTTP_GET,
        handleWebPrevious
    );

    webServer.on(
        "/api/next",
        HTTP_GET,
        handleWebNext
    );

    webServer.on(
        "/api/volume",
        HTTP_GET,
        handleWebVolume
    );

    webServer.on(
        "/api/alarm/time",
        HTTP_GET,
        handleWebAlarmTime
    );

    webServer.on(
        "/api/alarm/toggle",
        HTTP_GET,
        handleWebAlarmToggle
    );

    webServer.on(
        "/api/alarm/stations",
        HTTP_GET,
        handleWebAlarmStations
    );

    webServer.onNotFound(
        []()
        {
            webServer.send(
                404,
                "text/plain",
                "Not Found"
            );
        }
    );

    webServer.begin();

    Serial.println(
        "Web server started"
    );

    Serial.print(
        "Open: http://"
    );

    Serial.println(
        WiFi.localIP()
    );

    drawClockPage(true);

    Serial.println(
        "Setup complete"
    );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    audio.loop();

    webServer.handleClient();

    handlePhysicalButton();

    handleTouch();

    processDeviceReset();

    checkAlarm();

    if (
        currentPage ==
        PAGE_NOW_PLAYING
    )
    {
        bool marqueeMoved =
            updateMarquee();

        if (metadataChanged)
        {
            metadataChanged =
                false;

            drawNowPlaying(true);
        }
        else if (marqueeMoved)
        {
            drawNowPlaying(false);
        }
    }

    if (
        currentPage ==
            PAGE_CLOCK &&
        millis() -
            lastClockUpdate >=
            1000
    )
    {
        lastClockUpdate =
            millis();

        drawClockPage(false);
    }

    delay(5);
}
