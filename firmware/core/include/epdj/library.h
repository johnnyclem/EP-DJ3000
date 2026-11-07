// SD-card track library. No ESP-IDF, no heap: the card and the OLED live in the platform layer, which
// feeds this bytes and paints whatever formatScreen writes. Playback is 16-bit PCM WAV, 1 or 2 channels,
// resampled to 48 kHz stereo by holding samples (a 48 kHz file is copied unchanged).
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace library {

constexpr int kMaxTracks = 48;
constexpr int kNameLen = 40;
constexpr uint32_t kOutRate = 48000;

enum class WavError : uint8_t { Ok, Short, NotRiff, NotWave, NoFmt, NotPcm16, BadRate, NoData };

struct WavInfo {
  uint16_t channels = 0;    // 1 or 2
  uint32_t sampleRate = 0;  // Hz, as stored
  uint32_t dataBytes = 0;
  uint32_t dataOffset = 0;  // first PCM byte, from the start of the file
};

// `p` is the start of the file. The data chunk has to begin inside `n` (512 is enough for a normal WAV).
WavError parseWav(const uint8_t* p, size_t n, WavInfo& out);

class Catalog {
 public:
  void clear();
  bool add(const char* name);  // false when full, empty, or longer than kNameLen - 1
  int count() const { return count_; }
  const char* name(int i) const;  // "" when i is out of range

 private:
  char names_[kMaxTracks][kNameLen] = {};
  int count_ = 0;
};

class Browser {
 public:
  void bind(const Catalog* cat);
  int index() const { return index_; }
  int count() const;
  void next();  // wraps; no-op on an empty catalog
  void prev();
  const char* current() const;

 private:
  const Catalog* cat_ = nullptr;
  int index_ = 0;
};

enum class PlayState : uint8_t { Empty, Stopped, Playing, Paused, Ended };

// read: copy up to n bytes, return how many (0 at EOF). seek: absolute, from the start of the file.
using ReadFn = size_t (*)(void* ctx, uint8_t* dst, size_t n);
using SeekFn = bool (*)(void* ctx, uint32_t offset);

class Player {
 public:
  void reset();
  WavError load(ReadFn read, SeekFn seek, void* ctx);  // reads the header, leaves the file at the PCM
  void play();   // from the start if it had ended
  void pause();
  void stop();   // back to the first sample, stopped
  PlayState state() const { return state_; }
  const WavInfo& info() const { return info_; }

  // Stereo int16 at 48 kHz. Returns frames written; 0 when not playing. Sets Ended when the file runs out.
  size_t pull(int16_t* interleaved, size_t frames);

 private:
  bool prime();
  bool readFrame(int16_t& left, int16_t& right);

  ReadFn read_ = nullptr;
  SeekFn seek_ = nullptr;
  void* ctx_ = nullptr;
  WavInfo info_{};
  PlayState state_ = PlayState::Empty;
  uint32_t dataLeft_ = 0;
  uint32_t step_ = 0;     // input frames per output frame, Q16
  uint32_t phase_ = 0;    // Q16
  uint32_t frameAt_ = 0;  // index of curL_/curR_
  int16_t curL_ = 0, curR_ = 0;
  bool primed_ = false;
};

struct Screen {
  char top[22];
  char bottom[22];
};

// Two OLED lines: "3/12  PLAY" and the file name. Empty catalog says "NO CARD".
void formatScreen(Screen& s, const Catalog& cat, int index, PlayState state);

}  // namespace library
}  // namespace epdj
