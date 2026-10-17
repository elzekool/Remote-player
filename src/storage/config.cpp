#include "storage/config.h"

#include "players/players.h"
#include "storage/settings_store.h"

const char* DEFAULT_HOSTNAME = "remote-player";
const char* DEFAULT_MQTT_TOPIC = "remote-player";
const int DEFAULT_PLAYER_COUNT = 1;

Config config;

void loadConfig() {
  config.ssid = readSetting("config/ssid");
  config.wifiPassword = readSetting("config/wifi_pass");
  config.hostname = readSetting("config/hostname", DEFAULT_HOSTNAME);
  config.otaPassword = readSetting("config/ota_pass");
  config.webPassword = readSetting("config/web_pass");
  config.mqttHost = readSetting("config/mqtt_host");
  config.mqttUser = readSetting("config/mqtt_user");
  config.mqttPassword = readSetting("config/mqtt_pass");
  config.mqttTopic = readSetting("config/mqtt_topic", DEFAULT_MQTT_TOPIC);
  config.playerCount = constrain(
      readIntSetting("config/players", DEFAULT_PLAYER_COUNT), 1, MAX_PLAYERS);
}

void saveConfig() {
  writeSetting("config/ssid", config.ssid);
  writeSetting("config/wifi_pass", config.wifiPassword);
  writeSetting("config/hostname", config.hostname);
  writeSetting("config/ota_pass", config.otaPassword);
  writeSetting("config/web_pass", config.webPassword);
  writeSetting("config/mqtt_host", config.mqttHost);
  writeSetting("config/mqtt_user", config.mqttUser);
  writeSetting("config/mqtt_pass", config.mqttPassword);
  writeSetting("config/mqtt_topic", config.mqttTopic);
  writeIntSetting("config/players", config.playerCount);
}
