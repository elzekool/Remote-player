#pragma once

#include <Arduino.h>

// A track to play, and the files on the SD card it maps to. Tracks live in the
// card root: track 14 is /0014.wav or /0014.mp3, track "hello" is /hello.wav or
// /hello.mp3.

// The supported file types, in the order they are tried.
enum class TrackFormat : uint8_t { Wav, Mp3 };
const int TRACK_FORMAT_COUNT = 2;
extern const char* const TRACK_EXTENSIONS[TRACK_FORMAT_COUNT];

const int MAX_TRACK_NUMBER = 9999;
const int MAX_TRACK_NAME = 32;

// A number, or a name (written in double quotes in commands).
struct Track {
  uint16_t number = 0;                 // 0 for a named track
  char name[MAX_TRACK_NAME + 1] = {};  // empty for a numbered track

  static Track numbered(int number);
  // Fails for names that can't be a file name in the card root.
  static bool named(const String& name, Track& out);
  // The track as written in commands: 4 or "hello".
  String toString() const;
};

// The file name of a track in one format, e.g. "/0014.wav" or "/hello.wav".
String trackFileName(const Track& track, TrackFormat format);
