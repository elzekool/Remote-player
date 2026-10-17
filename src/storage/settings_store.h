#pragma once

#include <Arduino.h>

// Persistent key/value settings on LittleFS. Each key (e.g. "config/ssid") is
// stored as a small file /prefs/<key>. Writes are skipped when the stored value
// is already the same, which saves flash wear.

String readSetting(const char* key, const String& defaultValue = String());
int readIntSetting(const char* key, int defaultValue);
void writeSetting(const char* key, const String& value);
void writeIntSetting(const char* key, int value);
