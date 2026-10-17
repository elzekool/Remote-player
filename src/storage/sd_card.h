#pragma once

#include <Arduino.h>

// Shared access to the SD card (SD library over SPI, pins in pins.h).
//
// The SD library isn't thread-safe: only use SD, SDFS and their File objects
// while holding the card lock. The lock is recursive. Keep locked sections
// short, since anything streaming from the card waits meanwhile.

// Configures the SPI pins and the lock; call once before any other function
// here.
void setupSdCard();

void sdCardLock();
void sdCardUnlock();

// Whether the calling thread holds the card lock.
bool sdCardLockedByCaller();
// Halts with an error message if the calling thread doesn't hold the card lock.
// Functions that must be called with the card locked check this on entry.
#define SD_CARD_REQUIRE_LOCK() sdCardRequireLock(__func__)
void sdCardRequireLock(const char* function);

// Holds the card lock for the rest of the scope.
class SdCardLock {
 public:
  SdCardLock() { sdCardLock(); }
  ~SdCardLock() { sdCardUnlock(); }
  SdCardLock(const SdCardLock&) = delete;
  SdCardLock& operator=(const SdCardLock&) = delete;
};

// Mounts (or remounts) the card. Remounting invalidates open files, so only
// call this while none are open.
bool sdCardMount();
// Whether the last mount succeeded.
bool sdCardMounted();

// Marks the file being written, e.g. "/0014.wav" (empty for none). While set,
// readers treat that file as absent and the card must not be remounted. Call
// with the card locked.
void sdCardSetWritePath(const String& path);
// Whether path is the file being written (case-insensitive); call with the card
// locked.
bool sdCardIsBeingWritten(const String& path);
// Whether any file is being written; call with the card locked.
bool sdCardWriteInProgress();

// Checks a name for a new file in the card root against the FAT long file name
// rules and the names Windows refuses. Returns what is wrong with it, or an
// empty string if it is fine.
String sdCardNameProblem(const String& name);
// Renames a file, also when only the case changes; call with the card locked.
bool sdCardRename(const String& from, const String& to);
