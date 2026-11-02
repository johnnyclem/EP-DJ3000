#include "epdj_audio/audio_out.h"

#include <atomic>
#include <cstring>

#include "driver/i2s_std.h"
#include "epdj/audio_dsp.h"
#include "epdj/audio_rate.h"
#include "epdj_hal/board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace epdj {
namespace audio_out {
namespace {

const char* TAG = "audio";

constexpr size_t kRingFrames = CONFIG_EPDJ_AUDIO_RING_FRAMES;
static_assert((kRingFrames & (kRingFrames - 1)) == 0 && kRingFrames >= 512, "ring must be a power of two >= 512");
constexpr size_t kBlock = kFramesPerBlock;
constexpr uint32_t kDmaDescriptors = 8;  // 8 x 1 ms of slack between the task and the I2S hardware
constexpr uint32_t kEmptyBlocksBeforeReprime = 4;

audio::StaticSpscAudioRing<kRingFrames> g_ring;
audio::FeedbackController::Config feedbackConfig() {
  audio::FeedbackController::Config c;
  c.sampleRateHz = kSampleRateHz;
  c.capacityFrames = kRingFrames;
  return c;
}
audio::DriftCompensator::Config driftConfig() {
  audio::DriftCompensator::Config c;
  c.capacityFrames = kRingFrames;
  return c;
}
audio::FeedbackController g_feedback(feedbackConfig());
audio::DriftCompensator g_drift(driftConfig());
audio::VolumeProcessor g_volume;
audio::ToneGenerator g_tone(kSampleRateHz);

i2s_chan_handle_t g_i2s = nullptr;
FeedbackSink g_sink = nullptr;
bool g_started = false;

// Control state written by other tasks, read once per block by the audio task.
std::atomic<bool> g_streaming{false};
std::atomic<int32_t> g_volumeSetting{0};
std::atomic<bool> g_muteSetting{false};
std::atomic<uint32_t> g_toneCmd{0};  // packTone(): changes on every request
uint32_t g_toneGen = 0;               // engine task only

// Published by the audio task for stats().
std::atomic<bool> g_primed{false};
std::atomic<uint32_t> g_reprimes{0};
std::atomic<uint32_t> g_fbOut{0};
std::atomic<uint32_t> g_driftPpmBits{0};
std::atomic<uint32_t> g_fill{0};

// on:1 | freq:16 | attenuation:8 | generation:7
uint32_t packTone(bool on, uint16_t freq, uint8_t atten, uint32_t gen) {
  return (on ? 1u : 0u) | (static_cast<uint32_t>(freq) << 1) | (static_cast<uint32_t>(atten) << 17) | ((gen & 0x7Fu) << 25);
}

enum class State : uint8_t {
  Idle,     // host not streaming: silence, ring empty
  Priming,  // streaming, filling the ring to its target before the first sample is played
  Running,  // playing; rate feedback active
};

void publishDrift() {
  const float ppm = g_feedback.driftPpm();
  uint32_t bits;
  std::memcpy(&bits, &ppm, sizeof bits);
  g_driftPpmBits.store(bits, std::memory_order_relaxed);
}

void audioTask(void*) {
  int16_t buf[kBlock * 2];
  State state = State::Idle;
  uint32_t emptyBlocks = 0;
  uint32_t toneSeen = g_toneCmd.load(std::memory_order_acquire);
  int32_t volSeen = INT32_MIN;
  bool muteSeen = false;
  bool warnedWrite = false;
  const size_t primeTarget = g_ring.capacity() / 2;

  for (;;) {
    // ---- controls from other tasks ----
    const uint32_t tc = g_toneCmd.load(std::memory_order_acquire);
    if (tc != toneSeen) {
      toneSeen = tc;
      if (tc & 1u) g_tone.start(static_cast<uint16_t>((tc >> 1) & 0xFFFFu), static_cast<uint8_t>((tc >> 17) & 0xFFu));
      else g_tone.stop();
    }
    const int32_t vol = g_volumeSetting.load(std::memory_order_relaxed);
    const bool mute = g_muteSetting.load(std::memory_order_relaxed);
    if (vol != volSeen) {
      volSeen = vol;
      g_volume.setVolume(static_cast<int16_t>(vol));
    }
    if (mute != muteSeen) {
      muteSeen = mute;
      g_volume.setMute(mute);
    }

    // ---- stream state ----
    const bool streaming = g_streaming.load(std::memory_order_acquire);
    if (streaming && state == State::Idle) {
      g_ring.discard(g_ring.readableFrames());  // consumer-side only: the USB interrupt may be writing
      g_feedback.reset();
      g_drift.reset();
      g_volume.snapToTarget();
      emptyBlocks = 0;
      state = State::Priming;
      g_primed.store(false, std::memory_order_relaxed);
      if (g_sink) g_sink(g_feedback.nominalQ16());
    } else if (!streaming && state != State::Idle) {
      state = State::Idle;
      g_primed.store(false, std::memory_order_relaxed);
      g_ring.discard(g_ring.readableFrames());
    }

    // ---- one block of audio ----
    if (state == State::Running) {
      const size_t real = g_drift.render(g_ring, buf, kBlock);
      g_volume.process(buf, kBlock);
      if (real == 0) {
        if (++emptyBlocks >= kEmptyBlocksBeforeReprime) {  // the host stopped sending: buffer up again
          state = State::Priming;
          g_primed.store(false, std::memory_order_relaxed);
          g_reprimes.fetch_add(1, std::memory_order_relaxed);
          g_feedback.reset();
          g_drift.reset();
          if (g_sink) g_sink(g_feedback.nominalQ16());
        }
      } else {
        emptyBlocks = 0;
      }
      const size_t fill = g_ring.fillFrames();
      const uint32_t q16 = g_feedback.update(static_cast<uint32_t>(fill));
      g_fill.store(static_cast<uint32_t>(fill), std::memory_order_relaxed);
      g_fbOut.store(q16, std::memory_order_relaxed);
      publishDrift();
      if (g_sink) g_sink(q16);
    } else {
      std::memset(buf, 0, sizeof buf);
      if (state == State::Priming) {
        const size_t fill = g_ring.fillFrames();
        g_fill.store(static_cast<uint32_t>(fill), std::memory_order_relaxed);
        if (fill >= primeTarget) {
          state = State::Running;
          g_primed.store(true, std::memory_order_relaxed);
        }
      }
    }
    if (g_tone.active()) g_tone.render(buf, kBlock);  // the test tone replaces the stream

    // Blocks until the DMA ring has room: this call paces the whole loop at one block per millisecond.
    size_t written = 0;
    const esp_err_t err = i2s_channel_write(g_i2s, buf, sizeof buf, &written, 100);
    if (err != ESP_OK && !warnedWrite) {
      warnedWrite = true;
      ESP_LOGE(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
    }
  }
}

}  // namespace

bool start(FeedbackSink sink) {
  if (g_started) return true;
  g_sink = sink;

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  cc.dma_desc_num = kDmaDescriptors;
  cc.dma_frame_num = kBlock;
  cc.auto_clear = true;  // an underrun plays silence instead of repeating the last buffer
  esp_err_t err = i2s_new_channel(&cc, &g_i2s, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);  // MCLK stays internal: the DAC derives its clock from BCK
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = static_cast<gpio_num_t>(pins::kI2sBck);
  sc.gpio_cfg.ws = static_cast<gpio_num_t>(pins::kI2sWs);
  sc.gpio_cfg.dout = static_cast<gpio_num_t>(pins::kI2sDout);
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  err = i2s_channel_init_std_mode(g_i2s, &sc);
  if (err == ESP_OK) err = i2s_channel_enable(g_i2s);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s init/enable: %s", esp_err_to_name(err));
    return false;
  }

