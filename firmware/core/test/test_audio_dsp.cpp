// ToneGenerator (frequency, level, click-free start/stop/retune) and VolumeProcessor (UAC dB units, mute,
// gain ramps, saturation).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "epdj/audio_dsp.h"
#include "test_harness.h"

using namespace epdj::audio;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return next() % n; }
};

std::vector<int16_t> renderMono(ToneGenerator& t, size_t frames) {
  std::vector<int16_t> st(frames * 2);
  t.render(st.data(), frames);
  std::vector<int16_t> m(frames);
  for (size_t i = 0; i < frames; ++i) {
    if (st[2 * i] != st[2 * i + 1]) return {};  // L must equal R; an empty result fails the caller's checks
    m[i] = st[2 * i];
  }
  return m;
}

// Frequency from rising zero crossings with linear interpolation.
double measureHz(const std::vector<int16_t>& x, double rate) {
  double first = -1.0, last = -1.0;
  size_t n = 0;
  for (size_t i = 1; i < x.size(); ++i) {
    if (x[i - 1] < 0 && x[i] >= 0) {
      const double frac = -static_cast<double>(x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
      const double pos = static_cast<double>(i - 1) + frac;
      if (first < 0.0) first = pos;
      last = pos;
      ++n;
    }
  }
  if (n < 3) return 0.0;
  return static_cast<double>(n - 1) / (last - first) * rate;
}

int maxStep(const std::vector<int16_t>& x) {
  int m = 0;
  for (size_t i = 1; i < x.size(); ++i) m = std::max(m, std::abs(static_cast<int>(x[i]) - static_cast<int>(x[i - 1])));
  return m;
}

// Largest legitimate sample-to-sample change of a sine of this peak and frequency, plus the steepest the
// smoothstep envelope can add (1.5 / ramp per frame), plus rounding.
double stepBound(double peak, double hz, double rate) {
  return peak * 2.0 * kPi * hz / rate + peak * 1.5 / ToneGenerator::kRampFrames + 3.0;
}

}  // namespace

EPDJ_TEST(tone_frequency_is_accurate_across_the_band) {
  for (double hz : {1.0, 20.0, 50.0, 440.0, 997.0, 1000.0, 1234.0, 4000.0, 12000.0, 19999.0, 23000.0}) {
    ToneGenerator t;
    t.start(static_cast<uint16_t>(hz), 6);
    const size_t frames = static_cast<size_t>(std::max(96000.0, 48000.0 * 20.0 / hz));
    std::vector<int16_t> x = renderMono(t, frames);
    CHECK_EQ(x.size(), frames);
    x.erase(x.begin(), x.begin() + 480);  // steady state only
    const double got = measureHz(x, 48000.0);
    CHECK(std::fabs(got - hz) / hz < 2e-5);
  }
  ToneGenerator c(44100);  // other rates
  c.start(1000, 0);
  std::vector<int16_t> y = renderMono(c, 88200);
  y.erase(y.begin(), y.begin() + 480);
  CHECK(std::fabs(measureHz(y, 44100.0) - 1000.0) < 0.05);
}

EPDJ_TEST(tone_level_follows_attenuation_in_db) {
  for (int atten : {0, 3, 6, 12, 20, 40, 60, 90}) {
    ToneGenerator t;
    t.start(1000, static_cast<uint8_t>(atten));  // 48 samples per period: a sample lands exactly on the peak
    std::vector<int16_t> x = renderMono(t, 4800);
    int peak = 0;
    for (size_t i = 480; i < x.size(); ++i) peak = std::max(peak, std::abs(static_cast<int>(x[i])));
    const double ideal = 32767.0 * std::pow(10.0, -atten / 20.0);
    CHECK(peak <= ToneGenerator::peakFor(static_cast<uint8_t>(atten)));
    CHECK(peak >= ToneGenerator::peakFor(static_cast<uint8_t>(atten)) - 2);
    CHECK(std::fabs(static_cast<double>(peak) - ideal) <= 2.0);
  }
  CHECK_EQ(ToneGenerator::peakFor(0), 32767);
  CHECK_EQ(ToneGenerator::peakFor(255), 0);
  ToneGenerator quiet;
  quiet.start(1000, 255);
  for (int16_t v : renderMono(quiet, 2000)) CHECK_EQ(v, 0);
}

