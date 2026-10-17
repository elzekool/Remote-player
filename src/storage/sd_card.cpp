#include "storage/sd_card.h"

#include <SD.h>
#include <SPI.h>
#include <pico/mutex.h>

#include "pins.h"

// SPI clock for the SD card: the maximum the SD spec allows in SPI mode, and
// exactly reachable from the 150 MHz system clock (150 / 6). The CPU waits
// while the card transfers, so a faster clock means less load. Lower it (e.g.
// to 18) if long or breadboarded wires give read errors.
static const uint32_t SD_SPI_MHZ = 25;

static recursive_mutex_t lock;
static volatile bool mounted = false;
// Guarded by the lock.
static String writePath;

void setupSdCard() {
  recursive_mutex_init(&lock);
  SPI.setRX(SD_PIN_MISO);
  SPI.setSCK(SD_PIN_SCK);
  SPI.setTX(SD_PIN_MOSI);
}

void sdCardLock() { recursive_mutex_enter_blocking(&lock); }

void sdCardUnlock() { recursive_mutex_exit(&lock); }

bool sdCardLockedByCaller() {
  // Only the owner sets the owner to its own id, so this read needs no lock.
  return lock.owner == lock_get_caller_owner_id();
}

void sdCardRequireLock(const char* functionName) {
  if (sdCardLockedByCaller()) {
    return;
  }
  // A missing lock is a logic error that can corrupt the card, so stop here.
  Serial.printf("%s() called without holding the SD card lock\n", functionName);
  Serial.flush();
  panic("%s() called without holding the SD card lock", functionName);
}

bool sdCardMount() {
  SdCardLock guard;
  SD.end();
  mounted = SD.begin(SD_PIN_CS, SD_SPI_MHZ * 1000000UL);
  return mounted;
}

bool sdCardMounted() { return mounted; }

void sdCardSetWritePath(const String& path) {
  SD_CARD_REQUIRE_LOCK();
  writePath = path;
}

bool sdCardIsBeingWritten(const String& path) {
  SD_CARD_REQUIRE_LOCK();
  return !writePath.isEmpty() && path.equalsIgnoreCase(writePath);
}

bool sdCardWriteInProgress() {
  SD_CARD_REQUIRE_LOCK();
  return !writePath.isEmpty();
}

String sdCardNameProblem(const String& name) {
  if (name.isEmpty()) {
    return "the name is empty";
  }
  if (name.length() > 255) {
    return "the name is longer than 255 characters";
  }
  if (name == "." || name == "..") {
    return "\".\" and \"..\" are reserved";
  }
  for (char c : name) {
    if ((unsigned char)c < 32) {
      return "control characters aren't allowed";
    }
    if (strchr("\"*/:<>?\\|", c) != nullptr) {
      return String("the character ") + c + " isn't allowed";
    }
  }
  if (name[0] == ' ' || name.endsWith(" ") || name.endsWith(".")) {
    return "the name can't start with a space or end with a space or a dot";
  }
  // Device names are reserved with any extension too, e.g. "con.wav".
  int dot = name.indexOf('.');
  String base = dot < 0 ? name : name.substring(0, dot);
  base.toUpperCase();
  bool numberedDevice = base.length() == 4 &&
                        (base.startsWith("COM") || base.startsWith("LPT")) &&
                        base[3] >= '1' && base[3] <= '9';
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
      numberedDevice) {
    return base + " is a reserved device name";
  }
  return "";
}

bool sdCardRename(const String& from, const String& to) {
  SD_CARD_REQUIRE_LOCK();
  if (!from.equalsIgnoreCase(to)) {
    return SD.rename(from, to);
  }
  // FAT ignores case, so a case-only rename goes through a temporary name.
  const char* temporary = "/~rename.tmp";
  if (!SD.rename(from, temporary)) {
    return false;
  }
  if (SD.rename(temporary, to)) {
    return true;
  }
  SD.rename(temporary, from);
  return false;
}