  if (xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 22, nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "task create failed");
    return false;
  }
  g_started = true;
  ESP_LOGI(TAG, "I2S BCK GPIO%d WS GPIO%d DOUT GPIO%d, %u Hz 16-bit stereo, ring %u frames (%u ms), prime at %u",
           pins::kI2sBck, pins::kI2sWs, pins::kI2sDout, static_cast<unsigned>(kSampleRateHz),
           static_cast<unsigned>(kRingFrames), static_cast<unsigned>(kRingFrames * 1000 / kSampleRateHz),
           static_cast<unsigned>(kRingFrames / 2));
  return true;
}

bool running() { return g_started; }

size_t pushFromUsb(const int16_t* interleaved, size_t frames) { return g_ring.write(interleaved, frames); }

void setStreaming(bool streaming) { g_streaming.store(streaming, std::memory_order_release); }

void setVolume(int16_t volume256Db, bool mute) {
  g_volumeSetting.store(volume256Db, std::memory_order_relaxed);
  g_muteSetting.store(mute, std::memory_order_relaxed);
}

bool testTone(bool on, uint16_t freqHz, uint8_t attenuationDb) {
  if (!g_started) return false;
  g_toneGen = (g_toneGen + 1) & 0x7Fu;
  g_toneCmd.store(packTone(on, freqHz, attenuationDb, g_toneGen), std::memory_order_release);
  return true;
}

Stats stats() {
  Stats s;
  s.running = g_started;
  s.streaming = g_streaming.load(std::memory_order_relaxed);
  s.primed = g_primed.load(std::memory_order_relaxed);
  s.fillFrames = g_fill.load(std::memory_order_relaxed);
  s.overrunFrames = g_ring.overrunFrames();
  s.underrunFrames = g_ring.underrunFrames();
  s.reprimes = g_reprimes.load(std::memory_order_relaxed);
  s.driftDropped = g_drift.droppedFrames();
  s.driftDuplicated = g_drift.duplicatedFrames();
  const uint32_t bits = g_driftPpmBits.load(std::memory_order_relaxed);
  std::memcpy(&s.driftPpm, &bits, sizeof bits);
  s.feedbackQ16 = g_fbOut.load(std::memory_order_relaxed);
  return s;
}

}  // namespace audio_out
}  // namespace epdj
