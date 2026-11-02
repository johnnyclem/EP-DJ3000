// Tempo helpers, clock generator, transport FSM, MTC and tap tempo (PROTOCOL.md 2.6).
#include <cmath>
#include <vector>

#include "epdj/midi.h"
#include "epdj/protocol.h"
#include "epdj/sync.h"
#include "test_harness.h"
#include "vectors.generated.h"

using namespace epdj;
using proto::TransportState;
using Bytes = std::vector<uint8_t>;
using Event = TransportFsm::Event;

namespace {

Bytes bytesOf(const midi::Message& m) { return Bytes(m.b, m.b + m.len); }

Bytes outputBytes(const TransportFsm::Output& o) {
  Bytes b;
  for (uint8_t i = 0; i < o.count; ++i) {
    const Bytes m = bytesOf(o.msg[i]);
    b.insert(b.end(), m.begin(), m.end());
  }
  return b;
}

double periodUs(uint16_t bpmX10) { return 25e6 / bpmX10; }

// Steps a ClockGenerator at 1 us resolution and returns the time of every tick in [from, to].
std::vector<uint64_t> tickTimes(ClockGenerator& g, uint64_t from, uint64_t to) {
  std::vector<uint64_t> t;
  for (uint64_t now = from; now <= to; ++now) {
    const uint32_t n = g.advance(now);
    for (uint32_t i = 0; i < n; ++i) t.push_back(now);
  }
  return t;
}

}  // namespace

// ---- BPM helpers ----

EPDJ_TEST(bpm_clamp_and_cc14_helpers) {
  CHECK_EQ(bpm::clampX10(-5), 200);
  CHECK_EQ(bpm::clampX10(0), 200);
  CHECK_EQ(bpm::clampX10(199), 200);
  CHECK_EQ(bpm::clampX10(200), 200);
  CHECK_EQ(bpm::clampX10(1285), 1285);
  CHECK_EQ(bpm::clampX10(3000), 3000);
  CHECK_EQ(bpm::clampX10(3001), 3000);
  CHECK_EQ(bpm::clampX10(INT32_MAX), 3000);
  CHECK_EQ(bpm::clampX10(INT32_MIN), 200);
  for (const auto& v : epdj_vec::cc14Vectors()) {
    if (v.value14 < 200 || v.value14 > 3000) continue;
    CHECK_EQ(bpm::fromCc14(midi::join14(v.msb, v.lsb)), v.value14);
    CHECK_EQ(bpm::toCc14(v.value14), v.value14);
  }
  CHECK_EQ(bpm::fromCc14(0), 200);          // a 14-bit CC below 20.0 BPM clamps up
  CHECK_EQ(bpm::fromCc14(16383), 3000);
  CHECK_EQ(bpm::toCc14(3500), 3000);
}

EPDJ_TEST(bpm_nudge_steps_clamps_and_saturates) {
  CHECK_EQ(bpm::nudge(1200, 1), 1201);
  CHECK_EQ(bpm::nudge(1200, -5), 1195);
  CHECK_EQ(bpm::nudge(1200, 0), 1200);
  CHECK_EQ(bpm::nudge(200, -1), 200);
  CHECK_EQ(bpm::nudge(3000, 1), 3000);
  CHECK_EQ(bpm::nudge(2999, 63), 3000);
  CHECK_EQ(bpm::nudge(1200, INT32_MAX), 3000);
  CHECK_EQ(bpm::nudge(1200, INT32_MIN), 200);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 64), 1200);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 65), 1201);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 63), 1199);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 127), 1263);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 1), 1137);
  CHECK_EQ(bpm::nudgeFromRelative(1200, 0), 1136);
  CHECK_EQ(bpm::nudgeFromRelative(205, 1), 200);
  // Round trip with the encoder used by the sender.
  for (int d = -63; d <= 63; ++d) CHECK_EQ(bpm::nudgeFromRelative(1500, midi::encodeRelative(d)), 1500 + d);
}

EPDJ_TEST(clock_period_matches_vectors) {
  // The reference period is 60e6 / (bpm x 24) us = 25e6 / bpmX10; the generator must hit it.
  for (const auto& v : epdj_vec::clockVectors()) {
    CHECK_NEAR(periodUs(v.bpmX10), v.periodUs, 1e-5);
    ClockGenerator g;
    g.start(0, v.bpmX10);
    const uint64_t before = static_cast<uint64_t>(std::floor(1000.0 * v.periodUs)) - 3;
    uint64_t total = 0, t = 0;
    for (; t < before; t += 1000) total += g.advance(t);
    total += g.advance(before);
    CHECK_EQ(total, 999u);  // 1000th tick not there yet
    total += g.advance(static_cast<uint64_t>(std::ceil(1000.0 * v.periodUs)) + 2);
    CHECK_EQ(total, 1000u);
  }
}

// ---- ClockGenerator ----

