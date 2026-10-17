#include "audio/audio.h"

#include <SD.h>
#include <pico/mutex.h>
#include <pico/util/queue.h>

// Keep the Audio Tools classes in their own namespace; otherwise names like
// Player clash.
#define USE_AUDIOTOOLS_NS false
#include <AudioTools.h>
#include <AudioTools/AudioCodecs/CodecMP3Helix.h>

#include "storage/sd_card.h"

// Bytes read from the card per unit per round. A WAV read of 4 sectors is one
// multi-sector card command, which saves the per-command overhead. MP3 reads
// stay small: at low bitrates 2 KB would decode to more audio than
// FEED_THRESHOLD_FRAMES.
static const size_t WAV_READ_CHUNK = 2048;
static const size_t MP3_READ_CHUNK = 512;
// DMA buffering per I2S output. 12 buffers of 512 frames hold ~139 ms at 44.1
// kHz, enough to ride out a slow card read while the other units are being fed.
static const int I2S_BUFFERS = 12;
static const int I2S_BUFFER_FRAMES = 512;
// A unit is fed once this much of its buffer is free: enough for what one read
// can produce (a 2 KB mono WAV read, or two decoded MP3 frames), so writing it
// doesn't have to wait. That keeps at least ~87 ms of audio queued.
static const int FEED_THRESHOLD_FRAMES = 2304;
// The CPU load is measured over windows of this length.
static const uint32_t LOAD_WINDOW_US = 1000000;
// Every file is converted to this format, so the I2S outputs never need to be
// reconfigured. Stereo files are mixed down to mono; the I2S driver sends mono
// to both channels.
static const audio_tools::AudioInfo OUTPUT_FORMAT(44100, 1, 16);
static const int QUEUE_LENGTH = 16;

enum class RequestType : uint8_t { Play, Loop, Stop, Volume };

struct AudioRequest {
  uint8_t index;
  RequestType type;
  uint8_t volume;  // for Volume
  Track track;     // for Play and Loop
  uint32_t seq;
};

struct QueuedEvent {
  AudioEvent event;
  uint32_t seq;
};

// Converts the decoded audio to OUTPUT_FORMAT. FormatConverterStream only fully
// applies a new input format in begin(), so it restarts whenever a decoder
// reports the format of a file. It also counts the decoded audio, so a file
// that decodes to nothing can be reported as such.
class OutputConverter : public audio_tools::FormatConverterStream {
 public:
  size_t decodedBytes = 0;

  void setAudioInfo(audio_tools::AudioInfo info) override {
    if (restarting) {
      FormatConverterStream::setAudioInfo(info);
      return;
    }
    if (started && info == audioInfo()) {
      return;
    }
    restarting = true;
    started = begin(info);
    restarting = false;
  }

  size_t write(const uint8_t* data, size_t len) override {
    size_t result = FormatConverterStream::write(data, len);
    decodedBytes += result;
    return result;
  }

 private:
  bool restarting = false;
  bool started = false;
};

// The Audio Tools I2S driver, with access to how much room its output buffer
// has.
class UnitI2SDriver : public audio_tools::I2SDriverRP2040 {
 public:
  int freeFrames() {
    return i2s.availableForWrite() / 4;
  }  // one 32-bit word per frame

  // The library writes mono one sample at a time through several calls; this
  // builds blocks of frames (the sample in both channels) and writes each block
  // at once.
  size_t writeBytes(const void* src, size_t size) override {
    if (cfg.channels != 1 || cfg.bits_per_sample != 16) {
      return I2SDriverRP2040::writeBytes(src, size);
    }
    const int16_t* samples = (const int16_t*)src;
    size_t count = size / sizeof(int16_t);
    uint32_t frames[128];
    for (size_t done = 0; done < count;) {
      size_t n = min(count - done, sizeof(frames) / sizeof(frames[0]));
      for (size_t i = 0; i < n; i++) {
        uint32_t sample = (uint16_t)samples[done + i];
        frames[i] = (sample << 16) | sample;
      }
      const uint8_t* p = (const uint8_t*)frames;
      size_t left = n * sizeof(uint32_t);
      while (left >
             0) {  // only waits if the buffer is full, which the feeding avoids
        size_t written = i2s.write(p, left);
        p += written;
        left -= written;
      }
      done += n;
    }
    return size;
  }
};

