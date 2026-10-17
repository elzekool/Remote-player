#pragma once

#include <Arduino.h>

// Status display: a 128x32 SSD1306 OLED over I2C (pins in pins.h), showing the
// WiFi and MQTT status, the IP address and what each unit is doing. Without a
// display connected, all functions do nothing.

// Starts the display; call once before the other functions.
void setupDisplay();
// Shows a message of up to two lines (21 characters each) below the title, e.g.
// while connecting to WiFi.
void showDisplayMessage(const String& line1, const String& line2 = String());
// Redraws the status screen; call continuously.
void handleDisplay(bool accessPointMode);
