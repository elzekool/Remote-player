#pragma once

#include <Arduino.h>

#include "audio/audio.h"

// The audio units and the commands that control them. Each unit is an I2S
// amplifier with its own speaker, playing tracks from the shared SD card.
//
// Threading: commands and updatePlayers() change the players' state without
// locking, so call them from one thread. startPlayerAudio() and
// streamPlayerAudio() may run on another thread.
const int MAX_PLAYERS = MAX_AUDIO_UNITS;

enum class PlayerState : uint8_t { Starting, Ready, Failed };

// Limits of the "repeat" count and the "delayed" delay.
const int MAX_REPEAT_COUNT = 9999;
const int MAX_DELAY_MS = 5000;

// The tracks given to a command as 4, "hello" or a comma separated list like
// 1,"hello",6,"4". Each time the command plays a track, it picks one of them at
// random.
const int MAX_TRACK_CHOICES = 16;
struct TrackList {
  Track tracks[MAX_TRACK_CHOICES];
  uint8_t count = 0;

  const Track& pick() const {
    return tracks[count > 1 ? rp2040.hwrand32() % count : 0];
  }
  // Formats the list as it is written in commands, e.g. 1,"hello",6.
  String toString() const {
    String s;
    for (int i = 0; i < count; i++) {
      if (i > 0) {
        s += ',';
      }
      s += tracks[i].toString();
    }
    return s;
  }
};

struct Player {
  int unit;
  // Set by startPlayerAudio(); the unit only accepts commands once Ready.
  volatile PlayerState state = PlayerState::Starting;
  // State of the "repeat" command. Every repetition picks a new track from
  // repeatTracks. repeatRemaining counts the plays still to start; 0 means
  // forever.
  bool repeatActive = false;
  TrackList repeatTracks;
  int repeatRemaining = 0;
  // Set by "replace" with a transition: when the current track finishes, a
  // track from transitionTracks plays before the repeat continues. It doesn't
  // count towards repeatRemaining, and any later command cancels it.
  bool transitionPending = false;
  TrackList transitionTracks;
  // State of the "delayed" command: delayedTrack starts at delayedDue unless
  // another command for the unit comes in first.
  bool delayedActive = false;
  Track delayedTrack;
  uint32_t delayedDue = 0;
  // Set by "enable"/"disable" and saved; while false the unit refuses to start
  // playback.
  bool enabled = true;
  int volume = 0;
  // What the unit is playing, kept up to date from the commands and the audio
  // events. looping is set by the "loop" command, which plays nowPlaying until
  // stopped.
  bool playing = false;
  bool looping = false;
  Track nowPlaying;
};

extern Player players[MAX_PLAYERS];

// Loads each configured unit's saved settings; call once before the other
// functions.
void setupPlayers();
// Mounts the SD card and starts each configured unit's audio output; each
// unit's state then changes from Starting to Ready or Failed. Call from the
// thread that runs streamPlayerAudio().
void startPlayerAudio();
// Decodes and outputs the audio of all units; call continuously (see
// audioUpdate()).
void streamPlayerAudio();
// Handles finished tracks and playback errors, continues repeats and starts
// delayed tracks that are due; call continuously.
void updatePlayers();
// Executes a command of the form "[<unit>:]<command> [args]". Log lines go to
// the serial port and, when output is given, are also appended to it (one line
// per message).
void handleCommand(const String& message, String* output = nullptr);
