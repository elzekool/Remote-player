#pragma once

// MQTT connection: subscribes to the configured topic and runs every message as
// a player command (see handleCommand()).

// Configures the broker connection; does nothing when no broker is configured.
void setupMqtt();
// Keeps the connection alive and reconnects when needed; call continuously.
void handleMqtt();
// Whether setupMqtt() was called with a broker configured.
bool mqttEnabled();
bool mqttConnected();
