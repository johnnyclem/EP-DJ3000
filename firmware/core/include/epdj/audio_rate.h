// USB audio (UAC1, asynchronous) rate matching: the USB 1 ms frame clock and the I2S bit clock come from
// different oscillators, so the sample FIFO between them slowly fills or drains. The device tells the host
// how many samples per frame it really consumes (feedback endpoint, FeedbackController). For hosts that
// ignore the feedback, DriftCompensator slips single samples at the I2S end instead.
//
// No heap, no locks, every loop bounded. The ring is the only piece shared between threads.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace audio {

constexpr uint32_t kNominalRateHz = 48000;

// Lock-free single-producer / single-consumer FIFO of stereo int16 frames (interleaved L, R). The producer
// (USB OUT handler) calls write(); the consumer (I2S DMA refill) calls read()/discard(). Indices are
// free-running 32-bit counters, so wrap-around of the counters is harmless: the capacity is a power of two
// and all arithmetic is modular.
class SpscAudioRing {
 public:
  // `storage` must hold capacityFrames * 2 int16 and outlive the ring. capacityFrames must be a power of
  // two >= 2; otherwise valid() is false and the ring behaves as permanently empty and full.
  SpscAudioRing(int16_t* storage, size_t capacityFrames);
  SpscAudioRing(const SpscAudioRing&) = delete;
  SpscAudioRing& operator=(const SpscAudioRing&) = delete;

  bool valid() const { return mask_ != 0; }
  size_t capacity() const { return valid() ? mask_ + 1 : 0; }

  // ---- producer thread ----
  // Copies up to `frames` frames in and returns how many were accepted. It never overwrites unread
  // data; the refused frames are counted in overrunFrames().
  size_t write(const int16_t* interleaved, size_t frames);
  size_t writableFrames() const;

  // ---- consumer thread ----
  // Copies up to `frames` frames out and returns how many were available; the shortfall is counted in
  // underrunFrames(). The caller owns what to do with the rest of its buffer (silence).
  size_t read(int16_t* interleaved, size_t frames);
  size_t discard(size_t frames);
  size_t readableFrames() const;

  // Frames currently buffered, 0..capacity. Callable from either thread; momentarily approximate.
  size_t fillFrames() const;

  uint32_t overrunFrames() const { return overrun_.load(std::memory_order_relaxed); }
  uint32_t underrunFrames() const { return underrun_.load(std::memory_order_relaxed); }

  // Empties the ring and clears the counters. Only while neither thread is inside write/read.
  // `startIndex` seeds the free-running counters (tests use it to exercise the 32-bit wrap).
  void reset(uint32_t startIndex = 0);

 private:
  int16_t* buf_;
  size_t mask_;  // capacity - 1; 0 = invalid
  std::atomic<uint32_t> write_{0};
  std::atomic<uint32_t> read_{0};
  std::atomic<uint32_t> overrun_{0};
  std::atomic<uint32_t> underrun_{0};
};

// Ring with its own storage, for static allocation: StaticSpscAudioRing<2048> ring;
namespace detail {
template <size_t N>
struct RingStorage {
  int16_t samples[N * 2] = {};
};
}  // namespace detail
template <size_t N>
class StaticSpscAudioRing : private detail::RingStorage<N>, public SpscAudioRing {
 public:
  StaticSpscAudioRing() : SpscAudioRing(this->samples, N) {}
};

