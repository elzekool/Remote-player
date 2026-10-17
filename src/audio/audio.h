#pragma once

#include <Arduino.h>

#include "audio/track.h"
#include "pins.h"

// Plays tracks from the SD card on up to MAX_AUDIO_UNITS I2S outputs, one per
// amplifier. Every file is decoded (Audio Tools library) and converted to 44.1
// kHz mono, whatever its sample rate and channel count.
//
// Threading: audioBeginUnit() and audioUpdate() do all decoding and output and
// must be called from one thread, the audio thread. The other functions talk to
// it through queues and are safe to call from any thread.

const int AUDIO_MAX_VOLUME = 30;

enum class AudioEventType : uint8_t {
  // The track played to the end (never sent while looping).
  Finished,
  // The SD card could not be mounted.
  NoCard,
  // There is no file for the track on the card, in any supported format.
  FileNotFound,
  // The file can't be decoded.
  BadFormat,
  // Reading from the card failed while playing, e.g. the card was removed.
  ReadError,
};

struct AudioEvent {
  uint8_t index;  // 0-based unit index
  AudioEventType type;
  Track track;
  // The file that was played; not set for NoCard and FileNotFound.
  TrackFormat format;
};

// Creates the queues; call once before any other function here.
void setupAudio();

// --- Control (any thread) ---

// Starts playing a track on a unit, replacing whatever it was playing. With
// loop set the track restarts at its end until the unit is stopped or given
// another track.
void audioPlay(int index, const Track& track, bool loop);
void audioStop(int index);
// Volume runs from 0 (silent) to AUDIO_MAX_VOLUME.
void audioSetVolume(int index, int volume);
// Fetches the next event. Events about a track that has since been replaced or
// stopped are dropped, so a "finished" can never be mistaken for the end of the
// track started after it.
bool audioPollEvent(AudioEvent& event);
// Whether a unit has the given file open, e.g. "/0014.wav"; call with the card
// locked.
bool audioFileInUse(const String& path);
// Share of the audio thread's time spent working in the last second, and the
// highest value since start-up, in percent. Close to 100 means dropouts.
int audioLoad();
int audioPeakLoad();
// The part of audioLoad() spent reading the SD card (the CPU waits during
// transfers).
int audioCardLoad();

// --- Audio thread ---

// Starts the I2S output of a unit (pins from AUDIO_UNIT_PINS) at the given
// volume.
bool audioBeginUnit(int index, int volume);
// Handles requests and feeds audio to the units that are playing; call
// continuously. Each unit has only ~90-140 ms of audio buffered, so the thread
// must not be held up longer.
void audioUpdate(int unitCount);