EPDJ_TEST(clock_ticks_stay_within_three_microseconds_of_ideal_over_ten_minutes) {
  // Probe before and after every ideal tick time for 10 minutes: tick k must not have fired 3 us
  // early and must have fired 2 us late at the latest. That bounds the accumulated error to a few
  // microseconds in 600 s (floor-to-microsecond plus the Q16 period rounding, <= ~1 us at 300 BPM),
  // i.e. orders of magnitude below the 1 us/s requirement, and proves no drift builds up.
  const uint64_t bases[] = {0, 0xFFFFFFFFull - 300000000ull /* 32-bit wrap mid-run */, 0x1FFFF0000ull};
  const uint16_t tempos[] = {200, 600, 1200, 1285, 1337, 2999, 3000};
  for (uint64_t base : bases) {
    for (uint16_t bpmX10 : tempos) {
      ClockGenerator g;
      g.start(base, bpmX10);
      const double p = periodUs(bpmX10);
      const uint64_t ticks = static_cast<uint64_t>(600e6 / p);
      uint64_t total = 0;
      bool ok = true;
      for (uint64_t k = 1; k <= ticks && ok; ++k) {
        const double ideal = static_cast<double>(k) * p;
        total += g.advance(base + static_cast<uint64_t>(std::floor(ideal)) - 3);
        if (total != k - 1) { ok = false; break; }
        total += g.advance(base + static_cast<uint64_t>(std::ceil(ideal)) + 2);
        if (total != k) ok = false;
      }
      CHECK(ok);
      if (!ok) std::printf("      base=%llx bpmX10=%u total=%llu\n", static_cast<unsigned long long>(base), bpmX10, static_cast<unsigned long long>(total));
    }
  }
}

EPDJ_TEST(clock_is_identical_for_64bit_and_truncated_32bit_time) {
  const uint64_t base = 0xFFFFFFFFull - 2000000ull;  // wraps 2 s in
  ClockGenerator a, b;
  a.start(base, 1285);
  b.start(base & 0xFFFFFFFFull, 1285);
  uint64_t na = 0, nb = 0;
  for (uint64_t t = 0; t < 10000000; t += 777) {
    na += a.advance(base + t);
    nb += b.advance((base + t) & 0xFFFFFFFFull);
    CHECK_EQ(na, nb);
  }
  CHECK(na > 400);
}

EPDJ_TEST(clock_polled_at_1khz_counts_every_tick_across_the_wrap) {
  ClockGenerator g;
  const uint64_t base = 0xFFFFFFFFull - 30000000ull;
  g.start(base, 1285);
  uint64_t total = 0;
  for (uint64_t t = 0; t <= 600000000ull + 1000; t += 1000) total += g.advance(base + t);
  CHECK_EQ(total, 30840u);  // 10 min x 128.5 BPM x 24 PPQN, exactly
}

EPDJ_TEST(clock_not_running_emits_nothing_and_start_restarts) {
  ClockGenerator g;
  CHECK(!g.running());
  CHECK_EQ(g.advance(1000000), 0u);
  g.start(1000, 1200);  // period 20833.33 us
  CHECK(g.running());
  CHECK_EQ(g.advance(1000), 0u);  // first tick one period after start
  CHECK_EQ(g.advance(1000 + 20832), 0u);
  CHECK_EQ(g.advance(1000 + 20833), 1u);
  g.stop();
  CHECK(!g.running());
  CHECK_EQ(g.advance(10000000), 0u);
  g.start(50000000, 600);  // period 41666.67 us
  CHECK_EQ(g.advance(50000000 + 41665), 0u);
  CHECK_EQ(g.advance(50000000 + 41666), 1u);
}

EPDJ_TEST(clock_tempo_is_clamped) {
  ClockGenerator g;
  g.start(0, 5000);
  CHECK_EQ(g.bpmX10(), 3000);
  g.setBpmX10(1);
  CHECK_EQ(g.bpmX10(), 200);
  g.setBpmX10(65535);
  CHECK_EQ(g.bpmX10(), 3000);
}

EPDJ_TEST(clock_tempo_change_takes_effect_on_the_next_tick_without_a_phase_jump) {
  // Whenever in the interval the tempo is changed, the next tick stays exactly where the old tempo put
  // it, and the interval after it uses the new period.
  const uint64_t offsets[] = {0, 1, 5000, 20000, 20832};  // microseconds after tick 3
  for (uint64_t off : offsets) {
    ClockGenerator control, g;
    control.start(0, 1200);
    g.start(0, 1200);
    const auto ref = tickTimes(control, 0, 200000);
    CHECK(ref.size() >= 8);

    uint64_t t = 0;
    std::vector<uint64_t> got;
    auto run = [&](uint64_t to) {
      for (; t <= to; ++t) {
        const uint32_t n = g.advance(t);
        for (uint32_t i = 0; i < n; ++i) got.push_back(t);
      }
    };
    run(ref[2] + off);
    g.setBpmX10(1500);
    run(200000);

    CHECK(got.size() >= 8);
    if (got.size() < 8 || ref.size() < 8) continue;
    for (size_t i = 0; i <= 3; ++i) CHECK_EQ(got[i], ref[i]);  // ticks 1-4 unchanged (tick 4 was already scheduled)
    // From tick 5 on the spacing is 25e6/1500 = 16666.67 us.
    for (size_t i = 4; i + 1 < got.size(); ++i) {
      const uint64_t d = got[i + 1] - got[i];
      CHECK(d == 16666 || d == 16667);
    }
    CHECK(got[4] - got[3] == 16666 || got[4] - got[3] == 16667);
  }
}