// Feedback value for the USB feedback endpoint: how many samples per 1 ms frame the device wants.
//
// Input: the ring fill level, sampled once per USB frame (call update() from the SOF handler or the
// feedback endpoint's transfer-complete callback). Control law: PI on the (low-passed) fill error around
// 50% of the ring. The integral term converges to the oscillator mismatch (it IS the measured drift,
// see driftPpm()), the proportional term pulls the fill back after a disturbance. The output is rate
// limited per update and bounded to +/-1% of nominal.
//
// Output format is Q16.16 samples per frame (48 kHz nominal = 48.0 = 0x300000). toUsb10_14() converts to
// what a full-speed UAC1 feedback endpoint sends (10.14, three bytes, little-endian).
//
// Call reset() whenever streaming (re)starts: the integrator must not carry a stale drift estimate across
// a stop, and a stopped stream drains the ring, which would otherwise wind the controller up.
class FeedbackController {
 public:
  struct Config {
    uint32_t sampleRateHz = kNominalRateHz;
    uint32_t capacityFrames = 2048;  // ring size; the target is half of it
    // PI gains in samples/frame of correction per frame of fill error, per 1 ms update. Closed-loop
    // natural frequency ~0.05 Hz, critically damped: slow enough that the +/-32 frame sawtooth of a
    // DMA-block consumer and +/-200 us USB frame jitter do not reach the host as pitch noise.
    float kp = 6.3e-4f;
    float ki = 9.9e-8f;
    uint32_t maxStepQ16 = 131;  // ~0.002 samples/frame per update
  };

  FeedbackController() : FeedbackController(Config()) {}
  explicit FeedbackController(const Config& c);

  void reset();
  // One call per USB frame. Returns the new feedback value (Q16.16 samples/frame).
  uint32_t update(uint32_t fillFrames);

  uint32_t valueQ16() const { return out_; }
  uint32_t nominalQ16() const { return nominal_; }
  uint32_t minQ16() const { return min_; }
  uint32_t maxQ16() const { return max_; }
  // Estimated (host rate / device consumption rate - 1) in ppm, from the integral term: the host sends
  // nominal * (1 + ppm/1e6) when fill is steady.
  float driftPpm() const;

  // Q16.16 samples/frame -> 10.14 (rounded, saturating at 24 bits); the 3 USB bytes, little-endian.
  static uint32_t toUsb10_14(uint32_t q16);
  static void encodeUsb(uint32_t q16, uint8_t out[3]);
  static uint32_t decodeUsb(const uint8_t in[3]);  // back to Q16.16

 private:
  Config cfg_;
  uint32_t nominal_, min_, max_;
  float target_;
  float maxDev_;  // 1% of nominal, samples/frame
  bool primed_ = false;
  float filtered_ = 0.0f;
  float integ_ = 0.0f;  // samples/frame
  uint32_t out_;
};

// Software fallback for hosts that ignore the feedback endpoint (the ring then drifts at the full
// oscillator mismatch). Sits between the ring and the I2S DMA.
//
// Normally a pass-through. When the fill leaves a wide band around the target it starts slipping: it
// removes one frame (replacing two adjacent frames by their average) or inserts one (an interpolated
// frame between the previous output and the next), never more often than once per minSpacingFrames
// (1000 frames = at most 0.1% rate change, ~1.7 cents), and stops once the fill is back near the
// target. Hysteresis keeps it silent whenever feedback is working.
class DriftCompensator {
 public:
  struct Config {
    uint32_t capacityFrames = 2048;
    uint32_t engageFrames = 0;       // start slipping beyond this distance from half full (0 = capacity / 8)
    uint32_t releaseFrames = 0;      // stop within this distance (0 = capacity / 64)
    uint32_t minSpacingFrames = 1000;
  };

  DriftCompensator() : DriftCompensator(Config()) {}
  explicit DriftCompensator(const Config& c);

  // Fills `out` with `frames` stereo frames from the ring. Returns how many are real audio; the rest
  // (an underrun) is zero-filled.
  size_t render(SpscAudioRing& ring, int16_t* out, size_t frames);

  uint32_t droppedFrames() const { return dropped_; }
  uint32_t duplicatedFrames() const { return duplicated_; }
  void reset();

 private:
  uint32_t target_, engage_, release_, spacing_;
  int8_t dir_ = 0;  // +1 = ring too full: drop; -1 = too empty: duplicate
  uint32_t since_;
  int16_t last_[2] = {0, 0};
  uint32_t dropped_ = 0, duplicated_ = 0;
};

}  // namespace audio
}  // namespace epdj
