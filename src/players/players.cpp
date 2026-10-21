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

static void cancelTransition(Player& p) {
  if (p.transitionPending) {
    logf("[unit %d] Transition %s cancelled", p.unit,
         p.transitionTracks.toString().c_str());
    p.transitionPending = false;
  }
}

static void cancelRepeat(Player& p) {
  p.repeatActive = false;
  p.transitionPending = false;
}

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

// The kinds of argument a unit command takes. None ends a command's list.
enum class Argument : uint8_t {
  None,
  Track,           // a track list, see parseTrackList()
  TrackOptional,
  Number,          // a non-negative number; the command checks its range
  NumberOptional,
};

const int MAX_COMMAND_ARGS = 2;

// The arguments of a unit command, by kind in the order they were given: the
// first track list is tracks[0], the first number numbers[0]. A left out
// optional track list is empty, a left out optional number 0.
struct CommandArgs {
  TrackList tracks[MAX_COMMAND_ARGS];
  int numbers[MAX_COMMAND_ARGS] = {};
};

// A command for one unit. run() returns false if the arguments are invalid,
// and must check that before changing anything; then it calls beginCommand()
// or beginPlayback().
struct UnitCommand {
  const char* name;
  Argument args[MAX_COMMAND_ARGS];
  bool (*run)(Player& p, const CommandArgs& args);
};

// Every valid command cancels a pending delayed track and a pending transition.
static void beginCommand(Player& p) {
  cancelDelayed(p);
  cancelTransition(p);
}

// Like beginCommand(), for commands that start playback. Returns false if the
// unit is disabled, which the command then ignores.
static bool beginPlayback(Player& p) {
  beginCommand(p);
  return playbackEnabled(p);
}

static bool runPlay(Player& p, const CommandArgs& args) {
  if (!beginPlayback(p)) {
    return true;
  }
  Track track = args.tracks[0].pick();
  logf("[unit %d] Playing track %s", p.unit,
       describePick(args.tracks[0], track).c_str());
  cancelRepeat(p);
  startTrack(p, track);
  return true;
}

static bool runDelayed(Player& p, const CommandArgs& args) {
  int delay = args.numbers[0];
  if (delay > MAX_DELAY_MS) {
    return false;
  }
  if (!beginPlayback(p)) {
    return true;
  }
  Track track = args.tracks[0].pick();
  logf("[unit %d] Playing track %s in %d ms", p.unit,
       describePick(args.tracks[0], track).c_str(), delay);
  cancelRepeat(p);
  p.delayedActive = true;
  p.delayedTrack = track;
  p.delayedDue = millis() + delay;
  return true;
}

static bool runLoop(Player& p, const CommandArgs& args) {
  if (!beginPlayback(p)) {
    return true;
  }
  Track track = args.tracks[0].pick();
  logf("[unit %d] Looping track %s", p.unit,
       describePick(args.tracks[0], track).c_str());
  cancelRepeat(p);
  startTrack(p, track, true);
  return true;
}

// A count of 0 repeats until stopped.
static bool runRepeat(Player& p, const CommandArgs& args) {
  int count = args.numbers[0];
  if (count > MAX_REPEAT_COUNT) {
    return false;
  }
  if (!beginPlayback(p)) {
    return true;
  }
  const TrackList& tracks = args.tracks[0];
  Track track = tracks.pick();
  String description = describePick(tracks, track);
  if (count == 0) {
    logf("[unit %d] Repeating indefinitely, starting with track %s", p.unit,
         description.c_str());
  } else {
    logf("[unit %d] Playing %d time(s), starting with track %s", p.unit, count,
         description.c_str());
  }
  p.repeatActive = true;
  p.repeatTracks = tracks;
  p.repeatRemaining = count == 0 ? 0 : count - 1;
  startTrack(p, track);
  if (count == 1) {
    cancelRepeat(p);
  }
  return true;
}

