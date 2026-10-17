#include "audio/track.h"

const char* const TRACK_EXTENSIONS[TRACK_FORMAT_COUNT] = {".wav", ".mp3"};

Track Track::numbered(int number) {
  Track track;
  track.number = number;
  return track;
}

bool Track::named(const String& name, Track& out) {
  // FAT drops leading/trailing spaces and trailing dots, so such names would
  // never match.
  if (name.isEmpty() || name.length() > MAX_TRACK_NAME || name[0] == ' ' ||
      name.endsWith(" ") || name.endsWith(".")) {
    return false;
  }
  for (char c : name) {
    if (c < 32 || strchr("/\\:*?\"<>|", c) != nullptr) {
      return false;
    }
  }
  out = Track();
  strcpy(out.name, name.c_str());
  return true;
}

String Track::toString() const {
  return name[0] ? "\"" + String(name) + "\"" : String(number);
}

String trackFileName(const Track& track, TrackFormat format) {
  char base[MAX_TRACK_NAME + 2];
  if (track.name[0]) {
    snprintf(base, sizeof(base), "/%s", track.name);
  } else {
    snprintf(base, sizeof(base), "/%04d", track.number);
  }
  return String(base) + TRACK_EXTENSIONS[(int)format];
}
