#include "network/web.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <SD.h>
#include <SimpleMDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "audio/audio.h"
#include "network/html_writer.h"
#include "network/mqtt_handler.h"
#include "network/web_templates.h"
#include "parsing.h"
#include "pins.h"
#include "players/players.h"
#include "storage/config.h"
#include "storage/sd_card.h"

// The user name for the web password (HTTP basic authentication).
static const char* WEB_USER = "admin";

static WebServer server(80);
static DNSServer dnsServer;
static bool apMode = false;
// Access point mode with a web password: only the WiFi settings are available.
static bool wifiOnly = false;
static bool otaEnabled = false;
static bool restartPending = false;
static uint32_t restartAt = 0;
static String lastCommand;
static String lastCommandOutput;

static void writeChip(HtmlWriter& out, const char* kind, const char* label) {
  out.raw("<span class=\"chip ");
  out.raw(kind);
  out.raw("\">");
  out.text(label);
  out.raw("</span>");
}

// Whether the request carries the web password, or none is set.
static bool authenticated() {
  return config.webPassword.isEmpty() ||
         server.authenticate(WEB_USER, config.webPassword.c_str());
}

// Wraps a request handler so it only runs with the web password.
static std::function<void()> withLogin(void (*handler)()) {
  return [handler]() {
    if (!authenticated()) {
      server.requestAuthentication(BASIC_AUTH, "Remote player");
      return;
    }
    handler();
  };
}

enum class Nav : uint8_t { None, Status, Files, Usage };

// Sends a page: the layout from web_templates.h with the page template as its
// content.
static void sendPage(const char* title, Nav nav, const char* page,
                     const TemplateFiller& fill) {
  auto fillNav = [nav](HtmlWriter& out, const String& name) {
    if (name == "STATUS_NAV") {
      out.raw(nav == Nav::Status ? "active" : "");
    } else if (name == "FILES_NAV") {
      out.raw(nav == Nav::Files ? "active" : "");
    } else if (name == "USAGE_NAV") {
      out.raw(nav == Nav::Usage ? "active" : "");
    } else {
      return false;
    }
    return true;
  };
  HtmlWriter out(server);
  writeTemplate(out, LAYOUT_TEMPLATE, [&](HtmlWriter& out, const String& name) {
    if (name == "TITLE") {
      out.text(title);
    } else if (name == "NAV") {
      if (!wifiOnly) {
        writeTemplate(out, NAV_TEMPLATE, fillNav);
      }
    } else if (name == "CONTENT") {
      writeTemplate(out, page, fill);
    } else {
      return false;
    }
    return true;
  });
}

// --- Status page ---

static void writeWifiStatus(HtmlWriter& out) {
  if (apMode) {
    writeChip(out, "busy", "Access point");
    out.text(" " + WiFi.softAPIP().toString());
  } else if (WiFi.status() == WL_CONNECTED) {
    writeChip(out, "ok", "Connected");
    out.text(" " + config.ssid + ", " + WiFi.localIP().toString());
  } else {
    writeChip(out, "err", "Not connected");
    out.text(" " + config.ssid);
  }
}

static void writeMqttStatus(HtmlWriter& out) {
  if (!mqttEnabled()) {
    writeChip(out, "",
              apMode && !config.mqttHost.isEmpty()
                  ? "Not used in access point mode"
                  : "Not configured");
  } else if (mqttConnected()) {
    writeChip(out, "ok", "Connected");
    out.text(" " + config.mqttHost + ", topic " + config.mqttTopic);
  } else {
    writeChip(out, "err", "Not connected");
    out.text(" " + config.mqttHost);
  }
}

static void writePlayerStatus(HtmlWriter& out, const Player& p) {
  if (p.state == PlayerState::Starting) {
    writeChip(out, "busy", "Starting");
  } else if (p.state == PlayerState::Failed) {
    writeChip(out, "err", "I2S failed");
  } else if (!p.enabled) {
    writeChip(out, "", "Disabled");
  } else if (p.delayedActive) {
    writeChip(out, "busy", "Delayed");
  } else if (p.playing) {
    writeChip(out, "busy", p.looping ? "Looping" : "Playing");
  } else {
    writeChip(out, "ok", "Ready");
  }
}

