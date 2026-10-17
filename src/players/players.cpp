#include "players/players.h"

#include "audio/audio.h"
#include "parsing.h"
#include "storage/config.h"
#include "storage/sd_card.h"
#include "storage/settings_store.h"

static const int MAX_VOLUME = AUDIO_MAX_VOLUME;
static const int DEFAULT_VOLUME = 10;

Player players[MAX_PLAYERS];

// While a command runs, its log lines are also collected here so the caller can
// show them.
static String* commandOutput = nullptr;

static void logln(const char* line) {
  Serial.println(line);
  if (commandOutput) {
    *commandOutput += line;
    *commandOutput += '\n';
  }
}

static void logf(const char* format, ...) {
  // Room for a full list of named tracks.
  char buffer[768];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  logln(buffer);
}

// Per-unit settings are stored as "player/<name><unit>", e.g. "player/volume2".
static String unitKey(const Player& p, const char* name) {
  return String("player/") + name + p.unit;
}

static int loadVolume(const Player& p) {
  return constrain(readIntSetting(unitKey(p, "volume").c_str(), DEFAULT_VOLUME),
                   0, MAX_VOLUME);
}

static void saveVolume(const Player& p, int volume) {
  writeIntSetting(unitKey(p, "volume").c_str(), volume);
}

static bool loadEnabled(const Player& p) {
  return readIntSetting(unitKey(p, "enabled").c_str(), 1) != 0;
}

static void saveEnabled(const Player& p, bool enabled) {
  writeIntSetting(unitKey(p, "enabled").c_str(), enabled ? 1 : 0);
}

static void startTrack(Player& p, const Track& track, bool loop = false) {
  p.playing = true;
  p.looping = loop;
  p.nowPlaying = track;
  audioPlay(p.unit - 1, track, loop);
}

static void cancelRepeat(Player& p) { p.repeatActive = false; }

static void cancelDelayed(Player& p) {
  if (p.delayedActive) {
    logf("[unit %d] Delayed track %s cancelled", p.unit,
         p.delayedTrack.toString().c_str());
    p.delayedActive = false;
  }
}

static void stopPlayer(Player& p) {
  cancelRepeat(p);
  p.playing = false;
  audioStop(p.unit - 1);
}

// Commands that start playback call this first; a disabled unit ignores them.
static bool playbackEnabled(const Player& p) {
  if (!p.enabled) {
    logf("[unit %d] Playback disabled, ignoring command", p.unit);
  }
  return p.enabled;
}

static const char* unavailableReason(const Player& p) {
  return p.state == PlayerState::Starting ? "Audio output still starting"
                                          : "Audio output not available";
}

// Parses a track or a comma separated list of tracks without spaces between
// them, like 4 or 1,"hello",6,"4". A number is a numbered track; a name in
// double quotes is a named track, which may contain spaces and commas.
static bool parseTrackList(const String& s, TrackList& out) {
  out.count = 0;
  unsigned int start = 0;
  while (true) {
    if (out.count == MAX_TRACK_CHOICES) {
      return false;
    }
    Track& track = out.tracks[out.count];
    unsigned int end;  // just past the item
    if (start < s.length() && s[start] == '"') {
      int closing = s.indexOf('"', start + 1);
      if (closing < 0 ||
          !Track::named(s.substring(start + 1, closing), track)) {
        return false;
      }
      end = closing + 1;
    } else {
      int comma = s.indexOf(',', start);
      end = comma < 0 ? s.length() : comma;
      int value;
      if (!parseNumber(s.substring(start, end), 1, MAX_TRACK_NUMBER, value)) {
        return false;
      }
      track = Track::numbered(value);
    }
    out.count++;
    if (end == s.length()) {
      return true;
    }
    if (s[end] != ',') {
      return false;
    }
    start = end + 1;
  }
}