EPDJ_TEST(clock_continuous_tempo_ramp_keeps_the_phase_exact) {
  // Change tempo right after every single tick (a knob sweep). The interval after tick k+1 uses the
  // tempo set after tick k, so the elapsed time must equal that sum of periods to within rounding:
  // the sub-microsecond remainder must carry across tempo changes.
  ClockGenerator g;
  g.start(0, 1000);
  double tickTime = periodUs(1000);  // time of the next tick to fire
  uint16_t inForce = 1000;           // tempo that sets the interval after the next tick
  uint64_t ticks = 0, firedAt = 0;
  double lastIdeal = 0;
  for (uint64_t t = 0; ticks < 1000; ++t) {
    const uint32_t n = g.advance(t);
    for (uint32_t i = 0; i < n; ++i) {
      ++ticks;
      firedAt = t;
      lastIdeal = tickTime;
      tickTime += periodUs(inForce);
      inForce = static_cast<uint16_t>(1000 + (ticks * 7) % 1500);
      g.setBpmX10(inForce);
    }
  }
  CHECK(std::fabs(static_cast<double>(firedAt) - lastIdeal) < 1.5);
}

EPDJ_TEST(clock_stall_is_not_replayed_as_a_burst) {
  ClockGenerator g;
  g.start(0, 3000);  // 8333.33 us period
  CHECK_EQ(g.advance(83340), 10u);  // a 10-tick stall is caught up
  const uint64_t now = 83340 + 60000000;  // then a 60 s stall
  CHECK_EQ(g.advance(now), ClockGenerator::kMaxCatchUpTicks);
  CHECK_EQ(g.advance(now), 0u);  // re-anchored, backlog dropped
  CHECK_EQ(g.advance(now + 8332), 0u);
  CHECK_EQ(g.advance(now + 8333), 1u);  // next tick one period after the stall
}

EPDJ_TEST(clock_polled_slowly_still_counts_right) {
  ClockGenerator g;
  g.start(0, 3000);
  uint64_t total = 0;
  for (uint64_t t = 0; t <= 10000000; t += 50000) total += g.advance(t);  // 50 ms polls, 6 ticks each
  CHECK_EQ(total, 1200u);  // 10 s x 120 ticks/s
}

// ---- TransportFsm ----

EPDJ_TEST(transport_play_from_stopped_sends_start) {
  TransportFsm f;
  const auto o = f.handle(Event::Play);
  CHECK_BYTES(outputBytes(o), (Bytes{0xFA}));
  CHECK(f.state() == TransportState::Playing);
}

EPDJ_TEST(transport_state_event_table) {
  enum class Start { Stopped, Playing, Paused };
  struct Row {
    const char* name;
    Start start;
    int ticks;  // clocks counted while playing, before the event
    Event event;
    Bytes out;
    TransportState next;
    uint32_t beats;
  };
  const Row rows[] = {
      {"stopped+play", Start::Stopped, 0, Event::Play, {0xFA}, TransportState::Playing, 0},
      {"stopped+pause", Start::Stopped, 0, Event::Pause, {}, TransportState::Stopped, 0},
      {"stopped+stop", Start::Stopped, 0, Event::Stop, {}, TransportState::Stopped, 0},
      {"stopped+rewind", Start::Stopped, 0, Event::Rewind, {}, TransportState::Stopped, 0},
      // 100 clocks = 16 beats + 4 clocks
      {"playing+play", Start::Playing, 100, Event::Play, {}, TransportState::Playing, 16},
      {"playing+pause", Start::Playing, 100, Event::Pause, {0xFC, 0xF2, 16, 0}, TransportState::Paused, 16},
      {"playing+stop", Start::Playing, 100, Event::Stop, {0xFC, 0xF2, 0, 0}, TransportState::Stopped, 0},
      {"playing+rewind", Start::Playing, 100, Event::Rewind, {0xFC, 0xF2, 0, 0, 0xFA}, TransportState::Playing, 0},
      // beat 128 needs both 7-bit halves of SPP
      {"playing+pause@128", Start::Playing, 128 * 6 + 5, Event::Pause, {0xFC, 0xF2, 0, 1}, TransportState::Paused, 128},
      // paused at beat 16 (position rounded down from 100 clocks)
      {"paused+play", Start::Paused, 100, Event::Play, {0xFB}, TransportState::Playing, 16},
      {"paused+pause", Start::Paused, 100, Event::Pause, {}, TransportState::Paused, 16},
      {"paused+stop", Start::Paused, 100, Event::Stop, {0xF2, 0, 0}, TransportState::Stopped, 0},
      {"paused+rewind", Start::Paused, 100, Event::Rewind, {0xF2, 0, 0}, TransportState::Stopped, 0},
  };
  for (const Row& r : rows) {
    TransportFsm f;
    if (r.start != Start::Stopped) {
      f.handle(Event::Play);
      for (int i = 0; i < r.ticks; ++i) f.onClockTick();
      if (r.start == Start::Paused) f.handle(Event::Pause);
    }
    const auto o = f.handle(r.event);
    if (outputBytes(o) != r.out) std::printf("    row %s\n", r.name);
    CHECK_BYTES(outputBytes(o), r.out);
    CHECK(f.state() == r.next);
    CHECK_EQ(f.songPositionBeats(), r.beats);
    CHECK(o.count <= 3);
  }
}

