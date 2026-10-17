#pragma once

#include <Arduino.h>

// Persistent device settings, stored with settings_store.

extern const char* DEFAULT_HOSTNAME;
extern const char* DEFAULT_MQTT_TOPIC;
extern const int DEFAULT_PLAYER_COUNT;

struct Config {
  String ssid;
  String wifiPassword;
  String hostname;
  String otaPassword;  // empty disables OTA updates
  String webPassword;  // empty means the web interface needs no login
  String mqttHost;
  String mqttUser;
  String mqttPassword;
  String mqttTopic;
  int playerCount;
};

extern Config config;

void loadConfig();
void saveConfig();
