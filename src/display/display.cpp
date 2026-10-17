#include "display/display.h"

#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

#include "network/mqtt_handler.h"
#include "pins.h"
#include "players/players.h"
#include "storage/config.h"

// Screen layout (128x32, 6x8 pixel font):
//
//   [WiFi][MQTT] 192.168.1.23        status bar
//   --------------------------------
//   1 > 0014       2 @ hello         units 1 and 2
//   3 #            4 off             units 3 and 4
//
// Each unit shows an icon (playing, looping/repeating, delayed, stopped) and
// its track, or a word for a unit that can't play (starting, failed, disabled,
// not configured). Track names longer than fits scroll by one character at a
// time.

static const uint8_t DISPLAY_ADDRESS = 0x3C;
static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 32;
// Both screens share this layout: a top line, a divider and two text rows.
static const int DIVIDER_Y = 10;
static const int ROW_Y[] = {13, 24};
// The screen is redrawn this often, but only sent to the display (512 bytes,
// ~13 ms over I2C) when something changed.
static const uint32_t REDRAW_INTERVAL_MS = 100;
// Characters of a track name that fit in a unit's half of the screen.
static const unsigned int MAX_LABEL_CHARS = 7;
// A longer name shows its start for SCROLL_PAUSE_MS, then moves one character
// every SCROLL_STEP_MS until its end is shown, which also stays for
// SCROLL_PAUSE_MS; then it repeats.
static const uint32_t SCROLL_PAUSE_MS = 1500;
static const uint32_t SCROLL_STEP_MS = 400;

// 8x8 icons: one byte per row, most significant bit on the left.
static const uint8_t ICON_WIFI[] = {0x3C, 0x42, 0x81, 0x18,
                                    0x24, 0x00, 0x18, 0x18};
static const uint8_t ICON_MQTT[] = {0x40, 0xE0, 0x44, 0x44,
                                    0x44, 0x44, 0x0E, 0x04};
static const uint8_t ICON_PLAY[] = {0x80, 0xC0, 0xE0, 0xF0,
                                    0xE0, 0xC0, 0x80, 0x00};
static const uint8_t ICON_LOOP[] = {0x04, 0xFE, 0x84, 0x80,
                                    0x01, 0x21, 0x7F, 0x20};
static const uint8_t ICON_DELAYED[] = {0xFE, 0x44, 0x28, 0x10,
                                       0x28, 0x44, 0xFE, 0x00};
static const uint8_t ICON_STOPPED[] = {0x00, 0xF8, 0xF8, 0xF8,
                                       0xF8, 0xF8, 0x00, 0x00};

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static bool present = false;
// Copy of what was last sent, to skip sending an unchanged screen.
static uint8_t sentFrame[SCREEN_WIDTH * SCREEN_HEIGHT / 8];
static uint32_t lastRedraw = 0;
// Per unit: the label being scrolled, and since when, so a new track starts at
// its beginning.
static String scrollLabels[MAX_PLAYERS];
static uint32_t scrollSince[MAX_PLAYERS];

void setupDisplay() {
  Wire.setSDA(DISPLAY_PIN_SDA);
  Wire.setSCL(DISPLAY_PIN_SCL);
  Wire.begin();
  // The library doesn't check whether a display answers, so ask the address
  // first.
  Wire.beginTransmission(DISPLAY_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("No display found");
    return;
  }
  present = display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false);
  if (!present) {
    Serial.println("Starting the display failed");
    return;
  }
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  showDisplayMessage("Starting");
}

void showDisplayMessage(const String& line1, const String& line2) {
  if (!present) {
    return;
  }
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Remote player");
  display.drawFastHLine(0, DIVIDER_Y, SCREEN_WIDTH, SSD1306_WHITE);
  display.setCursor(0, ROW_Y[0]);
  display.print(line1);
  display.setCursor(0, ROW_Y[1]);
  display.print(line2);
  display.display();
}