EPDJ_TEST(transport_pause_rounds_position_down_to_the_announced_beat) {
  TransportFsm f;
  f.handle(Event::Play);
  for (int i = 0; i < 100; ++i) f.onClockTick();  // 16 beats + 4 clocks
  const auto p = f.handle(Event::Pause);
  CHECK_BYTES(outputBytes(p), (Bytes{0xFC, 0xF2, 16, 0}));
  CHECK_EQ(f.songPositionClocks(), 96u);  // what followers were told, not 100
  CHECK_BYTES(outputBytes(f.handle(Event::Play)), (Bytes{0xFB}));
  for (int i = 0; i < 12; ++i) f.onClockTick();  // +2 beats
  const auto q = f.handle(Event::Pause);
  CHECK_BYTES(outputBytes(q), (Bytes{0xFC, 0xF2, 18, 0}));
}

EPDJ_TEST(transport_clock_ticks_only_count_while_playing) {
  TransportFsm f;
  for (int i = 0; i < 50; ++i) f.onClockTick();
  CHECK_EQ(f.songPositionClocks(), 0u);
  f.handle(Event::Play);
  for (int i = 0; i < 12; ++i) f.onClockTick();
  f.handle(Event::Pause);
  for (int i = 0; i < 50; ++i) f.onClockTick();
  CHECK_EQ(f.songPositionClocks(), 12u);
  f.handle(Event::Stop);
  f.handle(Event::Play);  // Start restarts from the top
  CHECK_EQ(f.songPositionClocks(), 0u);
}

EPDJ_TEST(transport_song_position_wraps_at_14_bits) {
  TransportFsm f;
  f.handle(Event::Play);
  for (uint32_t i = 0; i < 16383u * 6u + 3u; ++i) f.onClockTick();
  CHECK_EQ(f.songPositionBeats(), 16383);
  CHECK_BYTES(outputBytes(f.handle(Event::Pause)), (Bytes{0xFC, 0xF2, 127, 127}));
  f.handle(Event::Play);
  for (int i = 0; i < 6; ++i) f.onClockTick();  // one more beat wraps to 0
  CHECK_EQ(f.songPositionBeats(), 0);
  CHECK_BYTES(outputBytes(f.handle(Event::Pause)), (Bytes{0xFC, 0xF2, 0, 0}));
}

EPDJ_TEST(transport_toggle_and_absolute_requests) {
  TransportFsm f;
  CHECK_BYTES(outputBytes(f.toggle()), (Bytes{0xFA}));
  for (int i = 0; i < 6; ++i) f.onClockTick();
  CHECK_BYTES(outputBytes(f.toggle()), (Bytes{0xFC, 0xF2, 1, 0}));
  CHECK_BYTES(outputBytes(f.toggle()), (Bytes{0xFB}));
  // transport CC: 1 = play (already), 2 = pause, 0 = stop.
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Playing)), (Bytes{}));
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Paused)), (Bytes{0xFC, 0xF2, 1, 0}));
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Paused)), (Bytes{}));
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Stopped)), (Bytes{0xF2, 0, 0}));
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Stopped)), (Bytes{}));
  CHECK_BYTES(outputBytes(f.goTo(TransportState::Playing)), (Bytes{0xFA}));
}

EPDJ_TEST(transport_random_events_keep_invariants) {
  uint32_t s = 7;
  auto rnd = [&s](uint32_t n) { s = s * 1664525u + 1013904223u; return (s >> 12) % n; };
  TransportFsm f;
  for (int i = 0; i < 20000; ++i) {
    const uint32_t r = rnd(8);
    if (r < 4) { f.onClockTick(); continue; }
    const auto o = f.handle(static_cast<Event>(r - 4));
    CHECK(o.count <= 3);
    // The output always matches the new state: a Start or Continue leaves it playing, a Stop leaves it not.
    if (o.count > 0) {
      const uint8_t last = o.msg[o.count - 1].b[0];
      if (last == 0xFA || last == 0xFB) CHECK(f.state() == TransportState::Playing);
      if (last == 0xFC || last == 0xF2) CHECK(f.state() != TransportState::Playing);
    }
    CHECK(f.songPositionBeats() < 16384);
    if (f.state() == TransportState::Stopped) CHECK_EQ(f.songPositionClocks(), 0u);
  }
}

// ---- MTC ----

