// Tempo, MIDI clock, transport, MIDI Time Code and tap tempo (PROTOCOL.md section 2.6).
// All classes are plain state machines driven by a monotonic microsecond clock passed in by the
// caller; none of them touch hardware, allocate, or block.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/midi.h"
#include "epdj/protocol.h"

namespace epdj {

// ---- BPM helpers. Tempo is carried everywhere as BPM x 10 (128.5 BPM = 1285). ----
namespace bpm {

constexpr uint16_t kMinX10 = proto::kBpmMinX10;
constexpr uint16_t kMaxX10 = proto::kBpmMaxX10;

constexpr uint16_t clampX10(int32_t v) {
  return static_cast<uint16_t>(v < kMinX10 ? kMinX10 : (v > kMaxX10 ? kMaxX10 : v));
}
// The `bpm` CC pair carries BPM x 10 directly as a 14-bit value; both directions clamp to 20.0..300.0.
constexpr uint16_t toCc14(uint16_t bpmX10) { return clampX10(bpmX10); }
constexpr uint16_t fromCc14(uint16_t value14) { return clampX10(value14); }
// One step is 0.1 BPM. Saturating, so a huge `steps` cannot wrap.
constexpr uint16_t nudge(uint16_t bpmX10, int32_t steps) {
  return clampX10(static_cast<int32_t>(bpmX10) + (steps > 100000 ? 100000 : (steps < -100000 ? -100000 : steps)));
}
// `bpmNudge` CC value (offset-64) applied to the current tempo.
constexpr uint16_t nudgeFromRelative(uint16_t bpmX10, uint8_t ccValue) {
  return nudge(bpmX10, midi::decodeRelative(ccValue));
}
}  // namespace bpm

// ---- MIDI clock source -------------------------------------------------------------------------
//
// 24 PPQN. Time is kept as a 32-bit microsecond counter plus a Q16 sub-microsecond fraction, with
// the period rounded to 1/65536 us: the schedule error stays below 0.002 us per second (the
// 1 us/s requirement leaves a factor of 500), with no floating point and no drift from rounding to
// whole microseconds. All time arithmetic is modular 32-bit, so it is correct across the 2^32 us
// (71 min) wrap and also when handed a 64-bit clock; the gap between advance() calls must stay
// below 2^31 us.
class ClockGenerator {
 public:
  // If the caller stalls for more than this many ticks, the backlog is dropped and the phase
  // re-anchored on the current time instead of emitting a burst.
  static constexpr uint32_t kMaxCatchUpTicks = 24;

  // First tick falls one period after nowUs. bpmX10 is clamped to 20.0..300.0.
  void start(uint64_t nowUs, uint16_t bpmX10);
  void stop() { running_ = false; }
  bool running() const { return running_; }

  // Takes effect on the next tick: the interval already scheduled is left alone (no tick is moved,
  // so there is no phase jump), every later interval uses the new period.
  void setBpmX10(uint16_t bpmX10) { bpmX10_ = bpm::clampX10(bpmX10); }
  uint16_t bpmX10() const { return bpmX10_; }

  // Number of ticks (0xF8) that have become due up to nowUs. Emit that many clocks now.
  uint32_t advance(uint64_t nowUs);

 private:
  void schedule();  // adds one period at the current tempo to nextUs_

  bool running_ = false;
  uint16_t bpmX10_ = 1200;
  uint32_t nextUs_ = 0;   // modular microseconds
  uint32_t fracQ16_ = 0;  // sub-microsecond remainder of nextUs_, 1/65536 us
};

// ---- Transport state machine -------------------------------------------------------------------
//
//   state     Play              Pause                 Stop (stop + rewind)   Rewind
//   STOPPED   FA -> PLAYING     -                     -                      -
//   PLAYING   -                 FC F2(pos) -> PAUSED  FC F2(0) -> STOPPED   FC F2(0) FA -> PLAYING
//   PAUSED    FB -> PLAYING     -                     F2(0) -> STOPPED       F2(0) -> STOPPED
//
// "-" produces no output. Song position is counted in clocks via onClockTick() (call it once for
// every 0xF8 you emit); a Pause announces floor(clocks / 6) MIDI beats and the FSM rounds its own
// position down to that beat, so a later Continue resumes exactly where followers think it is.
// A Pause at beat 0 is indistinguishable from Stop on the wire (SPP 0); followers mirror it as STOPPED.
class TransportFsm {
 public:
  enum class Event : uint8_t { Play, Pause, Stop, Rewind };

