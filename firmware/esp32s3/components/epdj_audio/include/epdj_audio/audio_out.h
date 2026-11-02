// I2S DAC output fed from USB audio. Threads:
//   producer  USB interrupt (pushFromUsb)           lock-free ring write, no float, no blocking
//   consumer  audio task (core 0)                   I2S writes, rate feedback, volume, test tone
//   control   TinyUSB task / engine task            atomics only
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace audio_out {

constexpr uint32_t kSampleRateHz = 48000;
constexpr size_t kFramesPerBlock = 48;  // one USB frame (1 ms); the audio task moves this much per pass

// Called by the audio task once per block while streaming, with the rate the host should send at, in Q16.16
// samples per USB frame (48.0 = nominal). The USB layer forwards it to the feedback endpoint.
using FeedbackSink = void (*)(uint32_t samplesPerFrameQ16);

struct Stats {
  bool running = false;
  bool streaming = false;
  bool primed = false;        // ring filled to its target since the stream started
  uint32_t fillFrames = 0;
  uint32_t overrunFrames = 0;   // USB sent more than the ring could take
  uint32_t underrunFrames = 0;  // I2S wanted more than the ring had
  uint32_t reprimes = 0;        // times the ring ran dry long enough to re-buffer
  uint32_t driftDropped = 0;    // software rate slips (hosts that ignore the feedback endpoint)
  uint32_t driftDuplicated = 0;
  float driftPpm = 0.0f;        // host rate over DAC rate - 1, as estimated by the feedback controller
  uint32_t feedbackQ16 = 0;
};

// Opens the I2S channel and starts the audio task. False if the I2S driver refuses.
bool start(FeedbackSink sink);
bool running();

// ---- USB side ----
// Interrupt-safe: copies up to `frames` interleaved int16 stereo frames into the ring, returns how many fit.
size_t pushFromUsb(const int16_t* interleaved, size_t frames);
// The host selected (true) or left (false) the streaming alternate setting.
void setStreaming(bool streaming);
// Feature unit state, USB audio units (1/256 dB, 0 = unity) and mute.
void setVolume(int16_t volume256Db, bool mute);

// ---- engine side ----
// Sine on the DAC for the audio-path test; false if audio is not running. Ramps in and out, no clicks.
bool testTone(bool on, uint16_t freqHz, uint8_t attenuationDb);
Stats stats();

}  // namespace audio_out
}  // namespace epdj