EPDJ_TEST(mtc_matches_vectors) {
  for (const auto& v : epdj_vec::mtcVectors()) {
    Timecode t;
    t.hours = static_cast<uint8_t>(v.h);
    t.minutes = static_cast<uint8_t>(v.m);
    t.seconds = static_cast<uint8_t>(v.s);
    t.frames = static_cast<uint8_t>(v.f);
    const uint8_t type = static_cast<uint8_t>(v.fpsType);
    CHECK_EQ(v.quarterFrames.size(), 8u);
    for (uint8_t piece = 0; piece < 8; ++piece) {
      CHECK_EQ(v.quarterFrames[piece][0], 0xF1);
      CHECK_EQ(MtcGenerator::quarterFrameByte(t, type, piece), v.quarterFrames[piece][1]);
    }
    uint8_t ff[MtcGenerator::kFullFrameBytes];
    CHECK_EQ(MtcGenerator::encodeFullFrame(t, type, ff), 10u);
    CHECK_BYTES(Bytes(ff, ff + 10), v.fullFrame);
  }
}

EPDJ_TEST(mtc_non_drop_frame_counting) {
  struct Row { uint8_t type; uint64_t frames; Timecode want; };
  const Row rows[] = {
      {0, 0, {0, 0, 0, 0}},
      {0, 23, {0, 0, 0, 23}},
      {0, 24, {0, 0, 1, 0}},
      {0, 24 * 3600 + 5, {1, 0, 0, 5}},
      {1, 25 * 60 * 61 + 24, {1, 1, 0, 24}},  // 61 minutes -> 01:01:00:24
      {3, 30 * 3600 * 23 + 30 * 60 * 59 + 30 * 59 + 29, {23, 59, 59, 29}},
      {3, 30 * 3600 * 24, {0, 0, 0, 0}},  // wraps at 24 h
      {0, 24ull * 3600 * 24 + 3, {0, 0, 0, 3}},
      {1, 25ull * 3600 * 24, {0, 0, 0, 0}},
  };
  for (const Row& r : rows) {
    const Timecode got = MtcGenerator::toTimecode(r.frames, r.type);
    if (!(got == r.want)) std::printf("    type %u frames %llu -> %u:%u:%u:%u\n", r.type, static_cast<unsigned long long>(r.frames), got.hours, got.minutes, got.seconds, got.frames);
    CHECK(got == r.want);
  }
}

EPDJ_TEST(mtc_drop_frame_counting) {
  struct Row { uint64_t frames; Timecode want; };
  const Row rows[] = {
      {0, {0, 0, 0, 0}},
      {1799, {0, 0, 59, 29}},
      {1800, {0, 1, 0, 2}},      // :00 and :01 are skipped at minute 1
      {1801, {0, 1, 0, 3}},
      {3597, {0, 1, 59, 29}},
      {3598, {0, 2, 0, 2}},
      {17981, {0, 9, 59, 29}},
      {17982, {0, 10, 0, 0}},    // every tenth minute keeps its frames
      {17983, {0, 10, 0, 1}},
      {107892, {1, 0, 0, 0}},    // 6 x 17982 = exactly one hour of DF timecode
      {107892 + 1800, {1, 1, 0, 2}},
      {144ull * 17982, {0, 0, 0, 0}},  // 24 h of DF wraps
      {144ull * 17982 - 1, {23, 59, 59, 29}},
  };
  for (const Row& r : rows) {
    const Timecode got = MtcGenerator::toTimecode(r.frames, 2);
    if (!(got == r.want)) std::printf("    DF frames %llu -> %u:%u:%u:%u\n", static_cast<unsigned long long>(r.frames), got.hours, got.minutes, got.seconds, got.frames);
    CHECK(got == r.want);
  }
}

EPDJ_TEST(mtc_drop_frame_labels_are_strictly_increasing_and_skip_only_the_right_ones) {
  // Walk 2 hours of frame counts: each label follows the previous one, and the only labels that
  // never appear are ;00 and ;01 at the start of minutes that are not a multiple of 10.
  Timecode prev = MtcGenerator::toTimecode(0, 2);
  uint64_t skipped = 0;
  for (uint64_t n = 1; n < 2ull * 3600 * 30; ++n) {
    const Timecode t = MtcGenerator::toTimecode(n, 2);
    const uint32_t a = ((prev.hours * 60u + prev.minutes) * 60u + prev.seconds) * 30u + prev.frames;
    const uint32_t b = ((t.hours * 60u + t.minutes) * 60u + t.seconds) * 30u + t.frames;
    CHECK(b > a);
    const uint32_t gap = b - a - 1;
    if (gap != 0) {
      CHECK_EQ(gap, 2u);
      CHECK_EQ(t.seconds, 0);
      CHECK_EQ(t.frames, 2);
      CHECK(t.minutes % 10 != 0);
    }
    skipped += gap;
    prev = t;
  }
  // Every minute boundary crossed drops 2 labels, except each tenth minute.
  const uint32_t minutes = prev.hours * 60u + prev.minutes;
  CHECK_EQ(skipped, 2u * (minutes - minutes / 10u));
}