static void writePlayerRows(HtmlWriter& out) {
  for (int i = 0; i < config.playerCount; i++) {
    const Player& p = players[i];
    bool ready = p.state == PlayerState::Ready;
    out.raw("<tr><td>");
    out.text(i + 1);
    out.raw("</td><td>");
    writePlayerStatus(out, p);
    out.raw("</td><td>");
    if (ready && p.delayedActive) {
      out.text(p.delayedTrack.toString());
    } else if (ready && p.playing) {
      out.text(p.nowPlaying.toString());
    } else {
      out.raw("-");
    }
    out.raw("</td><td>");
    out.raw(ready ? String(p.volume) : String("-"));
    out.raw("</td><td>");
    if (ready && p.repeatActive) {
      out.text((p.repeatTracks.count > 1 ? "Random of " : "Track ") +
               p.repeatTracks.toString());
      out.text(p.repeatRemaining == 0
                   ? ", indefinitely"
                   : ", " + String(p.repeatRemaining) + " more");
    } else {
      out.raw("-");
    }
    const I2sPins& pins = AUDIO_UNIT_PINS[i];
    out.raw("</td><td>");
    out.text(String(pins.bclk) + " / " + pins.lrclk + " / " + pins.din);
    out.raw("</td></tr>");
  }
}

static void writeCheckbox(HtmlWriter& out, const char* name,
                          const char* label) {
  out.raw("<label class=\"check\"><input type=\"checkbox\" name=\"");
  out.raw(name);
  out.raw("\" value=\"1\">");
  out.text(label);
  out.raw("</label>");
}

// Fills in the status page placeholders for OTA updates and the web password.
static bool replacePasswordPlaceholders(HtmlWriter& out, const String& name) {
  bool otaSet = !config.otaPassword.isEmpty();
  bool webSet = !config.webPassword.isEmpty();
  if (name == "OTA_STATUS") {
    writeChip(out, otaEnabled ? "ok" : "",
              otaEnabled ? "Enabled" : "Disabled (no OTA password set)");
  } else if (name == "OTA_PLACEHOLDER") {
    out.text(otaSet ? "(unchanged)" : "(not set: OTA updates disabled)");
  } else if (name == "OTA_CLEAR") {
    if (otaSet) {
      writeCheckbox(out, "ota_clear",
                    "Remove the password and disable OTA updates");
    }
  } else if (name == "WEB_USER") {
    out.text(WEB_USER);
  } else if (name == "WEB_PLACEHOLDER") {
    out.text(webSet ? "(unchanged)" : "(not set: no login needed)");
  } else if (name == "WEB_CLEAR") {
    if (webSet) {
      writeCheckbox(out, "web_clear", "Remove the password (no login needed)");
    }
  } else {
    return false;
  }
  return true;
}

