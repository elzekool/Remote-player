#include "storage/settings_store.h"

#include <LittleFS.h>

static bool mountFileSystem() {
  static bool mounted = false;
  if (!mounted) {
    // begin() formats the partition if it can't be mounted (e.g. on first
    // boot).
    mounted = LittleFS.begin();
    if (!mounted) {
      Serial.println("LittleFS mount failed, settings can't be stored");
    }
  }
  return mounted;
}

static String settingPath(const char* key) { return String("/prefs/") + key; }

static bool read(const char* key, String& value) {
  if (!mountFileSystem()) {
    return false;
  }
  File f = LittleFS.open(settingPath(key), "r");
  if (!f) {
    return false;
  }
  value = f.readString();
  f.close();
  return true;
}

String readSetting(const char* key, const String& defaultValue) {
  String value;
  return read(key, value) ? value : defaultValue;
}

int readIntSetting(const char* key, int defaultValue) {
  String value;
  return read(key, value) ? value.toInt() : defaultValue;
}

void writeSetting(const char* key, const String& value) {
  String current;
  if (read(key, current) && current == value) {
    return;
  }
  if (!mountFileSystem()) {
    return;
  }
  // Missing directories are created by LittleFS.
  File f = LittleFS.open(settingPath(key), "w");
  if (!f) {
    Serial.printf("Saving setting %s failed\n", key);
    return;
  }
  f.print(value);
  f.close();
}

void writeIntSetting(const char* key, int value) {
  writeSetting(key, String(value));
}