// One unit's pipeline: SD file -> decoder for the file's format -> converter ->
// volume -> I2S.
struct AudioUnit {
  UnitI2SDriver i2sDriver;
  audio_tools::I2SStream i2s{i2sDriver};
  audio_tools::VolumeStream volume{i2s};
  OutputConverter converter;
  audio_tools::WAVDecoder wavDecoder;
  audio_tools::MP3DecoderHelix mp3Decoder;
  audio_tools::EncodedAudioOutput output{&converter, &wavDecoder};
  File file;
  // Name of the open file, e.g. "/0014.wav"; empty while nothing plays. Only
  // changed with the card locked, so audioFileInUse() can read it from other
  // threads.
  String path;
  bool playing = false;
  bool loop = false;
  Track track;
  TrackFormat format = TrackFormat::Wav;
  // Sequence number of the request that started the current track; copied into
  // its events.
  uint32_t seq = 0;
  // Where reading stops: the end of the file, or for WAV the end of the sample
  // data.
  uint32_t dataEnd = 0;
};

static AudioUnit units[MAX_AUDIO_UNITS];
static queue_t requests;
static queue_t events;
// Guards requestSeq.
static mutex_t controlLock;
// Sequence number of the latest play or stop request per unit.
static uint32_t requestSeq[MAX_AUDIO_UNITS];
// The audio thread's load in the last window and the highest since start-up,
// and the part of the last window spent reading the SD card, in percent.
static volatile uint8_t load = 0;
static volatile uint8_t peakLoad = 0;
static volatile uint8_t cardLoad = 0;
// Audio thread only.
static uint8_t readBuffer[WAV_READ_CHUNK];
static uint32_t loadWindowStart = 0;
static uint32_t busyUs = 0;
static uint32_t cardBusyUs = 0;

void setupAudio() {
  mutex_init(&controlLock);
  queue_init(&requests, sizeof(AudioRequest), QUEUE_LENGTH);
  queue_init(&events, sizeof(QueuedEvent), QUEUE_LENGTH);
}

// --- Control ---

static void sendRequest(int index, RequestType type,
                        const Track& track = Track(), int volume = 0) {
  mutex_enter_blocking(&controlLock);
  if (type != RequestType::Volume) {
    requestSeq[index]++;
  }
  AudioRequest request = {(uint8_t)index, type, (uint8_t)volume, track,
                          requestSeq[index]};
  // audioUpdate() empties the queue continuously, so it only fills up if the
  // audio thread is stuck.
  bool queued = queue_try_add(&requests, &request);
  mutex_exit(&controlLock);
  if (!queued) {
    Serial.printf("[unit %d] Audio request queue full, request dropped\n",
                  index + 1);
  }
}

void audioPlay(int index, const Track& track, bool loop) {
  sendRequest(index, loop ? RequestType::Loop : RequestType::Play, track);
}

void audioStop(int index) { sendRequest(index, RequestType::Stop); }

void audioSetVolume(int index, int volume) {
  sendRequest(index, RequestType::Volume, Track(), volume);
}

bool audioPollEvent(AudioEvent& event) {
  QueuedEvent queued;
  bool found = false;
  mutex_enter_blocking(&controlLock);
  while (!found && queue_try_remove(&events, &queued)) {
    if (queued.seq == requestSeq[queued.event.index]) {
      event = queued.event;
      found = true;
    }
  }
  mutex_exit(&controlLock);
  return found;
}

bool audioFileInUse(const String& path) {
  SD_CARD_REQUIRE_LOCK();
  for (const AudioUnit& u : units) {
    if (u.path.equalsIgnoreCase(path)) {
      return true;
    }
  }
  return false;
}

int audioLoad() { return load; }

int audioPeakLoad() { return peakLoad; }

int audioCardLoad() { return cardLoad; }

// --- Audio thread ---

static float volumeFactor(int volume) {
  return (float)constrain(volume, 0, AUDIO_MAX_VOLUME) / AUDIO_MAX_VOLUME;
}

static void postEvent(int index, AudioEventType type) {
  const AudioUnit& u = units[index];
  QueuedEvent queued = {{(uint8_t)index, type, u.track, u.format}, u.seq};
  if (!queue_try_add(&events, &queued)) {
    Serial.printf("[unit %d] Audio event queue full, event dropped\n",
                  index + 1);
  }
}