EPDJ_TEST(mtc_time_follows_playing_time_for_every_rate) {
  struct Row { uint8_t type; uint64_t elapsedUs; Timecode want; };
  const Row rows[] = {
      {0, 1000000, {0, 0, 1, 0}},
      {0, 999999, {0, 0, 0, 23}},
      {1, 1000000, {0, 0, 1, 0}},
      {3, 1000000, {0, 0, 1, 0}},
      {3, 33333, {0, 0, 0, 0}},
      {3, 33334, {0, 0, 0, 1}},
      {2, 1000000, {0, 0, 0, 29}},          // 29.97: the 30th frame has not started yet
      {2, 1001000, {0, 0, 1, 0}},           // 30 frames take 1.001 s
      {2, 3600000000ull, {1, 0, 0, 0}},     // one real hour in drop-frame reads 01:00:00;00
      {3, 3600000000ull, {1, 0, 0, 0}},
      {0, 90000000ull * 1000, {1, 0, 0, 0}},  // 25 h wraps to 01:00:00
  };
  for (const Row& r : rows) {
    MtcGenerator g;
    g.setFps(r.type, 0);
    g.play(5000);
    const Timecode got = g.timecode(5000 + r.elapsedUs);
    if (!(got == r.want)) std::printf("    type %u at %llu us -> %u:%u:%u:%u\n", r.type, static_cast<unsigned long long>(r.elapsedUs), got.hours, got.minutes, got.seconds, got.frames);
    CHECK(got == r.want);
  }
}

namespace {
struct Qf {
  uint64_t t;
  uint8_t data;
};
// Polls advance() every `step` us for `durationUs`.
std::vector<Qf> collect(MtcGenerator& g, uint64_t from, uint64_t durationUs, uint64_t step) {
  std::vector<Qf> out;
  uint8_t buf[16];
  for (uint64_t t = from; t <= from + durationUs; t += step) {
    const size_t n = g.advance(t, buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) out.push_back({t, buf[i]});
  }
  return out;
}
}  // namespace

EPDJ_TEST(mtc_quarter_frames_are_paced_four_per_frame_and_carry_the_right_time) {
  const double fps[4] = {24.0, 25.0, 30000.0 / 1001.0, 30.0};
  for (uint8_t type = 0; type < 4; ++type) {
    MtcGenerator g;
    g.setFps(type, 0);
    g.play(1000);
    const auto qf = collect(g, 1000, 20000000, 100);  // 20 s polled every 100 us
    const double expected = 20.0 * fps[type] * 4.0;
    CHECK(std::fabs(static_cast<double>(qf.size()) - expected) <= 1.0);
    CHECK_EQ(qf[0].t, 1000u);  // piece 0 goes out the moment play starts

    const double step = 1e6 / (fps[type] * 4.0);
    for (size_t i = 0; i < qf.size(); ++i) {
      CHECK_EQ(qf[i].data >> 4, i % 8);  // pieces 0..7 in order, forever
      const double ideal = 1000.0 + static_cast<double>(i) * step;
      CHECK(static_cast<double>(qf[i].t) >= ideal - 1e-6);
      CHECK(static_cast<double>(qf[i].t) < ideal + 100.0 + 1e-6);  // late by at most one poll
    }
    // Reassemble each group of 8 and compare with the time of its first frame (every second frame from the rewind).
    for (size_t g8 = 0; g8 + 8 <= qf.size(); g8 += 8) {
      uint8_t n[8];
      for (size_t k = 0; k < 8; ++k) n[k] = qf[g8 + k].data & 0x0F;
      Timecode t;
      t.frames = static_cast<uint8_t>(n[0] | (n[1] << 4));
      t.seconds = static_cast<uint8_t>(n[2] | (n[3] << 4));
      t.minutes = static_cast<uint8_t>(n[4] | (n[5] << 4));
      t.hours = static_cast<uint8_t>(n[6] | ((n[7] & 1) << 4));
      CHECK_EQ((n[7] >> 1) & 3, type);
      CHECK(t == MtcGenerator::toTimecode(g8 / 8 * 2, type));
      if (type != 1) CHECK_EQ(t.frames % 2, 0);  // at 25 fps the label parity alternates; the frame count is what is even
    }
  }
}

EPDJ_TEST(mtc_pause_holds_time_and_silences_quarter_frames_and_resume_continues_the_sequence) {
  MtcGenerator g;
  g.setFps(3, 0);
  g.play(0);
  auto a = collect(g, 0, 1010000, 1000);  // a hair over 1 s: mid-sequence on purpose
  g.pause(1010000);
  const Timecode held = g.timecode(1010000);
  CHECK(!g.playing());
  CHECK(g.timecode(9999999) == held);
  CHECK(collect(g, 1020000, 5000000, 1000).empty());
  g.play(7000000);
  CHECK(g.timecode(7000000) == held);
  auto b = collect(g, 7000000, 1000000, 1000);
  CHECK(!a.empty() && !b.empty());
  // Piece numbers continue where they left off, no repeat, no gap.
  CHECK_EQ(b.front().data >> 4, ((a.back().data >> 4) + 1) & 7);
  for (size_t i = 1; i < b.size(); ++i) CHECK_EQ(b[i].data >> 4, ((b[i - 1].data >> 4) + 1) & 7);
  CHECK(g.timecode(8000000).seconds >= 1);
}