// Fills in the placeholders of the status page. One flat branch per
// placeholder, so it reads easily despite its complexity score.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool replacePlaceholders(HtmlWriter& out, const String& name) {
  if (name == "WIFI_STATUS") {
    writeWifiStatus(out);
  } else if (name == "MQTT_STATUS") {
    writeMqttStatus(out);
  } else if (name == "CARD_STATUS") {
    writeChip(out, sdCardMounted() ? "ok" : "err",
              sdCardMounted() ? "Mounted" : "Not found");
  } else if (name == "PLAYER_ROWS") {
    writePlayerRows(out);
  } else if (name == "COMMAND") {
    out.text(lastCommand);
  } else if (name == "COMMAND_OUTPUT") {
    if (!lastCommandOutput.isEmpty()) {
      out.raw("<pre>");
      out.text(lastCommandOutput);
      out.raw("</pre>");
    }
  } else if (name == "SSID") {
    out.text(config.ssid);
  } else if (name == "HOSTNAME") {
    out.text(config.hostname);
  } else if (name == "MAX_PLAYERS") {
    out.text(MAX_PLAYERS);
  } else if (name == "PLAYER_COUNT") {
    out.text(config.playerCount);
  } else if (name == "MQTT_HOST") {
    out.text(config.mqttHost);
  } else if (name == "MQTT_USER") {
    out.text(config.mqttUser);
  } else if (name == "MQTT_TOPIC") {
    out.text(config.mqttTopic);
  } else if (name == "CPU_LOAD") {
    out.text(audioLoad());
  } else if (name == "PEAK_LOAD") {
    out.text(audioPeakLoad());
  } else if (name == "CARD_LOAD") {
    out.text(audioCardLoad());
  } else if (name == "FREE_HEAP") {
    out.text(rp2040.getFreeHeap() / 1024);
  } else if (name == "TOTAL_HEAP") {
    out.text(rp2040.getTotalHeap() / 1024);
  } else if (name == "BUILD_DATE") {
    out.raw(__DATE__ " " __TIME__);
  } else {
    return replacePasswordPlaceholders(out, name);
  }
  return true;
}

static void handleRoot() {
  sendPage("Status", Nav::Status, ROOT_TEMPLATE, replacePlaceholders);
}

static void handleUsage() {
  sendPage("Usage", Nav::Usage, USAGE_TEMPLATE,
           [](HtmlWriter& out, const String& name) {
             if (name == "MQTT_TOPIC") {
               out.text(config.mqttTopic);
             } else if (name == "HOSTNAME") {
               out.text(config.hostname);
             } else if (name == "WEB_USER") {
               out.text(WEB_USER);
             } else if (name == "MAX_PLAYERS") {
               out.text(MAX_PLAYERS);
             } else if (name == "MAX_TRACK_NUMBER") {
               out.text(MAX_TRACK_NUMBER);
             } else if (name == "MAX_TRACK_NAME") {
               out.text(MAX_TRACK_NAME);
             } else if (name == "MAX_TRACK_CHOICES") {
               out.text(MAX_TRACK_CHOICES);
             } else if (name == "MAX_REPEAT_COUNT") {
               out.text(MAX_REPEAT_COUNT);
             } else if (name == "MAX_DELAY_MS") {
               out.text(MAX_DELAY_MS);
             } else if (name == "MAX_VOLUME") {
               out.text(AUDIO_MAX_VOLUME);
             } else {
               return false;
             }
             return true;
           });
}

