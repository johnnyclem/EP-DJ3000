// Sample-level audio helpers for the USB DAC path: the TEST_TONE generator and the UAC volume/mute
// processor. Stereo interleaved int16, no heap, no locks, fixed work per frame.
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace audio {

// Phase-continuous sine for the audio-path test (SysEx TEST_TONE: on/off, frequency, attenuation).
//
// Level changes never step: start and stop ramp the envelope over kRampFrames (5 ms at 48 kHz) with a
// smoothstep curve (zero slope at both ends), the sine always starts at phase 0 from idle, and a change
// of attenuation or frequency while running slews the gain / keeps the phase. Output is L == R.
// Sine source: 256-entry quarter-wave table (built at compile time) with linear interpolation,
// worst-case error ~5e-6 of full scale (about -106 dB).
class ToneGenerator {
 public:
  static constexpr uint32_t kRampFrames = 240;

  explicit ToneGenerator(uint32_t sampleRateHz = 48000) : rate_(sampleRateHz == 0 ? 48000 : sampleRateHz) {}

  // Starts or retunes. freqHz is clamped to 1 Hz .. just under Nyquist; attenuationDb 0 = full scale.
  void start(uint16_t freqHz, uint8_t attenuationDb);
  // Ramps out; active() turns false once silent.
  void stop();
  bool active() const { return running_ || level_ > 0; }

  // Overwrites `frames` stereo frames (all zero while idle).
  void render(int16_t* interleaved, size_t frames);

  // Peak sample value for an attenuation, as used by render() (for tests and level meters).
  static int32_t peakFor(uint8_t attenuationDb);

 private:
  uint32_t rate_;
  bool running_ = false;
  uint32_t phase_ = 0;      // full circle = 2^32
  uint32_t inc_ = 0;
  uint32_t level_ = 0;      // 0..kRampFrames, envelope position
  int32_t gain_ = 0;        // Q15, current
  int32_t gainTarget_ = 0;  // Q15
};

// UAC volume and mute -> smooth per-frame gain, saturating int16 multiply.
//
// Volume is the UAC value in 1/256 dB (0 = unity; INT16_MIN is "-inf dB" = silence); it is clamped to
// +24 dB. Every change of volume or mute moves the gain linearly over kRampFrames, never a step, so a
// host dragging its volume slider or toggling mute produces no zipper noise or clicks. At unity the
// samples pass through bit-exact; muted and settled the buffer is zeroed.
class VolumeProcessor {
 public:
  static constexpr uint32_t kRampFrames = 240;
  static constexpr int16_t kSilence = INT16_MIN;

  VolumeProcessor() = default;

  void setVolume(int16_t volume256Db);
  void setMute(bool muted);
  // The very next process() starts at the target gain instead of ramping (stream start).
  void snapToTarget() { gain_ = target_; }

  // In place, `frames` stereo frames.
  void process(int16_t* interleaved, size_t frames);

  // Q16 linear gain now (65536 = unity) and where it is heading.
  int32_t gainQ16() const { return gain_; }
  int32_t targetQ16() const { return target_; }
  bool settled() const { return gain_ == target_; }

  // 10^(volume256Db / 256 / 20) in Q16, 0 below -100 dB or for kSilence, capped at +24 dB.
  static int32_t gainForVolume(int16_t volume256Db);

 private:
  int16_t volume_ = 0;
  bool muted_ = false;
  int32_t gain_ = 65536;
  int32_t target_ = 65536;
};

}  // namespace audio
}  // namespace epdj