// Describes a picked track for the log, e.g. 4 or "hello" (picked from
// 1,"hello",6).
static String describePick(const TrackList& tracks, const Track& track) {
  String s = track.toString();
  if (tracks.count > 1) {
    s += " (picked from " + tracks.toString() + ")";
  }
  return s;
}

// The arguments of a unit command, parsed as its UnitCommand entry describes.
struct CommandArgs {
  TrackList tracks;
  int number = 0;  // 0 when an optional number is left out
};

enum class NumberArg : uint8_t { None, Optional, Required };

// A command for one unit and the arguments it takes, in this order: a track
// list (if takesTracks), then a number from 0 to maxNumber.
struct UnitCommand {
  const char* name;
  bool takesTracks;
  NumberArg number;
  int maxNumber;
  bool startsPlayback;  // refused while the unit is disabled
  void (*run)(Player& p, const CommandArgs& args);
};

static void runPlay(Player& p, const CommandArgs& args) {
  Track track = args.tracks.pick();
  logf("[unit %d] Playing track %s", p.unit,
       describePick(args.tracks, track).c_str());
  cancelRepeat(p);
  startTrack(p, track);
}

static void runDelayed(Player& p, const CommandArgs& args) {
  Track track = args.tracks.pick();
  logf("[unit %d] Playing track %s in %d ms", p.unit,
       describePick(args.tracks, track).c_str(), args.number);
  cancelRepeat(p);
  p.delayedActive = true;
  p.delayedTrack = track;
  p.delayedDue = millis() + args.number;
}

static void runLoop(Player& p, const CommandArgs& args) {
  Track track = args.tracks.pick();
  logf("[unit %d] Looping track %s", p.unit,
       describePick(args.tracks, track).c_str());
  cancelRepeat(p);
  startTrack(p, track, true);
}

// A count of 0 repeats until stopped.
static void runRepeat(Player& p, const CommandArgs& args) {
  int count = args.number;
  Track track = args.tracks.pick();
  String description = describePick(args.tracks, track);
  if (count == 0) {
    logf("[unit %d] Repeating indefinitely, starting with track %s", p.unit,
         description.c_str());
  } else {
    logf("[unit %d] Playing %d time(s), starting with track %s", p.unit, count,
         description.c_str());
  }
  p.repeatActive = true;
  p.repeatTracks = args.tracks;
  p.repeatRemaining = count == 0 ? 0 : count - 1;
  startTrack(p, track);
  if (count == 1) {
    cancelRepeat(p);
  }
}

static void runReplace(Player& p, const CommandArgs& args) {
  if (!p.repeatActive) {
    logf("[unit %d] Nothing repeating, ignoring replace", p.unit);
    return;
  }
  logf("[unit %d] Next repeat will play %s %s", p.unit,
       args.tracks.count > 1 ? "a track picked from" : "track",
       args.tracks.toString().c_str());
  p.repeatTracks = args.tracks;
}

static void runStop(Player& p, const CommandArgs&) {
  logf("[unit %d] Stopping playback", p.unit);
  stopPlayer(p);
}

static void runDisable(Player& p, const CommandArgs&) {
  logf("[unit %d] Disabling playback", p.unit);
  stopPlayer(p);
  p.enabled = false;
  saveEnabled(p, false);
}

static void runEnable(Player& p, const CommandArgs&) {
  logf("[unit %d] Enabling playback", p.unit);
  p.enabled = true;
  saveEnabled(p, true);
}

static void runVolume(Player& p, const CommandArgs& args) {
  logf("[unit %d] Setting volume to %d", p.unit, args.number);
  audioSetVolume(p.unit - 1, args.number);
  p.volume = args.number;
  saveVolume(p, args.number);
}

