// Remote player: plays WAV and MP3 files from an SD card on up to four I2S
// amplifiers, controlled over MQTT and a web page. See README.md for setup and
// wiring.
//
// Core 0 runs WiFi, the web server, MQTT, command handling and the display.
// Core 1 runs only the audio (reading the card, decoding, I2S output), so
// network activity can't cause dropouts.

#include <Arduino.h>
#include <WiFi.h>

#include "display/display.h"
#include "network/mqtt_handler.h"
#include "network/web.h"
#include "players/players.h"
#include "storage/config.h"
#include "storage/sd_card.h"

// A reset (e.g. after flashing) doesn't disconnect from the router, and some
// routers refuse the device until the old connection times out; a first attempt
// can also just miss the network. So connecting is tried a few times before
// falling back to the access point.
static const int WIFI_CONNECT_ATTEMPTS = 3;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static const uint32_t WIFI_RETRY_DELAY_MS = 3000;
// In access point mode with WiFi configured, the device restarts to try the
// network again after this long, unless someone is connected to the access
// point (e.g. to fix the settings).
static const uint32_t AP_MODE_RETRY_MS = 5 * 60 * 1000;
static const IPAddress AP_IP(192, 168, 4, 1);

static bool apMode = false;
// Set at the end of setup(). setup1() runs at the same time as setup(), so it
// waits for this: starting the audio needs the configuration and the SD card to
// be initialised first.
static volatile bool setupDone = false;

static const char* wifiStatusName(int8_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "network not found";
    case WL_CONNECT_FAILED:
      return "connection refused (wrong password?)";
    case WL_CONNECTION_LOST:
      return "connection lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "timed out";
  }
}

static bool connectWifi() {
  if (config.ssid.isEmpty()) {
    Serial.println("No WiFi configured");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(config.hostname.c_str());
  for (int attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; attempt++) {
    Serial.printf("Connecting to %s (attempt %d of %d)\n", config.ssid.c_str(),
                  attempt, WIFI_CONNECT_ATTEMPTS);
    showDisplayMessage(
        "Connecting to WiFi:",
        config.ssid + (attempt > 1 ? " (" + String(attempt) + ")" : ""));
    WiFi.begin(config.ssid.c_str(), config.wifiPassword.c_str());
    // A wl_status_t value, or -1 on a timeout.
    int8_t status = WiFi.waitForConnectResult(WIFI_CONNECT_TIMEOUT_MS);
    if (status == WL_CONNECTED) {
      return true;
    }
    Serial.printf("Connection failed: %s (status %d)\n", wifiStatusName(status),
                  status);
    WiFi.disconnect();
    if (attempt < WIFI_CONNECT_ATTEMPTS) {
      delay(WIFI_RETRY_DELAY_MS);
    }
  }
  WiFi.disconnect(true);
  return false;
}

static void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(config.hostname.c_str())) {
    Serial.println("Starting access point failed");
    return;
  }
  Serial.printf("Access point '%s' started\n", config.hostname.c_str());
}

// The access point is meant for setting up WiFi. If WiFi is configured but
// couldn't be reached at start-up, restart now and then to try again, unless
// someone is using the access point.
static void retryWifiFromAccessPoint() {
  static uint32_t idleSince = millis();
  if (!apMode || config.ssid.isEmpty()) {
    return;
  }
  if (WiFi.softAPgetStationNum() > 0) {
    idleSince = millis();
  } else if (millis() - idleSince >= AP_MODE_RETRY_MS) {
    Serial.println("Restarting to try connecting to WiFi again");
    delay(100);
    rp2040.reboot();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  loadConfig();
  setupDisplay();
  setupSdCard();
  setupPlayers();

  apMode = !connectWifi();
  if (apMode) {
    startAccessPoint();
  }

  setupWeb(apMode);
  if (!apMode) {
    setupMqtt();
  }

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(apMode ? WiFi.softAPIP() : WiFi.localIP());
  setupDone = true;
}

void loop() {
  handleWeb();
  handleMqtt();
  updatePlayers();
  handleDisplay(apMode);
  retryWifiFromAccessPoint();
}

void setup1() {
  while (!setupDone) {
    delay(10);
  }
  startPlayerAudio();
}

void loop1() { streamPlayerAudio(); }