static bool runReplace(Player& p, const CommandArgs& args) {
  beginCommand(p);
  if (!p.repeatActive) {
    logf("[unit %d] Nothing repeating, ignoring replace", p.unit);
    return true;
  }
  const TrackList& tracks = args.tracks[0];
  const TrackList& transition = args.tracks[1];
  const char* kind = tracks.count > 1 ? "a track picked from" : "track";
  if (transition.count > 0) {
    logf("[unit %d] Next repeat will play transition %s, then %s %s", p.unit,
         transition.toString().c_str(), kind, tracks.toString().c_str());
    p.transitionPending = true;
    p.transitionTracks = transition;
  } else {
    logf("[unit %d] Next repeat will play %s %s", p.unit, kind,
         tracks.toString().c_str());
  }
  p.repeatTracks = tracks;
  return true;
}

static bool runStop(Player& p, const CommandArgs&) {
  beginCommand(p);
  logf("[unit %d] Stopping playback", p.unit);
  stopPlayer(p);
  return true;
}

static bool runDisable(Player& p, const CommandArgs&) {
  beginCommand(p);
  logf("[unit %d] Disabling playback", p.unit);
  stopPlayer(p);
  p.enabled = false;
  saveEnabled(p, false);
  return true;
}

static bool runEnable(Player& p, const CommandArgs&) {
  beginCommand(p);
  logf("[unit %d] Enabling playback", p.unit);
  p.enabled = true;
  saveEnabled(p, true);
  return true;
}

static bool runVolume(Player& p, const CommandArgs& args) {
  int volume = args.numbers[0];
  if (volume > MAX_VOLUME) {
    return false;
  }
  beginCommand(p);
  logf("[unit %d] Setting volume to %d", p.unit, volume);
  audioSetVolume(p.unit - 1, volume);
  p.volume = volume;
  saveVolume(p, volume);
  return true;
}

static const UnitCommand UNIT_COMMANDS[] = {
    {"play", {Argument::Track}, runPlay},
    {"delayed", {Argument::Track, Argument::Number}, runDelayed},
    {"loop", {Argument::Track}, runLoop},
    {"repeat", {Argument::Track, Argument::NumberOptional}, runRepeat},
    {"replace", {Argument::Track, Argument::TrackOptional}, runReplace},
    {"stop", {}, runStop},
    {"disable", {}, runDisable},
    {"enable", {}, runEnable},
    {"volume", {Argument::Number}, runVolume},
};

static const UnitCommand* findUnitCommand(const String& name) {
  for (const UnitCommand& command : UNIT_COMMANDS) {
    if (name == command.name) {
      return &command;
    }
  }
  return nullptr;
}

// Parses the words after the command name as the command's argument list
// describes. Returns false if they don't match.
static bool parseCommandArgs(const UnitCommand& command, const ArgList& args,
                             CommandArgs& out) {
  int next = 1;
  int trackCount = 0;
  int numberCount = 0;
  for (Argument kind : command.args) {
    if (kind == Argument::None) {
      break;
    }
    bool optional =
        kind == Argument::TrackOptional || kind == Argument::NumberOptional;
    if (next >= args.argCount) {
      if (!optional) {
        return false;
      }
      continue;
    }
    const String& word = args.arguments[next++];
    if (kind == Argument::Track || kind == Argument::TrackOptional) {
      if (!parseTrackList(word, out.tracks[trackCount++])) {
        return false;
      }
    } else if (!parseNumber(word, 0, INT_MAX, out.numbers[numberCount++])) {
      return false;
    }
  }
  return next == args.argCount;
}

// Runs one command on one unit; args.arguments[0] is the command name in lower
// case. Returns false if the command or its arguments are invalid.
static bool handleUnitCommand(Player& p, const ArgList& args) {
  const UnitCommand* command = findUnitCommand(args.arguments[0]);
  CommandArgs parsed;
  if (command == nullptr || !parseCommandArgs(*command, args, parsed)) {
    return false;
  }
  return command->run(p, parsed);
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
  // The transition plays in between and leaves the count alone.
  if (p.transitionPending) {
    p.transitionPending = false;
    Track transition = p.transitionTracks.pick();
    logf("[unit %d] Playing transition track %s", p.unit,
         describePick(p.transitionTracks, transition).c_str());
    startTrack(p, transition);
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