static const UnitCommand UNIT_COMMANDS[] = {
    {"play", true, NumberArg::None, 0, true, runPlay},
    {"delayed", true, NumberArg::Required, MAX_DELAY_MS, true, runDelayed},
    {"loop", true, NumberArg::None, 0, true, runLoop},
    {"repeat", true, NumberArg::Optional, MAX_REPEAT_COUNT, true, runRepeat},
    {"replace", true, NumberArg::None, 0, false, runReplace},
    {"stop", false, NumberArg::None, 0, false, runStop},
    {"disable", false, NumberArg::None, 0, false, runDisable},
    {"enable", false, NumberArg::None, 0, false, runEnable},
    {"volume", false, NumberArg::Required, MAX_VOLUME, false, runVolume},
};

static const UnitCommand* findUnitCommand(const String& name) {
  for (const UnitCommand& command : UNIT_COMMANDS) {
    if (name == command.name) {
      return &command;
    }
  }
  return nullptr;
}

// Parses the words after the command name as the command describes. Returns
// false if they don't match.
static bool parseCommandArgs(const UnitCommand& command, const ArgList& args,
                             CommandArgs& out) {
  int next = 1;
  if (command.takesTracks) {
    if (next >= args.argCount ||
        !parseTrackList(args.arguments[next], out.tracks)) {
      return false;
    }
    next++;
  }
  if (command.number != NumberArg::None && next < args.argCount) {
    if (!parseNumber(args.arguments[next], 0, command.maxNumber, out.number)) {
      return false;
    }
    next++;
  } else if (command.number == NumberArg::Required) {
    return false;
  }
  return next == args.argCount;
}

// Runs one command on one unit; args.arguments[0] is the command name in lower
// case. Returns false if the command or its arguments are invalid. Every valid
// command cancels a pending delayed track.
static bool handleUnitCommand(Player& p, const ArgList& args) {
  const UnitCommand* command = findUnitCommand(args.arguments[0]);
  CommandArgs parsed;
  if (command == nullptr || !parseCommandArgs(*command, args, parsed)) {
    return false;
  }
  cancelDelayed(p);
  if (command->startsPlayback && !playbackEnabled(p)) {
    return true;
  }
  command->run(p, parsed);
  return true;
}

// Commands have the form "[<unit>:]<command> [args]", where <unit> is 1-4 or
// "all". Without a unit prefix the command goes to unit 1.
static void runCommand(String message) {
  message.trim();
  int unit = 1;
  bool allUnits = false;
  String body = message;
  int colon = message.indexOf(':');
  int space = message.indexOf(' ');
  if (colon >= 0 && (space < 0 || colon < space)) {
    String unitArg = message.substring(0, colon);
    if (unitArg.equalsIgnoreCase("all")) {
      allUnits = true;
    } else if (!parseNumber(unitArg, 1, MAX_PLAYERS, unit)) {
      logf("Invalid unit in command: '%s'", message.c_str());
      return;
    }
    body = message.substring(colon + 1);
    body.trim();
  }

  ArgList args;
  if (!args.parse(body) || args.argCount == 0) {
    logf("Unknown or invalid command: '%s'", message.c_str());
    return;
  }
  args.arguments[0].toLowerCase();

  if (allUnits) {
    bool anyReady = false;
    for (int i = 0; i < config.playerCount; i++) {
      if (players[i].state != PlayerState::Ready) {
        logf("[unit %d] %s, skipping", players[i].unit,
             unavailableReason(players[i]));
        continue;
      }
      anyReady = true;
      // Validity doesn't depend on the unit, so stop after the first rejection.
      if (!handleUnitCommand(players[i], args)) {
        logf("Unknown or invalid command: '%s'", message.c_str());
        return;
      }
    }
    if (!anyReady) {
      logln("No audio output available, ignoring command");
    }
    return;
  }

  if (unit > config.playerCount) {
    logf("Unit %d is not configured, ignoring command", unit);
    return;
  }
  Player& p = players[unit - 1];
  if (p.state != PlayerState::Ready) {
    logf("[unit %d] %s, ignoring command", unit, unavailableReason(p));
    return;
  }
  if (!handleUnitCommand(p, args)) {
    logf("Unknown or invalid command: '%s'", message.c_str());
  }
}