EPDJ_TEST(tone_waveform_matches_an_ideal_sine) {
  ToneGenerator t;
  t.start(1234, 10);
  const std::vector<int16_t> x = renderMono(t, 48000);
  const double peak = ToneGenerator::peakFor(10);
  int worst = 0;
  for (size_t n = 480; n < x.size(); ++n) {
    const double inc = std::round(1234.0 * 4294967296.0 / 48000.0) / 4294967296.0;  // the quantised phase step
    const double ideal = peak * std::sin(2.0 * kPi * inc * static_cast<double>(n));
    worst = std::max(worst, static_cast<int>(std::lround(std::fabs(static_cast<double>(x[n]) - ideal))));
  }
  CHECK(worst <= 2);

  // fs/4 is exact: 0, +P, 0, -P, ...
  ToneGenerator q;
  q.start(12000, 0);
  const std::vector<int16_t> y = renderMono(q, 2000);
  const int16_t p = y[1000 + 1];
  CHECK(p >= 32765);
  CHECK_EQ(y[1000], 0);
  CHECK_EQ(y[1002], 0);
  CHECK_EQ(y[1003], -p);
  CHECK_EQ(y[1005], p);
}

EPDJ_TEST(tone_start_and_stop_are_click_free) {
  for (double hz : {60.0, 440.0, 1000.0, 5000.0, 15000.0}) {
    for (int atten : {0, 20}) {
      ToneGenerator t;
      CHECK(!t.active());
      t.start(static_cast<uint16_t>(hz), static_cast<uint8_t>(atten));
      CHECK(t.active());
      std::vector<int16_t> all = renderMono(t, 3000);  // start ramp, steady state
      const size_t stopAt = all.size();
      t.stop();
      CHECK(t.active());  // ramping down
      const std::vector<int16_t> tail = renderMono(t, 1000);
      all.insert(all.end(), tail.begin(), tail.end());
      CHECK(!t.active());
      const double peak = ToneGenerator::peakFor(static_cast<uint8_t>(atten));
      CHECK(std::abs(all[0]) <= 1);  // begins at a zero crossing, in silence
      CHECK(static_cast<double>(maxStep(all)) <= stepBound(peak, hz, 48000.0));
      // silent once the ramp is over, and exactly so
      for (size_t i = stopAt + ToneGenerator::kRampFrames + 1; i < all.size(); ++i) CHECK_EQ(all[i], 0);
      // and the end of the ramp itself is flat (zero slope): the last 8 frames of it are below 0.5% of the peak
      int tailMax = 0;
      for (size_t i = stopAt + ToneGenerator::kRampFrames - 8; i < all.size(); ++i) tailMax = std::max(tailMax, std::abs(static_cast<int>(all[i])));
      CHECK(static_cast<double>(tailMax) <= peak * 0.005 + 2.0);
    }
  }
}

EPDJ_TEST(tone_stop_at_the_worst_possible_phase_is_still_smooth) {
  // 100 Hz = 480 samples per period, crest at sample 120 + 480k. Render up to the sample before the crest and
  // stop there: an abrupt cut would be a 32767-wide step.
  ToneGenerator t;
  t.start(100, 0);
  std::vector<int16_t> x = renderMono(t, 1080);
  t.stop();
  const std::vector<int16_t> tail = renderMono(t, 600);
  x.insert(x.end(), tail.begin(), tail.end());
  CHECK(tail[0] > 30000);  // really started the ramp-down at the crest
  CHECK(static_cast<double>(maxStep(x)) <= stepBound(32767.0, 100.0, 48000.0));
  CHECK(!t.active());
}

EPDJ_TEST(tone_is_independent_of_how_rendering_is_chunked) {
  ToneGenerator whole;
  whole.start(777, 9);
  const std::vector<int16_t> ref = renderMono(whole, 20000);
  ToneGenerator chunked;
  chunked.start(777, 9);
  Rng r(2);
  std::vector<int16_t> got;
  while (got.size() < ref.size()) {
    const size_t n = std::min<size_t>(1 + r.below(300), ref.size() - got.size());
    const std::vector<int16_t> part = renderMono(chunked, n);
    got.insert(got.end(), part.begin(), part.end());
  }
  CHECK(got == ref);
}

EPDJ_TEST(tone_retune_keeps_phase_and_slews_level) {
  ToneGenerator t;
  t.start(440, 0);
  std::vector<int16_t> x = renderMono(t, 2000);
  t.start(2000, 20);  // new frequency and 20 dB quieter, while running
  const std::vector<int16_t> y = renderMono(t, 3000);
  x.insert(x.end(), y.begin(), y.end());
  // no step beyond what the louder, faster of the two could produce
  CHECK(static_cast<double>(maxStep(x)) <= stepBound(32767.0, 2000.0, 48000.0));
  // settles at the new level (-20 dB)
  int peak = 0;
  for (size_t i = x.size() - 480; i < x.size(); ++i) peak = std::max(peak, std::abs(static_cast<int>(x[i])));
  CHECK(std::abs(peak - 3277) <= 3);
  // The level takes about kRampFrames to get there rather than jumping.
  int atRetune = 0;
  for (size_t i = 2000; i < 2010; ++i) atRetune = std::max(atRetune, std::abs(static_cast<int>(x[i])));
  CHECK(atRetune > 6000);

  // restarting while still ramping down resumes from where the envelope was
  ToneGenerator u;
  u.start(500, 0);
  std::vector<int16_t> a = renderMono(u, 1000);
  u.stop();
  const std::vector<int16_t> b = renderMono(u, 100);  // part-way down
  u.start(500, 0);
  const std::vector<int16_t> c = renderMono(u, 1000);
  a.insert(a.end(), b.begin(), b.end());
  a.insert(a.end(), c.begin(), c.end());
  CHECK(static_cast<double>(maxStep(a)) <= stepBound(32767.0, 500.0, 48000.0));
  CHECK(u.active());
}