// Runs the command in the "cmd" field. Browsers (which accept HTML) are sent
// back to the status page, which shows the output; other clients, like curl,
// get the output as plain text.
static void handleCommandPost() {
  String command = server.arg("cmd");
  command.trim();
  String output;
  if (!command.isEmpty()) {
    Serial.printf("Web command: %s\n", command.c_str());
    handleCommand(command, &output);
  }
  if (server.header("Accept").indexOf("text/html") < 0) {
    if (command.isEmpty()) {
      server.send(400, "text/plain", "Missing cmd field\n");
    } else {
      server.send(200, "text/plain", output);
    }
    return;
  }
  lastCommand = command;
  lastCommandOutput = output;
  // Redirect so reloading the page doesn't send the command again.
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

// Takes the WiFi settings from the form. An empty password keeps the existing
// one, unless the SSID changed (then the old password is meaningless and an
// open network is assumed).
static void applyWifiSettings() {
  String ssid = server.arg("ssid");
  ssid.trim();
  String wifiPass = server.arg("wifi_pass");
  if (!wifiPass.isEmpty() || ssid != config.ssid) {
    config.wifiPassword = wifiPass;
  }
  config.ssid = ssid;
}

// Takes a password from the form: a new value replaces it, the clear checkbox
// removes it, and an empty field keeps it.
static void applyPassword(const char* field, const char* clearField,
                          String& password) {
  if (server.hasArg(clearField)) {
    password = "";
  } else if (!server.arg(field).isEmpty()) {
    password = server.arg(field);
  }
}

// Saves the settings, shows the saved page and restarts shortly after.
static void saveAndRestart() {
  saveConfig();
  sendPage("Saved", Nav::None, SAVED_TEMPLATE,
           [](HtmlWriter& out, const String& name) {
             if (name != "RECONNECT") {
               return false;
             }
             if (!config.ssid.isEmpty()) {
               out.raw("<p>It will connect to <b>");
               out.text(config.ssid);
               out.raw("</b> and be reachable at <b>http://");
               out.text(config.hostname);
               out.raw(".local/</b></p>");
             }
             return true;
           });
  restartPending = true;
  restartAt = millis() + 1000;
}

static void handleSave() {
  String hostname = server.arg("hostname");
  String mqttHost = server.arg("mqtt_host");
  String mqttUser = server.arg("mqtt_user");
  String mqttPass = server.arg("mqtt_pass");
  String mqttTopic = server.arg("mqtt_topic");
  String playersArg = server.arg("players");
  hostname.trim();
  mqttHost.trim();
  mqttUser.trim();
  mqttTopic.trim();
  playersArg.trim();

  if (hostname.isEmpty()) {
    hostname = DEFAULT_HOSTNAME;
  }
  if (mqttTopic.isEmpty()) {
    mqttTopic = DEFAULT_MQTT_TOPIC;
  }
  int playerCount;
  if (!parseNumber(playersArg, 1, MAX_PLAYERS, playerCount)) {
    playerCount = config.playerCount;
  }

  applyWifiSettings();
  applyPassword("ota_pass", "ota_clear", config.otaPassword);
  applyPassword("web_pass", "web_clear", config.webPassword);
  // Same rule as for the WiFi password: a new user or broker without a
  // password clears it.
  if (!mqttPass.isEmpty() || mqttUser != config.mqttUser ||
      mqttHost != config.mqttHost) {
    config.mqttPassword = mqttPass;
  }
  config.mqttHost = mqttHost;
  config.mqttUser = mqttUser;
  config.mqttTopic = mqttTopic;
  config.hostname = hostname;
  config.playerCount = playerCount;
  saveAndRestart();
}

// --- WiFi-only pages (access point mode with a web password) ---

static void handleWifiOnly() {
  sendPage("WiFi", Nav::None, WIFI_ONLY_TEMPLATE,
           [](HtmlWriter& out, const String& name) {
             if (name != "SSID") {
               return false;
             }
             out.text(config.ssid);
             return true;
           });
}

static void handleWifiSave() {
  applyWifiSettings();
  saveAndRestart();
}

// --- File manager for the root of the SD card ---

// Result of the last delete or upload, shown once on the files page.
static String fileMessage;
static File uploadFile;
static String uploadFilePath;

// Turns the name of an existing file in the card root into its path ("0014.wav"
// ->
// "/0014.wav"). Returns an empty string for anything that isn't a plain file
// name.
static String existingFilePath(const String& name) {
  if (name.isEmpty() || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
    return "";
  }
  return "/" + name;
}

// The extension of a file name or path including the dot (".wav"), or "" if it
// has none. A dot at the start of the name (".hidden") doesn't begin an
// extension.
static String fileExtension(const String& name) {
  int dot = name.lastIndexOf('.');
  int nameStart = name.lastIndexOf('/') + 1;
  return dot > nameStart ? name.substring(dot) : String();
}

static String formatSize(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  }
  if (bytes < 1024 * 1024) {
    return String(bytes / 1024.0, 1) + " KB";
  }
  return String(bytes / (1024.0 * 1024.0), 1) + " MB";
}

static void redirectToFiles() {
  server.sendHeader("Location", "/files", true);
  server.send(303, "text/plain", "");
}