EPDJ_TEST(mtc_rewind_restarts_at_piece_zero_and_keeps_running) {
  MtcGenerator g;
  g.setFps(1, 0);
  g.play(0);
  collect(g, 0, 777000, 500);
  CHECK(g.timecode(777000).seconds == 0 && g.timecode(777000).frames > 0);
  g.rewind(777500);
  CHECK(g.playing());
  CHECK(g.timecode(777500) == Timecode());
  uint8_t buf[4];
  CHECK_EQ(g.advance(777500, buf, sizeof(buf)), 1u);
  CHECK_EQ(buf[0], 0x00);  // piece 0, frames low nibble 0
  CHECK_EQ(g.timecode(777500 + 1000000).seconds, 1);
}

EPDJ_TEST(mtc_transport_event_mapping) {
  MtcGenerator g;
  g.onTransportEvent(Event::Play, 100);
  CHECK(g.playing());
  g.onTransportEvent(Event::Play, 200);  // idempotent
  CHECK_EQ(g.timecode(2000100).seconds, 2);
  g.onTransportEvent(Event::Pause, 2000100);
  CHECK(!g.playing());
  g.onTransportEvent(Event::Pause, 3000000);
  CHECK_EQ(g.timecode(9000000).seconds, 2);
  g.onTransportEvent(Event::Play, 9000000);
  g.onTransportEvent(Event::Rewind, 9500000);  // rewinds but keeps running
  CHECK(g.playing());
  CHECK(g.timecode(9500000) == Timecode());
  g.onTransportEvent(Event::Stop, 9600000);
  CHECK(!g.playing());
  CHECK(g.timecode(20000000) == Timecode());
}

EPDJ_TEST(mtc_full_frame_reports_current_time_and_rate) {
  MtcGenerator g;
  g.setFps(2, 0);
  g.play(0);
  uint8_t ff[MtcGenerator::kFullFrameBytes];
  CHECK_EQ(g.fullFrame(3600000000ull, ff), 10u);
  CHECK_BYTES(Bytes(ff, ff + 10), (Bytes{0xF0, 0x7F, 0x7F, 0x01, 0x01, static_cast<uint8_t>((2 << 5) | 1), 0, 0, 0, 0xF7}));
}

EPDJ_TEST(mtc_set_fps_validates_and_rewinds_on_change) {
  MtcGenerator g;
  CHECK_EQ(g.fpsType(), 3);
  g.setFps(7, 0);
  CHECK_EQ(g.fpsType(), 3);
  g.play(0);
  g.setFps(3, 500000);  // unchanged: keeps running time
  CHECK_EQ(g.timecode(1000000).seconds, 1);
  g.setFps(0, 1000000);
  CHECK_EQ(g.fpsType(), 0);
  CHECK(g.timecode(1000000) == Timecode());
}

EPDJ_TEST(mtc_quarter_frame_backlog_is_skipped_to_the_next_sequence_and_maxout_is_respected) {
  MtcGenerator g;
  g.setFps(3, 0);  // 120 quarter frames per second
  g.play(0);
  uint8_t buf[4];
  CHECK_EQ(g.advance(20000, buf, 0), 0u);  // maxOut 0 writes nothing
  CHECK_EQ(g.advance(20000, buf, 2), 2u);  // 3 are due (pieces 0-2); only 2 fit
  CHECK_EQ(buf[0] >> 4, 0);
  CHECK_EQ(buf[1] >> 4, 1);
  CHECK_EQ(g.advance(20000, buf, 4), 1u);  // the third is still waiting
  CHECK_EQ(buf[0] >> 4, 2);
  // 10.05 s later the generator is hundreds of pieces behind: it skips to the next sequence start
  // (piece 0 of the group after the current time) instead of replaying them.
  uint8_t big[16];
  CHECK_EQ(g.advance(10050000, big, sizeof(big)), 0u);
  const auto next = collect(g, 10050000, 200000, 500);
  CHECK(!next.empty());
  CHECK_EQ(next.front().data >> 4, 0);
  for (size_t i = 1; i < next.size(); ++i) CHECK_EQ(next[i].data >> 4, i % 8);
  CHECK(next.front().t >= 10050000);
}

// ---- Tap tempo ----

EPDJ_TEST(tap_tempo_averages_the_last_four_intervals) {
  TapTempo t;
  uint64_t now = 1000000;
  CHECK_EQ(t.tap(now), 0);  // first tap: nothing to average
  now += 500000;
  CHECK_EQ(t.tap(now), 1200);  // 120 BPM
  now += 500000; CHECK_EQ(t.tap(now), 1200);
  now += 500000; CHECK_EQ(t.tap(now), 1200);
  now += 500000; CHECK_EQ(t.tap(now), 1200);
  // Speeding up: the window slides over 4 intervals.
  now += 400000;  // (500,500,500,400) = 475 ms
  CHECK_EQ(t.tap(now), 1263);
  now += 400000;  // (500,500,400,400) = 450
  CHECK_EQ(t.tap(now), 1333);
  now += 400000;  // (500,400,400,400) = 425
  CHECK_EQ(t.tap(now), 1412);
  now += 400000;  // (400 x 4)
  CHECK_EQ(t.tap(now), 1500);
  now += 400000;
  CHECK_EQ(t.tap(now), 1500);  // old intervals fully forgotten
}