  struct Output {
    midi::Message msg[3];
    uint8_t count = 0;
  };

  Output handle(Event e);
  // Platter tap / play note: pauses when playing, otherwise plays.
  Output toggle() { return handle(state_ == proto::TransportState::Playing ? Event::Pause : Event::Play); }
  // Absolute request from the `transport` CC (0 stop, 1 play, 2 pause).
  Output goTo(proto::TransportState target);

  void onClockTick();

  proto::TransportState state() const { return state_; }
  // MIDI beats (6 clocks) since the start, wrapped to the 14 bits of Song Position Pointer.
  uint16_t songPositionBeats() const { return static_cast<uint16_t>((clocks_ / 6) & 0x3FFF); }
  uint32_t songPositionClocks() const { return clocks_; }

 private:
  static constexpr uint32_t kWrapClocks = 16384u * 6u;

  proto::TransportState state_ = proto::TransportState::Stopped;
  uint32_t clocks_ = 0;
};

// ---- MIDI Time Code ----------------------------------------------------------------------------

struct Timecode {
  uint8_t hours = 0, minutes = 0, seconds = 0, frames = 0;
  bool operator==(const Timecode& o) const {
    return hours == o.hours && minutes == o.minutes && seconds == o.seconds && frames == o.frames;
  }
};

// Frame rate types as in the MTC spec and `mtcFps`: 0 = 24, 1 = 25, 2 = 29.97 drop-frame, 3 = 30.
//
// Time is the elapsed PLAYING time since the last rewind (pause holds it), derived from the
// microsecond clock with exact rational frame rates, so 29.97 stays frame-accurate over hours and
// one hour of playing reads 01:00:00;00 in drop-frame. Quarter frames are emitted at 4 per frame.
// The 8 pieces of a sequence span 2 frames and all carry the time of its first frame, so piece 0
// goes out on every second frame counted from the rewind (frame 0, 2, 4, ...), as receivers expect.
// Hours wrap at 24.
class MtcGenerator {
 public:
  static constexpr uint8_t kFullFrameBytes = 10;

  // fpsType > 3 is ignored. A change of rate rewinds.
  void setFps(uint8_t fpsType, uint64_t nowUs);
  uint8_t fpsType() const { return fps_; }

  void play(uint64_t nowUs);
  void pause(uint64_t nowUs);
  // Time back to 00:00:00:00, keeps running if it was running. The next quarter frame is piece 0.
  void rewind(uint64_t nowUs);
  // The mapping of TransportFsm events onto play/pause/rewind (Stop = pause + rewind). Idempotent.
  void onTransportEvent(TransportFsm::Event e, uint64_t nowUs);
  bool playing() const { return playing_; }

  Timecode timecode(uint64_t nowUs) const;
  // F0 7F 7F 01 01 hr mn sc fr F7 for the current time (hr carries the rate type in bits 5-6).
  // Send on start, pause, stop and rewind. Returns kFullFrameBytes.
  size_t fullFrame(uint64_t nowUs, uint8_t out[kFullFrameBytes]) const;

  // Writes the data bytes (0nnndddd) of the quarter-frame messages that are due, at most maxOut;
  // the caller prefixes 0xF1 (midi::mtcQuarterFrame). Returns how many. If the caller falls more
  // than one 8-piece sequence behind, the backlog is skipped and output resumes at the next sequence.
  size_t advance(uint64_t nowUs, uint8_t* out, size_t maxOut);