static void handleFiles() {
  struct Entry {
    String name;
    size_t size;
  };
  std::vector<Entry> files;
  sdCardLock();
  bool mounted = sdCardMounted() || sdCardMount();
  if (mounted) {
    Dir dir = SDFS.openDir("/");
    while (dir.next()) {
      if (dir.isFile()) {
        files.push_back({dir.fileName(), dir.fileSize()});
      }
    }
  }
  sdCardUnlock();
  std::sort(files.begin(), files.end(),
            [](const Entry& a, const Entry& b) { return a.name < b.name; });

  sendPage("Files", Nav::Files, FILES_TEMPLATE,
           [&](HtmlWriter& out, const String& name) {
             if (name == "MESSAGE") {
               if (!fileMessage.isEmpty()) {
                 out.raw("<div class=\"notice\">");
                 out.text(fileMessage);
                 out.raw("</div>");
                 fileMessage = "";
               }
             } else if (name == "FILE_LIST") {
               if (!mounted) {
                 out.raw("<p>");
                 writeChip(out, "err", "No SD card found");
                 out.raw("</p>");
               } else if (files.empty()) {
                 out.raw("<p class=\"hint\">No files</p>");
               } else {
                 out.raw(
                     "<div "
                     "class=\"table\"><table><tr><th>Name</th><th>Size</"
                     "th><th></th></tr>");
                 for (const Entry& f : files) {
                   writeTemplate(out, FILE_ROW_TEMPLATE,
                                 [&](HtmlWriter& out, const String& name) {
                                   if (name == "NAME") {
                                     out.text(f.name);
                                   } else if (name == "SIZE") {
                                     out.text(formatSize(f.size));
                                   } else {
                                     return false;
                                   }
                                   return true;
                                 });
                 }
                 out.raw("</table></div>");
               }
             } else {
               return false;
             }
             return true;
           });
}

static void handleFileRename() {
  String from = existingFilePath(server.arg("name"));
  String toName = server.arg("to");
  toName.trim();
  String problem = sdCardNameProblem(toName);
  if (problem.isEmpty() &&
      !fileExtension(toName).equalsIgnoreCase(fileExtension(from))) {
    problem = "the extension can't be changed";
  }
  if (from.isEmpty()) {
    fileMessage = "Invalid file name";
  } else if (!problem.isEmpty()) {
    fileMessage = "Can't rename to \"" + toName + "\": " + problem;
  } else {
    String to = "/" + toName;
    sdCardLock();
    if (to == from) {
      fileMessage = "Name unchanged";
    } else if (audioFileInUse(from)) {
      fileMessage = from + " is playing; stop it before renaming";
    } else if (!to.equalsIgnoreCase(from) && SD.exists(to)) {
      fileMessage = to + " already exists";
    } else if (sdCardRename(from, to)) {
      fileMessage = "Renamed " + from + " to " + to;
    } else {
      fileMessage = "Could not rename " + from;
    }
    sdCardUnlock();
  }
  Serial.println(fileMessage);
  redirectToFiles();
}

static void handleFileDelete() {
  String path = existingFilePath(server.arg("name"));
  if (path.isEmpty()) {
    fileMessage = "Invalid file name";
  } else {
    sdCardLock();
    if (audioFileInUse(path)) {
      fileMessage = path + " is playing; stop it before deleting";
    } else if (SD.remove(path)) {
      fileMessage = "Deleted " + path;
    } else {
      fileMessage = "Could not delete " + path;
    }
    sdCardUnlock();
  }
  Serial.println(fileMessage);
  redirectToFiles();
}

// Ends an upload; a failed upload removes the partial file.
static void finishUpload(bool ok, const String& message) {
  sdCardLock();
  if (uploadFile) {
    uploadFile.close();
    if (!ok) {
      SD.remove(uploadFilePath);
    }
  }
  sdCardSetWritePath("");
  sdCardUnlock();
  fileMessage = message;
  Serial.println(message);
}