EPDJ_TEST(tone_clamps_extreme_requests_and_idles_silently) {
  ToneGenerator t;
  std::vector<int16_t> st(200, 1234);
  t.render(st.data(), 100);  // idle: zeros, overwritten
  for (int16_t v : st) CHECK_EQ(v, 0);
  t.stop();  // no-op while idle
  CHECK(!t.active());
  t.start(0, 0);  // below range -> 1 Hz
  std::vector<int16_t> a = renderMono(t, 5000);
  CHECK(!a.empty());
  CHECK(a[4000] >= 0);  // 1 Hz: still on the first half-wave
  ToneGenerator hi;
  hi.start(65535, 0);  // above Nyquist -> just under it
  std::vector<int16_t> b = renderMono(hi, 5000);
  CHECK(!b.empty());
  CHECK(maxStep(b) < 65536);
}

// ---------------------------------------------------------------------------------------------------

EPDJ_TEST(volume_db_to_gain) {
  CHECK_EQ(VolumeProcessor::gainForVolume(0), 65536);
  CHECK(std::abs(VolumeProcessor::gainForVolume(-1541) - 32768) <= 20);    // -6.0206 dB = x0.5
  CHECK(std::abs(VolumeProcessor::gainForVolume(1541) - 131072) <= 60);   // +6.0206 dB = x2
  CHECK(std::abs(VolumeProcessor::gainForVolume(-5120) - 6554) <= 2);     // -20 dB = x0.1
  CHECK(std::abs(VolumeProcessor::gainForVolume(-256 * 40) - 655) <= 2);  // -40 dB
  CHECK_EQ(VolumeProcessor::gainForVolume(VolumeProcessor::kSilence), 0);
  CHECK_EQ(VolumeProcessor::gainForVolume(static_cast<int16_t>(-100 * 256 - 1)), 0);  // below -100 dB: silence
  CHECK(VolumeProcessor::gainForVolume(INT16_MAX) == VolumeProcessor::gainForVolume(24 * 256));  // capped at +24 dB
  CHECK(std::abs(VolumeProcessor::gainForVolume(24 * 256) - 1038689) <= 20);
  // monotonic
  int32_t prev = 0;
  for (int v = -20000; v <= 10000; v += 17) {
    const int32_t g = VolumeProcessor::gainForVolume(static_cast<int16_t>(v));
    CHECK(g >= prev);
    prev = g;
  }
}

EPDJ_TEST(volume_unity_is_bit_exact_and_zero_frames_is_a_no_op) {
  VolumeProcessor v;
  Rng r(4);
  std::vector<int16_t> in(2 * 5000), out;
  for (auto& s : in) s = static_cast<int16_t>(r.next() >> 16);
  out = in;
  v.process(out.data(), 5000);
  CHECK(out == in);
  v.process(out.data(), 0);
  CHECK(out == in);
  CHECK(v.settled());
}

EPDJ_TEST(volume_applies_the_requested_gain_to_both_channels) {
  struct { int16_t vol; int expect; } cases[] = {{-1541, 10000}, {-5120, 2000}, {-256 * 12, 5024}, {256 * 3, 28251}};
  for (const auto& c : cases) {
    VolumeProcessor v;
    v.setVolume(c.vol);
    std::vector<int16_t> warm(2 * 1000, 0);
    v.process(warm.data(), 1000);  // let the ramp finish
    CHECK(v.settled());
    std::vector<int16_t> x(2 * 10);
    for (size_t i = 0; i < 10; ++i) {
      x[2 * i] = 20000;
      x[2 * i + 1] = -20000;
    }
    v.process(x.data(), 10);
    CHECK(std::abs(x[0] - c.expect) <= 2);
    CHECK_EQ(x[1], -x[0]);
    CHECK_EQ(x[18], x[0]);
  }
}