bool audioBeginUnit(int index, int volume) {
  AudioUnit& u = units[index];
  const I2sPins& pins = AUDIO_UNIT_PINS[index];
  auto cfg = u.i2s.defaultConfig(audio_tools::TX_MODE);
  cfg.copyFrom(OUTPUT_FORMAT);
  cfg.pin_bck = pins.bclk;
  cfg.pin_ws = pins.lrclk;
  cfg.pin_data = pins.din;
  cfg.buffer_count = I2S_BUFFERS;
  cfg.buffer_size = I2S_BUFFER_FRAMES;
  if (!u.i2s.begin(cfg)) {
    return false;
  }
  u.volume.begin(OUTPUT_FORMAT);
  u.volume.setVolume(volumeFactor(volume));
  u.converter.setOutput(static_cast<Print&>(u.volume));
  u.converter.setAudioInfoOut(OUTPUT_FORMAT);
  // Write straight through, so the end of a track isn't held back in a buffer.
  u.converter.setBuffered(false);
  return true;
}

static bool anyPlaying() {
  for (const AudioUnit& u : units) {
    if (u.playing) {
      return true;
    }
  }
  return false;
}

static void closeTrack(AudioUnit& u) {
  SdCardLock guard;
  if (u.playing) {
    u.output.end();
    u.file.close();
    u.playing = false;
  }
  u.path = "";
}

// Tries each format in turn; call with the card locked.
static bool openTrackFile(AudioUnit& u) {
  SD_CARD_REQUIRE_LOCK();
  for (int i = 0; i < TRACK_FORMAT_COUNT; i++) {
    u.format = (TrackFormat)i;
    String path = trackFileName(u.track, u.format);
    if (sdCardIsBeingWritten(path)) {
      continue;
    }
    u.file = SD.open(path, "r");
    if (u.file) {
      u.path = path;
      return true;
    }
  }
  return false;
}

// Opens the unit's track; on failure posts the reason and returns false. Call
// with the card locked.
static bool openTrack(int index) {
  SD_CARD_REQUIRE_LOCK();
  AudioUnit& u = units[index];
  if (sdCardMounted() && openTrackFile(u)) {
    return true;
  }
  // The card may have been inserted or swapped since it was mounted, so mount
  // it again and retry. Remounting breaks the files other units are reading, so
  // only while nothing plays (and nothing is being written).
  if (!sdCardWriteInProgress() && (!sdCardMounted() || !anyPlaying()) &&
      sdCardMount() && openTrackFile(u)) {
    return true;
  }
  postEvent(index, sdCardMounted() ? AudioEventType::FileNotFound
                                   : AudioEventType::NoCard);
  return false;
}