EPDJ_TEST(tap_tempo_partial_windows_and_rounding) {
  TapTempo t;
  CHECK_EQ(t.tap(0), 0);
  CHECK_EQ(t.tap(461538), 1300);  // 130.0 BPM: 600e6 / 461538 = 1300.0004
  CHECK_EQ(t.tap(461538 + 461539), 1300);
  TapTempo u;
  u.tap(0);
  CHECK_EQ(u.tap(480000), 1250);
  CHECK_EQ(u.tap(480000 + 520000), 1200);  // mean of 480 and 520 ms = 500 ms
}

EPDJ_TEST(tap_tempo_two_second_boundary) {
  {
    TapTempo t;
    t.tap(0);
    CHECK_EQ(t.tap(1999999), 300);  // 30.0 BPM, the slowest tappable tempo
  }
  {
    TapTempo t;
    t.tap(0);
    CHECK_EQ(t.tap(2000000), 0);  // gap of exactly 2 s: this tap starts a new series
    CHECK_EQ(t.tap(2500000), 1200);
  }
  {
    TapTempo t;
    t.tap(0); t.tap(500000); t.tap(1000000);
    CHECK_EQ(t.tap(3500000), 0);  // after the long pause, old intervals do not leak in
    CHECK_EQ(t.tap(3900000), 1500);
  }
}

EPDJ_TEST(tap_tempo_clamps_and_survives_odd_timestamps) {
  TapTempo t;
  t.tap(1000);
  CHECK_EQ(t.tap(1000 + 100000), 3000);  // 600 BPM -> clamped to 300
  TapTempo same;
  same.tap(5000);
  CHECK_EQ(same.tap(5000), 3000);  // zero interval: no division by zero
  TapTempo back;
  back.tap(9000);
  CHECK_EQ(back.tap(8000), 0);  // time went backwards: new series
  CHECK_EQ(back.tap(8000 + 500000), 1200);
  t.reset();
  CHECK_EQ(t.tap(0), 0);
}

// ---- master wiring: the pieces together, the way the engine will use them ----

EPDJ_TEST(master_session_emits_the_specified_message_sequence) {
  ClockGenerator clk;
  TransportFsm fsm;
  MtcGenerator mtc;
  mtc.setFps(3, 0);
  uint64_t now = 0;
  clk.start(now, 1200);  // 120 BPM: 48 clocks/s

  Bytes wire;
  size_t qfCount = 0, fullFrames = 0;
  auto emit = [&](const midi::Message& m) { const Bytes b = bytesOf(m); wire.insert(wire.end(), b.begin(), b.end()); };
  auto transport = [&](Event e) {
    const auto o = fsm.handle(e);
    for (uint8_t i = 0; i < o.count; ++i) emit(o.msg[i]);
    mtc.onTransportEvent(e, now);
    if (o.count) {
      uint8_t ff[10];
      mtc.fullFrame(now, ff);
      fullFrames += 1;
      CHECK_EQ(ff[0], 0xF0);
    }
  };
  auto run = [&](uint64_t durationUs) {
    const uint64_t end = now + durationUs;
    for (; now < end; now += 1000) {
      for (uint32_t n = clk.advance(now); n; --n) {
        emit(midi::clock());
        fsm.onClockTick();
      }
      uint8_t qf[8];
      qfCount += mtc.advance(now, qf, sizeof(qf));
    }
  };

  run(1000000);              // 1 s stopped: clocks flow (48), no MTC
  CHECK_EQ(qfCount, 0u);
  transport(Event::Play);
  run(2000000);              // 2 s playing: 96 more clocks, 240 quarter frames at 30 fps
  transport(Event::Pause);   // 96 clocks since Start = 16 beats
  run(1000000);              // paused: clocks keep flowing, MTC silent
  transport(Event::Play);
  run(1000000);              // +48 clocks = 8 beats
  transport(Event::Stop);

  size_t clocks = 0, starts = 0, conts = 0, stops = 0;
  Bytes spp;
  for (size_t i = 0; i < wire.size(); ++i) {
    switch (wire[i]) {
      case 0xF8: ++clocks; break;
      case 0xFA: ++starts; break;
      case 0xFB: ++conts; break;
      case 0xFC: ++stops; break;
      case 0xF2: spp.push_back(wire[i + 1]); spp.push_back(wire[i + 2]); i += 2; break;
      default: CHECK(false); break;
    }
  }
  CHECK(clocks >= 5 * 48 - 2 && clocks <= 5 * 48);
  CHECK_EQ(starts, 1u);
  CHECK_EQ(conts, 1u);
  CHECK_EQ(stops, 2u);
  CHECK_BYTES(spp, (Bytes{16, 0, 0, 0}));  // pause announced beat 16; stop+rewind sent 0
  CHECK(qfCount >= 3 * 30 * 4 - 8 && qfCount <= 3 * 30 * 4 + 1);  // 3 s of playing time
  CHECK_EQ(fullFrames, 4u);  // play, pause, play, stop
  // Ordering: Start precedes the first clock after it; Stop is immediately followed by its SPP.
  for (size_t i = 0; i + 1 < wire.size(); ++i) {
    if (wire[i] == 0xFC) CHECK_EQ(wire[i + 1], 0xF2);
  }
}
