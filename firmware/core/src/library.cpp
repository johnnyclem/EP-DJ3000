#include "epdj/library.h"

#include <cstdio>
#include <cstring>

namespace epdj {
namespace library {
namespace {

uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
bool tag(const uint8_t* p, char a, char b, char c, char d) {
  return p[0] == static_cast<uint8_t>(a) && p[1] == static_cast<uint8_t>(b) && p[2] == static_cast<uint8_t>(c) &&
         p[3] == static_cast<uint8_t>(d);
}
int16_t s16(const uint8_t* p) {
  return static_cast<int16_t>(rd16(p));
}

}  // namespace

WavError parseWav(const uint8_t* p, size_t n, WavInfo& out) {
  out = WavInfo{};
  if (p == nullptr || n < 12) return WavError::Short;
  if (!tag(p, 'R', 'I', 'F', 'F')) return WavError::NotRiff;
  if (!tag(p + 8, 'W', 'A', 'V', 'E')) return WavError::NotWave;

  bool sawFmt = false;
  uint16_t channels = 0;
  uint32_t rate = 0;
  uint16_t bits = 0;
  size_t at = 12;
  while (at + 8 <= n) {
    const uint32_t sz = rd32(p + at + 4);
    const size_t dataAt = at + 8;
    if (tag(p + at, 'f', 'm', 't', ' ')) {
      if (sz < 16 || dataAt + 16 > n) return WavError::Short;
      if (rd16(p + dataAt) != 1) return WavError::NotPcm16;  // 1 = PCM
      channels = rd16(p + dataAt + 2);
      rate = rd32(p + dataAt + 4);
      bits = rd16(p + dataAt + 14);
      sawFmt = true;
    } else if (tag(p + at, 'd', 'a', 't', 'a')) {
      if (!sawFmt) return WavError::NoFmt;
      if (bits != 16 || (channels != 1 && channels != 2)) return WavError::NotPcm16;
      if (rate < 8000 || rate > 96000) return WavError::BadRate;
      out.channels = channels;
      out.sampleRate = rate;
      out.dataBytes = sz;
      out.dataOffset = static_cast<uint32_t>(dataAt);
      return WavError::Ok;
    }
    const uint32_t step = 8u + sz + (sz & 1u);
    if (step < 8 || at + step < at) return WavError::Short;
    at += step;
  }
  return sawFmt ? WavError::NoData : WavError::NoFmt;
}

void Catalog::clear() {
  count_ = 0;
}

bool Catalog::add(const char* name) {
  if (name == nullptr || name[0] == '\0' || count_ >= kMaxTracks) return false;
  const size_t n = std::strlen(name);
  if (n >= kNameLen) return false;
  std::memcpy(names_[count_], name, n + 1);
  ++count_;
  return true;
}

const char* Catalog::name(int i) const {
  if (i < 0 || i >= count_) return "";
  return names_[i];
}

void Browser::bind(const Catalog* cat) {
  cat_ = cat;
  index_ = 0;
}

int Browser::count() const { return cat_ == nullptr ? 0 : cat_->count(); }

void Browser::next() {
  const int n = count();
  if (n <= 0) return;
  index_ = (index_ + 1) % n;
}

void Browser::prev() {
  const int n = count();
  if (n <= 0) return;
  index_ = (index_ + n - 1) % n;
}

const char* Browser::current() const {
  if (cat_ == nullptr) return "";
  return cat_->name(index_);
}

bool Player::readFrame(int16_t& left, int16_t& right) {
  const uint32_t need = info_.channels == 1 ? 2u : 4u;
  if (dataLeft_ < need || read_ == nullptr) return false;
  uint8_t b[4];
  const size_t n = read_(ctx_, b, need);
  if (n < need) return false;
  dataLeft_ -= need;
  left = s16(b);
  right = info_.channels == 1 ? left : s16(b + 2);
  return true;
}

bool Player::prime() {
  if (seek_ == nullptr || !seek_(ctx_, info_.dataOffset)) return false;
  dataLeft_ = info_.dataBytes;
  phase_ = 0;
  frameAt_ = 0;
  if (!readFrame(curL_, curR_)) return false;
  frameAt_ = 0;
  primed_ = true;
  return true;
}

void Player::reset() {
  *this = Player{};
}

WavError Player::load(ReadFn read, SeekFn seek, void* ctx) {
  reset();
  if (read == nullptr || seek == nullptr) return WavError::Short;
  read_ = read;
  seek_ = seek;
  ctx_ = ctx;
  uint8_t hdr[512];
  const size_t n = read_(ctx_, hdr, sizeof hdr);
  const WavError err = parseWav(hdr, n, info_);
  if (err != WavError::Ok) {
    reset();
    return err;
  }
  step_ = static_cast<uint32_t>((static_cast<uint64_t>(info_.sampleRate) << 16) / kOutRate);
  if (step_ == 0) step_ = 1;
  if (!prime()) {
    reset();
    return WavError::NoData;
  }
  state_ = PlayState::Stopped;
  return WavError::Ok;
}

void Player::play() {
  if (state_ == PlayState::Empty) return;
  if (state_ == PlayState::Ended || state_ == PlayState::Stopped) {
    if (!prime()) {
      state_ = PlayState::Ended;
      return;
    }
  }
  state_ = PlayState::Playing;
}

void Player::pause() {
  if (state_ == PlayState::Playing) state_ = PlayState::Paused;
}

void Player::stop() {
  if (state_ == PlayState::Empty) return;
  if (!prime()) state_ = PlayState::Ended;
  else state_ = PlayState::Stopped;
}

size_t Player::pull(int16_t* interleaved, size_t frames) {
  if (state_ != PlayState::Playing || interleaved == nullptr || frames == 0) return 0;
  size_t produced = 0;
  while (produced < frames) {
    const uint32_t idx = phase_ >> 16;
    while (frameAt_ < idx) {
      if (!readFrame(curL_, curR_)) {
        state_ = PlayState::Ended;
        return produced;
      }
      ++frameAt_;
    }
    interleaved[produced * 2] = curL_;
    interleaved[produced * 2 + 1] = curR_;
    ++produced;
    phase_ += step_;
  }
  return produced;
}

void formatScreen(Screen& s, const Catalog& cat, int index, PlayState state) {
  std::memset(&s, 0, sizeof s);
  if (cat.count() <= 0) {
    std::snprintf(s.top, sizeof s.top, "NO CARD");
    return;
  }
  const char* tag = "";
  if (state == PlayState::Playing) tag = " PLAY";
  else if (state == PlayState::Paused) tag = " PAUSE";
  else if (state == PlayState::Ended) tag = " END";
  char line[40];
  std::snprintf(line, sizeof line, "%d/%d%s", index + 1, cat.count(), tag);
  std::snprintf(s.top, sizeof s.top, "%.*s", static_cast<int>(sizeof s.top - 1), line);
  std::snprintf(s.bottom, sizeof s.bottom, "%.*s", static_cast<int>(sizeof s.bottom - 1), cat.name(index));
}

}  // namespace library
}  // namespace epdj
