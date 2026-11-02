#include "epdj/sync.h"

namespace epdj {

using proto::TransportState;

// ---- ClockGenerator ----

namespace {
// Period in 1/65536 us. 60e6 us/min / (bpmX10/10 beats/min x 24 ticks/beat) = 25e6 / bpmX10 us.
uint64_t periodQ16(uint16_t bpmX10) {
  return ((25000000ull << 16) + bpmX10 / 2u) / bpmX10;
}
}  // namespace

void ClockGenerator::schedule() {
  const uint64_t acc = fracQ16_ + periodQ16(bpmX10_);
  nextUs_ += static_cast<uint32_t>(acc >> 16);  // modular
  fracQ16_ = static_cast<uint32_t>(acc & 0xFFFFu);
}

void ClockGenerator::start(uint64_t nowUs, uint16_t bpmX10) {
  bpmX10_ = bpm::clampX10(bpmX10);
  nextUs_ = static_cast<uint32_t>(nowUs);
  fracQ16_ = 0;
  schedule();
  running_ = true;
}

uint32_t ClockGenerator::advance(uint64_t nowUs) {
  if (!running_) return 0;
  const uint32_t now = static_cast<uint32_t>(nowUs);
  uint32_t due = 0;
  while (static_cast<int32_t>(now - nextUs_) >= 0) {
    if (due == kMaxCatchUpTicks) {  // stalled for more than a beat: do not replay, re-anchor
      nextUs_ = now;
      fracQ16_ = 0;
      schedule();
      break;
    }
    ++due;
    schedule();
  }
  return due;
}

// ---- TransportFsm ----

TransportFsm::Output TransportFsm::handle(Event e) {
  Output o;
  auto add = [&o](const midi::Message& m) { o.msg[o.count++] = m; };
  switch (e) {
    case Event::Play:
      if (state_ == TransportState::Stopped) {
        clocks_ = 0;
        add(midi::start());
        state_ = TransportState::Playing;
      } else if (state_ == TransportState::Paused) {
        add(midi::cont());
        state_ = TransportState::Playing;
      }
      break;
    case Event::Pause:
      if (state_ == TransportState::Playing) {
        clocks_ = clocks_ / 6u * 6u;  // followers only know whole beats
        add(midi::stop());
        add(midi::songPosition(songPositionBeats()));
        state_ = TransportState::Paused;
      }
      break;
    case Event::Stop:
      if (state_ == TransportState::Playing) {
        add(midi::stop());
        add(midi::songPosition(0));
      } else if (state_ == TransportState::Paused) {
        add(midi::songPosition(0));
      }
      clocks_ = 0;
      state_ = TransportState::Stopped;
      break;
    case Event::Rewind:
      if (state_ == TransportState::Playing) {
        add(midi::stop());
        add(midi::songPosition(0));
        add(midi::start());
        clocks_ = 0;
      } else if (state_ == TransportState::Paused) {
        add(midi::songPosition(0));
        clocks_ = 0;
        state_ = TransportState::Stopped;
      }
      break;
  }
  return o;
}

TransportFsm::Output TransportFsm::goTo(TransportState target) {
  switch (target) {
    case TransportState::Playing: return handle(Event::Play);
    case TransportState::Paused: return handle(Event::Pause);
    case TransportState::Stopped: break;
  }
  return handle(Event::Stop);
}

void TransportFsm::onClockTick() {
  if (state_ == TransportState::Playing) clocks_ = (clocks_ + 1u) % kWrapClocks;
}

// ---- MtcGenerator ----

namespace {
struct FrameRate {
  uint64_t num, den;  // frames per second = num / den
};
constexpr FrameRate kRates[4] = {{24, 1}, {25, 1}, {30000, 1001}, {30, 1}};
constexpr uint64_t kUsPerSecond = 1000000;
}  // namespace

uint64_t MtcGenerator::elapsedUs(uint64_t nowUs) const {
  return accumUs_ + (playing_ && nowUs > runStartUs_ ? nowUs - runStartUs_ : 0);
}

uint64_t MtcGenerator::frameAt(uint64_t elapsed) const {
  return elapsed * kRates[fps_].num / (kRates[fps_].den * kUsPerSecond);
}

uint64_t MtcGenerator::quarterFrameAt(uint64_t elapsed) const {
  return elapsed * 4u * kRates[fps_].num / (kRates[fps_].den * kUsPerSecond);
}

void MtcGenerator::setFps(uint8_t fpsType, uint64_t nowUs) {
  if (fpsType > 3 || fpsType == fps_) return;
  fps_ = fpsType;
  rewind(nowUs);
}

void MtcGenerator::play(uint64_t nowUs) {
  if (playing_) return;
  playing_ = true;
  runStartUs_ = nowUs;
}

void MtcGenerator::pause(uint64_t nowUs) {
  if (!playing_) return;
  accumUs_ = elapsedUs(nowUs);
  playing_ = false;
}

void MtcGenerator::rewind(uint64_t nowUs) {
  accumUs_ = 0;
  runStartUs_ = nowUs;
  nextQf_ = 0;
}

void MtcGenerator::onTransportEvent(TransportFsm::Event e, uint64_t nowUs) {
  switch (e) {
    case TransportFsm::Event::Play: play(nowUs); break;
    case TransportFsm::Event::Pause: pause(nowUs); break;
    case TransportFsm::Event::Stop:
      pause(nowUs);
      rewind(nowUs);
      break;
    case TransportFsm::Event::Rewind: rewind(nowUs); break;
  }
}

Timecode MtcGenerator::toTimecode(uint64_t frameCount, uint8_t fpsType) {
  static constexpr uint32_t kFps[4] = {24, 25, 30, 30};
  static constexpr uint64_t kFramesPerDay[4] = {24ull * 86400, 25ull * 86400, 144ull * 17982, 30ull * 86400};
  const uint8_t type = fpsType > 3 ? 3 : fpsType;
  uint64_t n = frameCount % kFramesPerDay[type];
  if (type == 2) {
    // Drop-frame: labels :00 and :01 are skipped at the start of every minute except each tenth.
    // A 10-minute block holds 17982 frames: 1800 in its first minute, 1798 in each of the other 9.
    const uint64_t block = n / 17982, m = n % 17982;
    n += 18 * block + (m < 2 ? 0 : 2 * ((m - 2) / 1798));
  }
  const uint64_t fps = kFps[type];
  Timecode t;
  t.frames = static_cast<uint8_t>(n % fps);
  t.seconds = static_cast<uint8_t>((n / fps) % 60);
  t.minutes = static_cast<uint8_t>((n / (fps * 60)) % 60);
  t.hours = static_cast<uint8_t>((n / (fps * 3600)) % 24);
  return t;
}

size_t MtcGenerator::encodeFullFrame(const Timecode& t, uint8_t fpsType, uint8_t out[kFullFrameBytes]) {
  out[0] = 0xF0;
  out[1] = 0x7F;  // universal real time, all devices
  out[2] = 0x7F;
  out[3] = 0x01;  // MTC
  out[4] = 0x01;  // full message
  out[5] = static_cast<uint8_t>(((fpsType & 3) << 5) | (t.hours & 0x1F));
  out[6] = t.minutes;
  out[7] = t.seconds;
  out[8] = t.frames;
  out[9] = 0xF7;
  return kFullFrameBytes;
}

uint8_t MtcGenerator::quarterFrameByte(const Timecode& t, uint8_t fpsType, uint8_t piece) {
  uint8_t nibble = 0;
  switch (piece & 7) {
    case 0: nibble = t.frames & 0x0F; break;
    case 1: nibble = t.frames >> 4; break;
    case 2: nibble = t.seconds & 0x0F; break;
    case 3: nibble = t.seconds >> 4; break;
    case 4: nibble = t.minutes & 0x0F; break;
    case 5: nibble = t.minutes >> 4; break;
    case 6: nibble = t.hours & 0x0F; break;
    default: nibble = static_cast<uint8_t>(((fpsType & 3) << 1) | ((t.hours >> 4) & 1)); break;
  }
  return static_cast<uint8_t>(((piece & 7) << 4) | (nibble & 0x0F));
}

Timecode MtcGenerator::timecode(uint64_t nowUs) const { return toTimecode(frameAt(elapsedUs(nowUs)), fps_); }

size_t MtcGenerator::fullFrame(uint64_t nowUs, uint8_t out[kFullFrameBytes]) const {
  return encodeFullFrame(timecode(nowUs), fps_, out);
}

size_t MtcGenerator::advance(uint64_t nowUs, uint8_t* out, size_t maxOut) {
  if (!playing_) return 0;
  const uint64_t due = quarterFrameAt(elapsedUs(nowUs));
  if (due > nextQf_ + 8) nextQf_ = (due + 7) / 8 * 8;
  size_t n = 0;
  while (nextQf_ <= due && n < maxOut) {
    const uint8_t piece = static_cast<uint8_t>(nextQf_ % 8);
    // All 8 pieces describe the time of the sequence's first frame (every second frame).
    out[n++] = quarterFrameByte(toTimecode(nextQf_ / 8 * 2, fps_), fps_, piece);
    ++nextQf_;
  }
  return n;
}

// ---- ClockFollower ----

void ClockFollower::reset() {
  head_ = 0;
  count_ = 0;
  estimateX10_ = 0;
  transport_ = TransportState::Stopped;
}

void ClockFollower::onClock(uint64_t nowUs) {
  const uint32_t t = static_cast<uint32_t>(nowUs);
  if (count_ > 0 && static_cast<uint32_t>(t - lastUs_) > kTimeoutUs) {
    count_ = 0;
    estimateX10_ = 0;
  }
  t_[head_] = t;
  head_ = (head_ + 1) % kWindow;
  if (count_ < kWindow) ++count_;
  lastUs_ = t;
  if (count_ >= kMinTicks) estimate();
}

void ClockFollower::estimate() {
  const size_t m = count_ - 1;  // intervals
  const size_t oldest = (head_ + kWindow - count_) % kWindow;
  uint32_t d[kWindow - 1];
  uint32_t sorted[kWindow - 1];
  for (size_t i = 0; i < m; ++i) {
    d[i] = t_[(oldest + i + 1) % kWindow] - t_[(oldest + i) % kWindow];
    size_t j = i;
    while (j > 0 && sorted[j - 1] > d[i]) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = d[i];
  }
  const uint32_t median = sorted[m / 2];
  estimateX10_ = 0;
  if (median == 0) return;

  uint64_t sumUs = 0, sumTicks = 0;
  for (size_t i = 0; i < m; ++i) {
    const uint64_t ticks = (d[i] + median / 2u) / median;
    sumTicks += ticks == 0 ? 1 : ticks;
    sumUs += d[i];
  }
  if (sumUs == 0) return;
  const uint64_t x10 = (25000000ull * sumTicks * 2 + sumUs) / (2 * sumUs);  // 25e6 / mean tick period
  estimateX10_ = bpm::clampX10(static_cast<int32_t>(x10 > 100000 ? 100000 : x10));
}

bool ClockFollower::locked(uint64_t nowUs) const {
  return count_ >= kMinTicks && estimateX10_ != 0 &&
         static_cast<uint32_t>(static_cast<uint32_t>(nowUs) - lastUs_) <= kTimeoutUs;
}

void ClockFollower::onStart() { transport_ = TransportState::Playing; }
void ClockFollower::onContinue() { transport_ = TransportState::Playing; }
void ClockFollower::onStop() {
  if (transport_ == TransportState::Playing) transport_ = TransportState::Paused;
}
void ClockFollower::onSongPosition(uint16_t beats) {
  // A master that stops and rewinds sends Stop then SPP 0: tell that apart from a plain pause.
  if (transport_ != TransportState::Playing && beats == 0) transport_ = TransportState::Stopped;
}

bool ClockFollower::onMessage(const midi::Message& m, uint64_t nowUs) {
  if (m.len == 0) return false;
  switch (m.b[0]) {
    case proto::kStatusClock: onClock(nowUs); return true;
    case proto::kStatusStart: onStart(); return true;
    case proto::kStatusContinue: onContinue(); return true;
    case proto::kStatusStop: onStop(); return true;
    case proto::kStatusSongPosition:
      if (m.len != 3) return false;
      onSongPosition(midi::join14(m.b[1], m.b[2]));
      return true;
    default: return false;
  }
}

// ---- TapTempo ----

uint16_t TapTempo::tap(uint64_t nowUs) {
  if (taps_ > 0 && (nowUs < lastUs_ || nowUs - lastUs_ >= kResetUs)) taps_ = 0;
  if (taps_ == 0) {
    next_ = 0;
  } else {
    const uint64_t gap = nowUs - lastUs_;
    intervals_[next_] = gap == 0 ? 1 : gap;
    next_ = (next_ + 1) % kIntervals;
  }
  lastUs_ = nowUs;
  if (taps_ < kIntervals + 1) ++taps_;

  const size_t n = taps_ - 1;
  if (n == 0) return 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += intervals_[i];  // before the ring wraps the first n slots are the intervals
  const uint64_t mean = (sum + n / 2) / n;
  const uint64_t x10 = (600000000ull + mean / 2) / mean;
  return bpm::clampX10(static_cast<int32_t>(x10 > 100000 ? 100000 : x10));
}

}  // namespace epdj