// Receives the upload in chunks. Each chunk is written with the card locked,
// which keeps playback from reading for that long; a slow card can cause short
// dropouts.
static void handleUploadData() {
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      // Some browsers send the full local path of the file.
      String name = upload.filename;
      name = name.substring(max(name.lastIndexOf('/'), name.lastIndexOf('\\')) +
                            1);
      String problem = sdCardNameProblem(name);
      if (!problem.isEmpty()) {
        uploadFilePath = "";
        fileMessage = "Can't upload \"" + name + "\": " + problem;
        return;
      }
      uploadFilePath = "/" + name;
      sdCardLock();
      if (audioFileInUse(uploadFilePath)) {
        fileMessage =
            uploadFilePath + " is playing; stop it before replacing it";
      } else if (!sdCardMounted() && !sdCardMount()) {
        fileMessage = "No SD card found";
      } else {
        uploadFile = SD.open(uploadFilePath, "w");
        if (uploadFile) {
          sdCardSetWritePath(uploadFilePath);
        } else {
          fileMessage = "Could not create " + uploadFilePath;
        }
      }
      sdCardUnlock();
      break;
    }
    case UPLOAD_FILE_WRITE: {
      if (!uploadFile) {
        return;
      }
      sdCardLock();
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      sdCardUnlock();
      if (written != upload.currentSize) {
        finishUpload(
            false, "Writing " + uploadFilePath + " failed; is the card full?");
      }
      break;
    }
    case UPLOAD_FILE_END:
      if (uploadFile) {
        finishUpload(true, "Uploaded " + uploadFilePath + " (" +
                               formatSize(upload.totalSize) + ")");
      }
      break;
    case UPLOAD_FILE_ABORTED:
      if (uploadFile) {
        finishUpload(false, "Upload of " + uploadFilePath + " aborted");
      }
      break;
  }
}

static void handleNotFound() {
  // In AP mode redirect everything to the config page (captive portal).
  if (apMode) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/",
                      true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// Without a password anyone could replace the firmware, so OTA updates then
// stay off and only mDNS is started.
static void startOtaAndMdns() {
  otaEnabled = !config.otaPassword.isEmpty();
  if (otaEnabled) {
    ArduinoOTA.setHostname(config.hostname.c_str());
    ArduinoOTA.setPassword(config.otaPassword.c_str());
    ArduinoOTA.begin();  // also starts mDNS
  } else {
    Serial.println("No OTA password set, OTA updates disabled");
    MDNS.begin(config.hostname.c_str());
  }
  MDNS.addService("http", "tcp", 80);
}

void setupWeb(bool accessPointMode) {
  apMode = accessPointMode;
  wifiOnly = apMode && !config.webPassword.isEmpty();
  if (apMode) {
    dnsServer.start(53, "*", WiFi.softAPIP());
  }
  startOtaAndMdns();

  server.collectHeaders("Accept");
  if (wifiOnly) {
    server.on("/", HTTP_GET, handleWifiOnly);
    server.on("/save", HTTP_POST, handleWifiSave);
  } else {
    server.on("/", HTTP_GET, withLogin(handleRoot));
    server.on("/save", HTTP_POST, withLogin(handleSave));
    server.on("/command", HTTP_POST, withLogin(handleCommandPost));
    server.on("/usage", HTTP_GET, withLogin(handleUsage));
    server.on("/files", HTTP_GET, withLogin(handleFiles));
    server.on("/files/delete", HTTP_POST, withLogin(handleFileDelete));
    server.on("/files/rename", HTTP_POST, withLogin(handleFileRename));
    // The upload data arrives before the final handler runs, so it is only
    // written with the password.
    server.on("/files/upload", HTTP_POST, withLogin(redirectToFiles), []() {
      if (authenticated()) {
        handleUploadData();
      }
    });
  }
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleWeb() {
  // ArduinoOTA uses SimpleMDNS, so this file must use it too: its global MDNS
  // object would otherwise clash with LEAmDNS's object of the same name and
  // crash.
  if (otaEnabled) {
    ArduinoOTA.handle();  // also services mDNS
  } else {
    MDNS.update();
  }
  server.handleClient();
  if (apMode) {
    dnsServer.processNextRequest();
  }
  if (restartPending && (int32_t)(millis() - restartAt) >= 0) {
    rp2040.reboot();
  }
}
