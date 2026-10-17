#include "network/mqtt_handler.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "players/players.h"
#include "storage/config.h"

static const uint16_t DEFAULT_MQTT_PORT = 1883;
static const uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static String mqttServerHost;
static bool enabled = false;
static uint32_t lastMqttAttempt = 0;

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message(payload, length);
  Serial.printf("MQTT [%s] %s\n", topic, message.c_str());
  handleCommand(message);
}

void setupMqtt() {
  if (config.mqttHost.isEmpty()) {
    return;
  }
  // Accept "host" or "host:port".
  uint16_t port = DEFAULT_MQTT_PORT;
  mqttServerHost = config.mqttHost;
  int colon = mqttServerHost.lastIndexOf(':');
  if (colon > 0) {
    port = mqttServerHost.substring(colon + 1).toInt();
    mqttServerHost = mqttServerHost.substring(0, colon);
  }
  mqtt.setServer(mqttServerHost.c_str(), port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
  enabled = true;
}

bool mqttEnabled() { return enabled; }

bool mqttConnected() { return enabled && mqtt.connected(); }

void handleMqtt() {
  if (!enabled) {
    return;
  }
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (lastMqttAttempt != 0 &&
      millis() - lastMqttAttempt < MQTT_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastMqttAttempt = millis();

  Serial.printf("Connecting to MQTT broker %s\n", config.mqttHost.c_str());
  bool ok = config.mqttUser.isEmpty()
                ? mqtt.connect(config.hostname.c_str())
                : mqtt.connect(config.hostname.c_str(), config.mqttUser.c_str(),
                               config.mqttPassword.c_str());
  if (!ok) {
    Serial.printf("MQTT connection failed, state %d\n", mqtt.state());
    return;
  }
  Serial.println("MQTT connected");
  if (mqtt.subscribe(config.mqttTopic.c_str(), 0)) {
    Serial.printf("Subscribed to %s\n", config.mqttTopic.c_str());
  } else {
    Serial.printf("Subscribing to %s failed\n", config.mqttTopic.c_str());
  }
}