void handleCommand(const String& message, String* output) {
  commandOutput = output;
  runCommand(message);
  commandOutput = nullptr;
}

// Only touches units still in the Starting state, which commands leave alone.
// Logs straight to Serial, so its lines never end up in a command's output.
void startPlayerAudio() {
  if (sdCardMount()) {
    Serial.println("SD card mounted");
  } else {
    Serial.println(
        "SD card not found, check wiring and card; retrying when a track is "
        "played");
  }
  for (int i = 0; i < config.playerCount; i++) {
    Player& p = players[i];
    if (!audioBeginUnit(i, p.volume)) {
      Serial.printf("[unit %d] Starting I2S output failed\n", p.unit);
      p.state = PlayerState::Failed;
      continue;
    }
    Serial.printf("[unit %d] Audio output ready, volume %d\n", p.unit,
                  p.volume);
    p.state = PlayerState::Ready;
  }
}

void streamPlayerAudio() { audioUpdate(config.playerCount); }

void setupPlayers() {
  setupAudio();
  for (int i = 0; i < config.playerCount; i++) {
    Player& p = players[i];
    p.unit = i + 1;
    p.volume = loadVolume(p);
    p.enabled = loadEnabled(p);
  }
}

static void handlePlayFinished(Player& p, const Track& track) {
  logf("[unit %d] Finished track %s", p.unit, track.toString().c_str());
  p.playing = false;
  if (!p.repeatActive) {
    return;
  }
  if (p.repeatRemaining > 0) {
    p.repeatRemaining--;
    if (p.repeatRemaining == 0) {
      cancelRepeat(p);
    }
  }
  Track next = p.repeatTracks.pick();
  if (p.repeatTracks.count > 1) {
    logf("[unit %d] Repeating with track %s", p.unit,
         describePick(p.repeatTracks, next).c_str());
  }
  startTrack(p, next);
}

static void handlePlayError(Player& p, const AudioEvent& event) {
  String file = trackFileName(event.track, event.format);
  String track = event.track.toString();
  switch (event.type) {
    case AudioEventType::NoCard:
      logf("[unit %d] Can't play track %s: no SD card", p.unit, track.c_str());
      break;
    case AudioEventType::FileNotFound: {
      String names;
      for (int i = 0; i < TRACK_FORMAT_COUNT; i++) {
        names +=
            (i == 0 ? "" : " or ") + trackFileName(event.track, (TrackFormat)i);
      }
      logf("[unit %d] Can't play track %s: no %s found", p.unit, track.c_str(),
           names.c_str());
      break;
    }
    case AudioEventType::BadFormat:
      logf("[unit %d] Can't play track %s: %s can't be decoded", p.unit,
           track.c_str(), file.c_str());
      break;
    default:
      logf("[unit %d] Track %s stopped: error reading %s", p.unit,
           track.c_str(), file.c_str());
      break;
  }
  // A failed play never reports "finished", so a running repeat would silently
  // stall.
  cancelRepeat(p);
  p.playing = false;
}

static void handleAudioEvents() {
  AudioEvent event;
  while (audioPollEvent(event)) {
    Player& p = players[event.index];
    if (event.type == AudioEventType::Finished) {
      handlePlayFinished(p, event.track);
    } else {
      handlePlayError(p, event);
    }
  }
}

static void handleDelayedTrack(Player& p) {
  if (!p.delayedActive || (int32_t)(millis() - p.delayedDue) < 0) {
    return;
  }
  p.delayedActive = false;
  logf("[unit %d] Playing delayed track %s", p.unit,
       p.delayedTrack.toString().c_str());
  startTrack(p, p.delayedTrack);
}

void updatePlayers() {
  handleAudioEvents();
  for (int i = 0; i < config.playerCount; i++) {
    handleDelayedTrack(players[i]);
  }
}