static uint32_t readUint32LE(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Returns the end of a WAV file's sample data, so chunks after it (e.g. LIST
// metadata) aren't played as noise; returns the file size if the file can't be
// walked. Leaves the file at its start. Checking the format itself is left to
// the decoder. Call with the card locked.
static uint32_t wavDataEnd(File& file) {
  SD_CARD_REQUIRE_LOCK();
  uint32_t size = file.size();
  uint32_t end = size;
  uint8_t chunk[12];
  if (file.read(chunk, 12) == 12 && memcmp(chunk, "RIFF", 4) == 0 &&
      memcmp(chunk + 8, "WAVE", 4) == 0) {
    uint32_t position = 12;
    while (position + 8 <= size && file.seek(position) &&
           file.read(chunk, 8) == 8) {
      uint32_t length = readUint32LE(chunk + 4);
      if (memcmp(chunk, "data", 4) == 0) {
        // Streamed recordings may state 0 or a too large length; play those to
        // the end.
        if (length > 0 && length <= size - position - 8) {
          end = position + 8 + length;
        }
        break;
      }
      // Chunks are padded to an even length.
      position += 8 + length + (length & 1);
    }
  }
  file.seek(0);
  return end;
}

// Starts decoding the open file from its beginning.
static void beginDecoding(AudioUnit& u) {
  u.output.end();
  sdCardLock();
  u.file.seek(0);
  sdCardUnlock();
  u.converter.decodedBytes = 0;
  u.output.begin();
}

static void startTrack(int index, const AudioRequest& request) {
  AudioUnit& u = units[index];
  closeTrack(u);
  u.seq = request.seq;
  u.track = request.track;
  u.loop = request.type == RequestType::Loop;
  sdCardLock();
  bool opened = openTrack(index);
  if (opened) {
    u.dataEnd =
        u.format == TrackFormat::Wav ? wavDataEnd(u.file) : u.file.size();
  }
  sdCardUnlock();
  if (!opened) {
    return;
  }
  u.output.setDecoder(u.format == TrackFormat::Wav
                          ? (audio_tools::AudioDecoder*)&u.wavDecoder
                          : &u.mp3Decoder);
  beginDecoding(u);
  u.playing = true;
}

static void handleRequests(int unitCount) {
  AudioRequest request;
  while (queue_try_remove(&requests, &request)) {
    if (request.index >= unitCount) {
      continue;
    }
    AudioUnit& u = units[request.index];
    switch (request.type) {
      case RequestType::Play:
      case RequestType::Loop:
        startTrack(request.index, request);
        break;
      case RequestType::Stop:
        closeTrack(u);
        u.seq = request.seq;
        break;
      case RequestType::Volume:
        u.volume.setVolume(volumeFactor(request.volume));
        break;
    }
  }
}

static void endOfTrack(int index) {
  AudioUnit& u = units[index];
  if (u.converter.decodedBytes == 0) {
    closeTrack(u);
    postEvent(index, AudioEventType::BadFormat);
  } else if (u.loop) {
    beginDecoding(u);
  } else {
    closeTrack(u);
    postEvent(index, AudioEventType::Finished);
  }
}

// Feeds one chunk of a playing unit's file to its pipeline.
static void feedUnit(int index) {
  AudioUnit& u = units[index];
  uint32_t chunk =
      u.format == TrackFormat::Wav ? WAV_READ_CHUNK : MP3_READ_CHUNK;
  uint32_t readStart = time_us_32();
  sdCardLock();
  uint32_t position = u.file.position();
  size_t length = position >= u.dataEnd ? 0 : min(chunk, u.dataEnd - position);
  int read = length > 0 ? u.file.read(readBuffer, length) : 0;
  sdCardUnlock();
  cardBusyUs += time_us_32() - readStart;
  if (read < 0) {
    closeTrack(u);
    postEvent(index, AudioEventType::ReadError);
    return;
  }
  if (read == 0) {
    endOfTrack(index);
    return;
  }
  // Decoders that recognise an unsupported file refuse the data (others decode
  // it to nothing, which endOfTrack() catches).
  if (u.output.write(readBuffer, read) == 0) {
    closeTrack(u);
    postEvent(index, AudioEventType::BadFormat);
  }
}

// Adds a pass's working time to the load window and publishes the load when it
// is complete.
static void measureLoad(uint32_t start, uint32_t end) {
  busyUs += end - start;
  if (end - loadWindowStart < LOAD_WINDOW_US) {
    return;
  }
  uint32_t window = end - loadWindowStart;
  load = min((uint32_t)((uint64_t)busyUs * 100 / window), (uint32_t)100);
  peakLoad = max(peakLoad, load);
  cardLoad =
      min((uint32_t)((uint64_t)cardBusyUs * 100 / window), (uint32_t)100);
  loadWindowStart = end;
  busyUs = 0;
  cardBusyUs = 0;
}

// Feeds each playing unit whose buffer has room; a unit with a full buffer is
// skipped until it has played some of it. Time spent waiting for room counts as
// idle in the load figure.
void audioUpdate(int unitCount) {
  uint32_t start = time_us_32();
  handleRequests(unitCount);
  bool anyPlaying = false;
  bool fed = false;
  for (int i = 0; i < unitCount; i++) {
    AudioUnit& u = units[i];
    if (!u.playing) {
      continue;
    }
    anyPlaying = true;
    if (u.i2sDriver.freeFrames() >= FEED_THRESHOLD_FRAMES) {
      feedUnit(i);
      fed = true;
    }
  }
  measureLoad(start, time_us_32());
  if (!anyPlaying) {
    delay(1);
  } else if (!fed) {
    // All buffers are full; one I2S buffer lasts ~11.6 ms, so check again
    // shortly.
    delayMicroseconds(200);
  }
}