EPDJ_TEST(volume_mute_and_unmute_ramp_without_steps) {
  VolumeProcessor v;
  std::vector<int16_t> x(2 * 600, 20000);
  v.setMute(true);
  v.process(x.data(), 600);
  int prev = 20000;
  int maxDelta = 0;
  size_t zeroFrom = 0;
  for (size_t i = 0; i < 600; ++i) {
    const int s = x[2 * i];
    CHECK(s <= prev);  // monotone fall
    maxDelta = std::max(maxDelta, prev - s);
    if (s == 0 && zeroFrom == 0) zeroFrom = i;
    prev = s;
  }
  CHECK(maxDelta <= 20000 * 274 / 65536 + 2);  // no step larger than one ramp increment
  CHECK(zeroFrom > 200 && zeroFrom <= VolumeProcessor::kRampFrames + 2);
  for (size_t i = zeroFrom; i < 600; ++i) CHECK_EQ(x[2 * i], 0);
  CHECK(v.settled());
  CHECK_EQ(v.gainQ16(), 0);

  std::vector<int16_t> y(2 * 600, 20000);
  v.setMute(false);
  v.process(y.data(), 600);
  prev = 0;
  maxDelta = 0;
  for (size_t i = 0; i < 600; ++i) {
    const int s = y[2 * i];
    CHECK(s >= prev);
    maxDelta = std::max(maxDelta, s - prev);
    prev = s;
  }
  CHECK(maxDelta <= 20000 * 274 / 65536 + 2);
  CHECK_EQ(y[2 * 599], 20000);
  CHECK(v.settled());

  // Volume changes while muted stay muted and take effect on unmute.
  v.setMute(true);
  v.setVolume(-5120);
  std::vector<int16_t> z(2 * 400, 20000);
  v.process(z.data(), 400);
  v.setMute(false);
  v.process(z.data(), 400);
  std::vector<int16_t> w(2 * 400, 20000);
  v.process(w.data(), 400);
  CHECK(std::abs(w[2 * 399] - 2000) <= 2);
}

EPDJ_TEST(volume_slider_drag_never_steps_the_gain) {
  VolumeProcessor v;
  Rng r(5);
  int32_t prev = v.gainQ16();
  std::vector<int16_t> buf(2 * 240, 0);
  int32_t maxStepQ16 = 0;
  for (int move = 0; move < 300; ++move) {
    v.setVolume(static_cast<int16_t>(-static_cast<int>(r.below(60 * 256))));  // anywhere 0 .. -60 dB
    if (move % 7 == 0) v.setMute(r.below(2) != 0);
    for (int block = 0; block < 5; ++block) {  // 5 ms of audio per UI event
      for (size_t i = 0; i < 240; ++i) {
        v.process(buf.data() + 2 * i, 1);
        maxStepQ16 = std::max(maxStepQ16, std::abs(v.gainQ16() - prev));
        prev = v.gainQ16();
      }
    }
  }
  CHECK(maxStepQ16 <= 274);  // 65536 / 240 + 1 per frame
  VolumeProcessor s;
  s.setVolume(-3000);
  s.snapToTarget();
  CHECK(s.settled());
  CHECK_EQ(s.gainQ16(), VolumeProcessor::gainForVolume(-3000));
}

EPDJ_TEST(volume_saturates_instead_of_wrapping) {
  VolumeProcessor v;
  v.setVolume(6 * 256);  // +6 dB
  v.snapToTarget();
  std::vector<int16_t> x = {30000, -30000, 32767, -32768, 20000, -16000, 0, 1};
  v.process(x.data(), 4);
  CHECK_EQ(x[0], 32767);
  CHECK_EQ(x[1], -32768);
  CHECK_EQ(x[2], 32767);
  CHECK_EQ(x[3], -32768);
  CHECK_EQ(x[4], 32767);  // 20000 x 1.995 > full scale
  CHECK(std::abs(x[5] + 31924) <= 2);  // -16000 x 1.995 fits and is not distorted
  CHECK_EQ(x[6], 0);
  CHECK_EQ(x[7], 2);
  // a loud sine at +12 dB: every output has the sign of its input (no wrap-around inversion)
  VolumeProcessor l;
  l.setVolume(12 * 256);
  l.snapToTarget();
  std::vector<int16_t> s(2 * 480);
  for (size_t i = 0; i < 480; ++i) s[2 * i] = s[2 * i + 1] = static_cast<int16_t>(std::lround(30000.0 * std::sin(2.0 * kPi * static_cast<double>(i) / 48.0)));
  const std::vector<int16_t> in = s;
  l.process(s.data(), 480);
  for (size_t i = 0; i < 480; ++i) {
    CHECK(in[2 * i] == 0 || (in[2 * i] > 0) == (s[2 * i] > 0) || s[2 * i] == 0);
    CHECK(std::abs(s[2 * i]) >= std::abs(in[2 * i]));
  }
}