  // Pure helpers (also what the tests check against the vectors).
  static Timecode toTimecode(uint64_t frameCount, uint8_t fpsType);
  static size_t encodeFullFrame(const Timecode& t, uint8_t fpsType, uint8_t out[kFullFrameBytes]);
  static uint8_t quarterFrameByte(const Timecode& t, uint8_t fpsType, uint8_t piece);

 private:
  uint64_t elapsedUs(uint64_t nowUs) const;
  uint64_t frameAt(uint64_t elapsed) const;
  uint64_t quarterFrameAt(uint64_t elapsed) const;

  uint8_t fps_ = 3;
  bool playing_ = false;
  uint64_t accumUs_ = 0;     // played time before the current run
  uint64_t runStartUs_ = 0;
  uint64_t nextQf_ = 0;      // index of the next quarter frame to emit; piece = index % 8
};

// ---- Following an external clock ---------------------------------------------------------------
//
// Tempo from the spacing of 0xF8 ticks. The last 48 tick times are kept; the median interval says
// what one tick is, each interval is then counted as round(interval / median) ticks (so a dropped
// tick is a 2-tick interval, not a tempo change), and tempo = total time / total ticks. Jitter on
// the individual timestamps cancels out of the total, leaving at most (2 x peak jitter) / (ticks - 1)
// of one period: ~0.4% at the 24-tick minimum for +/-5% jitter, ~0.2% with a full window. Counting
// stays right while timestamp jitter is below +/-25% of a tick (an interval is then off by < half a
// tick); beyond that, intervals are miscounted and the tempo is wrong. The
// estimate is invalid until 24 ticks have arrived and 500 ms after the last one; a gap longer than
// 500 ms starts a new window. Transport is mirrored from Start/Continue/Stop/Song Position.
class ClockFollower {
 public:
  static constexpr uint32_t kTimeoutUs = 500000;
  static constexpr size_t kWindow = 48;
  static constexpr size_t kMinTicks = 24;

  void onClock(uint64_t nowUs);
  void onStart();
  void onContinue();
  void onStop();
  void onSongPosition(uint16_t beats);
  // Dispatches F8, FA, FB, FC and F2. Returns true if the message was one of those.
  bool onMessage(const midi::Message& m, uint64_t nowUs);

  // Mirrored from the master. Not reset by clock loss: some masters stop clocking while stopped.
  proto::TransportState transport() const { return transport_; }

  // True with >= 24 ticks in the window and the last one < 500 ms old.
  bool locked(uint64_t nowUs) const;
  // Clamped to 20.0..300.0, or 0 when not locked.
  uint16_t bpmX10(uint64_t nowUs) const { return locked(nowUs) ? estimateX10_ : 0; }

  void reset();

 private:
  void estimate();

  uint32_t t_[kWindow] = {};  // modular microsecond timestamps, ring
  size_t head_ = 0;           // next slot to write
  size_t count_ = 0;
  uint32_t lastUs_ = 0;
  uint16_t estimateX10_ = 0;
  proto::TransportState transport_ = proto::TransportState::Stopped;
};

// ---- Tap tempo ----------------------------------------------------------------------------------
//
// Tempo = 600e6 / mean of the last 4 tap intervals (fewer while fewer exist), clamped to the BPM
// range. A gap of 2 s or more forgets everything, so the slowest tempo tappable is 30 BPM.
class TapTempo {
 public:
  static constexpr uint64_t kResetUs = 2000000;
  static constexpr size_t kIntervals = 4;

  // Returns the tempo after this tap, or 0 for the first tap of a series (nothing to average yet).
  uint16_t tap(uint64_t nowUs);
  void reset() { taps_ = 0; }

 private:
  uint64_t lastUs_ = 0;
  uint64_t intervals_[kIntervals] = {};
  size_t taps_ = 0;  // taps in the current series
  size_t next_ = 0;
};

}  // namespace epdj