// Draws an icon, struck through when its connection is down.
static void drawStatusIcon(int x, const uint8_t* icon, bool ok) {
  display.drawBitmap(x, 0, icon, 8, 8, SSD1306_WHITE);
  if (!ok) {
    display.drawLine(x, 7, x + 7, 0, SSD1306_WHITE);
  }
}

static void drawStatusBar(bool accessPointMode) {
  bool connected = !accessPointMode && WiFi.status() == WL_CONNECTED;
  if (accessPointMode) {
    display.setCursor(0, 0);
    display.print("AP");
  } else {
    drawStatusIcon(0, ICON_WIFI, connected);
  }
  // No icon when MQTT isn't in use.
  if (mqttEnabled()) {
    drawStatusIcon(11, ICON_MQTT, mqttConnected());
  }
  display.setCursor(24, 0);
  if (accessPointMode) {
    display.print(WiFi.softAPIP());
  } else if (connected) {
    display.print(WiFi.localIP());
  } else {
    display.print("No WiFi");
  }
  display.drawFastHLine(0, DIVIDER_Y, SCREEN_WIDTH, SSD1306_WHITE);
}

// The track as a label: the number, or the name without quotes.
static String trackLabel(const Track& track) {
  return track.name[0] ? String(track.name) : String(track.number);
}

// The part of a unit's label to show now: all of it if it fits, otherwise a
// scrolling window.
static String visiblePart(int index, const String& label) {
  if (label != scrollLabels[index]) {
    scrollLabels[index] = label;
    scrollSince[index] = millis();
  }
  if (label.length() <= MAX_LABEL_CHARS) {
    return label;
  }
  uint32_t steps = label.length() - MAX_LABEL_CHARS;
  uint32_t cycle = 2 * SCROLL_PAUSE_MS + steps * SCROLL_STEP_MS;
  uint32_t t = (millis() - scrollSince[index]) % cycle;
  uint32_t offset = t < SCROLL_PAUSE_MS
                        ? 0
                        : min(steps, (t - SCROLL_PAUSE_MS) / SCROLL_STEP_MS);
  return label.substring(offset, offset + MAX_LABEL_CHARS);
}

static void drawUnit(int index, int x, int y) {
  display.setCursor(x, y);
  display.print(index + 1);
  int iconX = x + 9;
  int textX = x + 19;
  const char* word = nullptr;
  const uint8_t* icon = nullptr;
  String label;
  const Player& p = players[index];
  if (index >= config.playerCount) {
    word = "n/a";
  } else if (p.state == PlayerState::Starting) {
    word = "...";
  } else if (p.state == PlayerState::Failed) {
    word = "error";
  } else if (!p.enabled) {
    word = "off";
  } else if (p.delayedActive) {
    icon = ICON_DELAYED;
    label = trackLabel(p.delayedTrack);
  } else if (p.playing) {
    icon = p.looping || p.repeatActive ? ICON_LOOP : ICON_PLAY;
    label = trackLabel(p.nowPlaying);
  } else {
    icon = ICON_STOPPED;
  }
  if (word != nullptr) {
    display.setCursor(iconX, y);
    display.print(word);
    return;
  }
  display.drawBitmap(iconX, y, icon, 8, 8, SSD1306_WHITE);
  display.setCursor(textX, y);
  display.print(visiblePart(index, label));
}

void handleDisplay(bool accessPointMode) {
  if (!present || millis() - lastRedraw < REDRAW_INTERVAL_MS) {
    return;
  }
  lastRedraw = millis();
  display.clearDisplay();
  drawStatusBar(accessPointMode);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    drawUnit(i, (i % 2) * (SCREEN_WIDTH / 2), ROW_Y[i / 2]);
  }
  if (memcmp(display.getBuffer(), sentFrame, sizeof(sentFrame)) != 0) {
    memcpy(sentFrame, display.getBuffer(), sizeof(sentFrame));
    display.display();
  }
}
