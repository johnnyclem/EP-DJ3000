#include "epdj/audio_rate.h"

#include <cstring>

namespace epdj {
namespace audio {

// ---- SpscAudioRing ------------------------------------------------------------------------------

namespace {
constexpr size_t kFrameBytes = 2 * sizeof(int16_t);
}

SpscAudioRing::SpscAudioRing(int16_t* storage, size_t capacityFrames) : buf_(storage), mask_(0) {
  const bool pow2 = capacityFrames >= 2 && (capacityFrames & (capacityFrames - 1)) == 0;
  if (storage != nullptr && pow2 && capacityFrames <= (1u << 30)) mask_ = capacityFrames - 1;
}

size_t SpscAudioRing::writableFrames() const {
  if (!valid()) return 0;
  const uint32_t w = write_.load(std::memory_order_relaxed);
  const uint32_t r = read_.load(std::memory_order_acquire);
  return capacity() - static_cast<size_t>(w - r);
}

size_t SpscAudioRing::readableFrames() const {
  if (!valid()) return 0;
  const uint32_t r = read_.load(std::memory_order_relaxed);
  const uint32_t w = write_.load(std::memory_order_acquire);
  return static_cast<size_t>(w - r);
}

size_t SpscAudioRing::fillFrames() const {
  if (!valid()) return 0;
  const uint32_t r = read_.load(std::memory_order_acquire);
  const uint32_t w = write_.load(std::memory_order_acquire);
  const size_t fill = static_cast<size_t>(w - r);
  return fill > capacity() ? capacity() : fill;
}

size_t SpscAudioRing::write(const int16_t* in, size_t frames) {
  if (!valid() || frames == 0) return 0;
  const uint32_t w = write_.load(std::memory_order_relaxed);
  const uint32_t r = read_.load(std::memory_order_acquire);
  const size_t room = capacity() - static_cast<size_t>(w - r);
  const size_t n = frames < room ? frames : room;
  if (n < frames) {  // single writer (the producer): no read-modify-write atomic needed
    overrun_.store(overrun_.load(std::memory_order_relaxed) + static_cast<uint32_t>(frames - n), std::memory_order_relaxed);
  }
  if (n == 0) return 0;
  const size_t start = w & mask_;
  const size_t first = n < capacity() - start ? n : capacity() - start;
  std::memcpy(buf_ + start * 2, in, first * kFrameBytes);
  if (n > first) std::memcpy(buf_, in + first * 2, (n - first) * kFrameBytes);
  write_.store(w + static_cast<uint32_t>(n), std::memory_order_release);
  return n;
}

size_t SpscAudioRing::read(int16_t* out, size_t frames) {
  if (!valid() || frames == 0) return 0;
  const uint32_t r = read_.load(std::memory_order_relaxed);
  const uint32_t w = write_.load(std::memory_order_acquire);
  const size_t avail = static_cast<size_t>(w - r);
  const size_t n = frames < avail ? frames : avail;
  if (n < frames) {  // single writer (the consumer)
    underrun_.store(underrun_.load(std::memory_order_relaxed) + static_cast<uint32_t>(frames - n), std::memory_order_relaxed);
  }
  if (n == 0) return 0;
  const size_t start = r & mask_;
  const size_t first = n < capacity() - start ? n : capacity() - start;
  std::memcpy(out, buf_ + start * 2, first * kFrameBytes);
  if (n > first) std::memcpy(out + first * 2, buf_, (n - first) * kFrameBytes);
  read_.store(r + static_cast<uint32_t>(n), std::memory_order_release);
  return n;
}

size_t SpscAudioRing::discard(size_t frames) {
  if (!valid() || frames == 0) return 0;
  const uint32_t r = read_.load(std::memory_order_relaxed);
  const uint32_t w = write_.load(std::memory_order_acquire);
  const size_t avail = static_cast<size_t>(w - r);
  const size_t n = frames < avail ? frames : avail;
  read_.store(r + static_cast<uint32_t>(n), std::memory_order_release);
  return n;
}

void SpscAudioRing::reset(uint32_t startIndex) {
  write_.store(startIndex, std::memory_order_relaxed);
  read_.store(startIndex, std::memory_order_relaxed);
  overrun_.store(0, std::memory_order_relaxed);
  underrun_.store(0, std::memory_order_relaxed);
}

// ---- FeedbackController -------------------------------------------------------------------------

namespace {
constexpr float kFillAlpha = 1.0f / 32.0f;  // low-pass on the fill reading, per 1 ms update
constexpr float kMaxDeviation = 0.01f;
}  // namespace

FeedbackController::FeedbackController(const Config& c) : cfg_(c) {
  // samples per 1 ms frame = rate / 1000, in Q16.16, rounded
  nominal_ = static_cast<uint32_t>((static_cast<uint64_t>(c.sampleRateHz) * 65536u + 500u) / 1000u);
  const uint32_t dev = static_cast<uint32_t>(static_cast<float>(nominal_) * kMaxDeviation);
  min_ = nominal_ - dev;
  max_ = nominal_ + dev;
  target_ = static_cast<float>(c.capacityFrames) * 0.5f;
  maxDev_ = static_cast<float>(c.sampleRateHz) * 0.001f * kMaxDeviation;
  out_ = nominal_;
}

void FeedbackController::reset() {
  primed_ = false;
  filtered_ = 0.0f;
  integ_ = 0.0f;
  out_ = nominal_;
}

float FeedbackController::driftPpm() const {
  return integ_ / (static_cast<float>(nominal_) / 65536.0f) * 1e6f;
}

uint32_t FeedbackController::update(uint32_t fillFrames) {
  const float f = static_cast<float>(fillFrames);
  if (!primed_) {
    filtered_ = f;
    primed_ = true;
  } else {
    filtered_ += (f - filtered_) * kFillAlpha;
  }
  const float err = filtered_ - target_;  // > 0: ring too full, ask the host for less
  integ_ -= cfg_.ki * err;
  integ_ = integ_ > maxDev_ ? maxDev_ : (integ_ < -maxDev_ ? -maxDev_ : integ_);
  float u = integ_ - cfg_.kp * err;
  u = u > maxDev_ ? maxDev_ : (u < -maxDev_ ? -maxDev_ : u);

  const float uq = u * 65536.0f;
  const int32_t want = static_cast<int32_t>(nominal_) + static_cast<int32_t>(uq + (uq >= 0.0f ? 0.5f : -0.5f));
  int32_t step = want - static_cast<int32_t>(out_);
  const int32_t lim = static_cast<int32_t>(cfg_.maxStepQ16);
  step = step > lim ? lim : (step < -lim ? -lim : step);
  int32_t next = static_cast<int32_t>(out_) + step;
  next = next < static_cast<int32_t>(min_) ? static_cast<int32_t>(min_)
                                           : (next > static_cast<int32_t>(max_) ? static_cast<int32_t>(max_) : next);
  out_ = static_cast<uint32_t>(next);
  return out_;
}

uint32_t FeedbackController::toUsb10_14(uint32_t q16) {
  const uint64_t v = (static_cast<uint64_t>(q16) + 2u) >> 2;  // 16 -> 14 fractional bits, rounded
  return v > 0xFFFFFFu ? 0xFFFFFFu : static_cast<uint32_t>(v);
}

void FeedbackController::encodeUsb(uint32_t q16, uint8_t out[3]) {
  const uint32_t v = toUsb10_14(q16);
  out[0] = static_cast<uint8_t>(v & 0xFFu);
  out[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  out[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
}

uint32_t FeedbackController::decodeUsb(const uint8_t in[3]) {
  const uint32_t v = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
                     (static_cast<uint32_t>(in[2]) << 16);
  return v << 2;
}

// ---- DriftCompensator ---------------------------------------------------------------------------

DriftCompensator::DriftCompensator(const Config& c) {
  target_ = c.capacityFrames / 2;
  engage_ = c.engageFrames != 0 ? c.engageFrames : c.capacityFrames / 8;
  release_ = c.releaseFrames != 0 ? c.releaseFrames : c.capacityFrames / 64;
  spacing_ = c.minSpacingFrames != 0 ? c.minSpacingFrames : 1;
  since_ = spacing_;
}

void DriftCompensator::reset() {
  dir_ = 0;
  since_ = spacing_;
  last_[0] = last_[1] = 0;
}

namespace {
inline int16_t avg16(int16_t a, int16_t b) { return static_cast<int16_t>((static_cast<int32_t>(a) + b) / 2); }
}  // namespace

size_t DriftCompensator::render(SpscAudioRing& ring, int16_t* out, size_t frames) {
  const int32_t err = static_cast<int32_t>(ring.fillFrames()) - static_cast<int32_t>(target_);
  if (dir_ == 0) {
    if (err > static_cast<int32_t>(engage_)) {
      dir_ = 1;
    } else if (err < -static_cast<int32_t>(engage_)) {
      dir_ = -1;
    }
  } else if ((dir_ > 0 && err <= static_cast<int32_t>(release_)) ||
             (dir_ < 0 && err >= -static_cast<int32_t>(release_))) {
    dir_ = 0;
  }

  size_t done = 0;
  while (done < frames) {
    const size_t room = frames - done;
    if (dir_ != 0 && since_ >= spacing_) {
      if (dir_ > 0 && ring.readableFrames() >= 2) {
        int16_t ab[4];
        ring.read(ab, 2);
        out[done * 2] = last_[0] = avg16(ab[0], ab[2]);
        out[done * 2 + 1] = last_[1] = avg16(ab[1], ab[3]);
        ++done;
        ++dropped_;
        since_ = 0;
        continue;
      }
      if (dir_ < 0 && room >= 2 && ring.readableFrames() >= 1) {
        int16_t b[2];
        ring.read(b, 1);
        out[done * 2] = avg16(last_[0], b[0]);
        out[done * 2 + 1] = avg16(last_[1], b[1]);
        out[done * 2 + 2] = last_[0] = b[0];
        out[done * 2 + 3] = last_[1] = b[1];
        done += 2;
        ++duplicated_;
        since_ = 0;
        continue;
      }
    }
    // Plain copy, stopping where the next slip becomes allowed.
    size_t n = room;
    if (dir_ != 0 && since_ < spacing_ && spacing_ - since_ < n) n = spacing_ - since_;
    const size_t got = ring.read(out + done * 2, n);
    if (got > 0) {
      last_[0] = out[(done + got - 1) * 2];
      last_[1] = out[(done + got - 1) * 2 + 1];
      done += got;
      const size_t grown = since_ + got;
      since_ = grown > spacing_ ? spacing_ : static_cast<uint32_t>(grown);
    }
    if (got < n) break;  // underrun
  }
  const size_t real = done;
  if (real < frames) std::memset(out + real * 2, 0, (frames - real) * kFrameBytes);
  return real;
}

}  // namespace audio
}  // namespace epdj
