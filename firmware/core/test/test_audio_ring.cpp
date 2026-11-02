// SpscAudioRing: FIFO semantics, 32-bit index wrap, counters, a model check against std::deque, and a
// real two-thread stress run (the sanitizer build catches data races on the payload indirectly through
// content corruption; ordering is verified frame by frame).
#include <atomic>
#include <deque>
#include <thread>
#include <vector>

#include "epdj/audio_rate.h"
#include "test_harness.h"

using namespace epdj::audio;

namespace {

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

// A frame whose two samples encode a 32-bit sequence number.
void put(int16_t* f, uint32_t n) {
  f[0] = static_cast<int16_t>(n & 0xFFFF);
  f[1] = static_cast<int16_t>(n >> 16);
}
uint32_t get(const int16_t* f) {
  return static_cast<uint32_t>(static_cast<uint16_t>(f[0])) | (static_cast<uint32_t>(static_cast<uint16_t>(f[1])) << 16);
}

}  // namespace

EPDJ_TEST(ring_basic_fifo_and_counters) {
  int16_t store[16 * 2];
  SpscAudioRing r(store, 16);
  CHECK(r.valid());
  CHECK_EQ(r.capacity(), 16);
  CHECK_EQ(r.fillFrames(), 0);
  CHECK_EQ(r.writableFrames(), 16);

  int16_t in[20 * 2], out[20 * 2];
  for (uint32_t i = 0; i < 20; ++i) put(in + 2 * i, i);
  CHECK_EQ(r.write(in, 10), 10);
  CHECK_EQ(r.fillFrames(), 10);
  CHECK_EQ(r.readableFrames(), 10);
  CHECK_EQ(r.writableFrames(), 6);
  CHECK_EQ(r.read(out, 4), 4);
  for (uint32_t i = 0; i < 4; ++i) CHECK_EQ(get(out + 2 * i), i);

  // Fill to the brim: 6 more fit, the rest is refused and counted, nothing unread is overwritten.
  CHECK_EQ(r.write(in + 20, 10), 10);  // frames 10..19: only 10 free (16 - 6)
  CHECK_EQ(r.fillFrames(), 16);
  CHECK_EQ(r.write(in, 5), 0);
  CHECK_EQ(r.overrunFrames(), 5);
  CHECK_EQ(r.read(out, 20), 16);
  for (uint32_t i = 0; i < 16; ++i) CHECK_EQ(get(out + 2 * i), 4 + i);
  CHECK_EQ(r.underrunFrames(), 4);  // asked for 20, got 16
  CHECK_EQ(r.read(out, 3), 0);
  CHECK_EQ(r.underrunFrames(), 7);
  CHECK_EQ(r.fillFrames(), 0);

  CHECK_EQ(r.write(in, 8), 8);
  CHECK_EQ(r.discard(3), 3);
  CHECK_EQ(r.read(out, 1), 1);
  CHECK_EQ(get(out), 3);
  CHECK_EQ(r.discard(100), 4);
  CHECK_EQ(r.fillFrames(), 0);

  r.reset();
  CHECK_EQ(r.overrunFrames(), 0);
  CHECK_EQ(r.underrunFrames(), 0);
  CHECK_EQ(r.write(in, 0), 0);
  CHECK_EQ(r.read(out, 0), 0);
}

EPDJ_TEST(ring_rejects_bad_capacity) {
  int16_t store[64];
  SpscAudioRing a(store, 12), b(store, 1), c(store, 0), d(nullptr, 16);
  for (SpscAudioRing* r : {&a, &b, &c, &d}) {
    CHECK(!r->valid());
    CHECK_EQ(r->capacity(), 0);
    int16_t f[2] = {1, 2};
    CHECK_EQ(r->write(f, 1), 0);
    CHECK_EQ(r->read(f, 1), 0);
    CHECK_EQ(r->fillFrames(), 0);
    CHECK_EQ(r->writableFrames(), 0);
  }
  SpscAudioRing ok(store, 2);
  CHECK(ok.valid());
  StaticSpscAudioRing<256> s;
  CHECK(s.valid());
  CHECK_EQ(s.capacity(), 256);
  int16_t f[8] = {1, 2, 3, 4, 5, 6, 7, 8}, g[8] = {};
  CHECK_EQ(s.write(f, 4), 4);
  CHECK_EQ(s.read(g, 4), 4);
  CHECK_EQ(g[7], 8);
}

// The indices are free-running 32-bit counters: starting just below 2^32 the data crosses the wrap
// while the ring is in every possible state.
EPDJ_TEST(ring_is_correct_across_the_32bit_index_wrap) {
  for (uint32_t start : {0xFFFFFFF0u, 0xFFFFFFFFu, 0xFFFFFF00u, 0x7FFFFFFFu, 0u}) {
    int16_t store[32 * 2];
    SpscAudioRing r(store, 32);
    r.reset(start);
    Rng rng(start ^ 0x1234u);
    uint32_t nextIn = 0, nextOut = 0;
    for (int iter = 0; iter < 2000; ++iter) {
      int16_t buf[40 * 2];
      const size_t n = rng.below(40);
      for (size_t i = 0; i < n; ++i) put(buf + 2 * i, nextIn + static_cast<uint32_t>(i));
      const size_t room = r.writableFrames();
      const size_t w = r.write(buf, n);
      CHECK_EQ(w, n < room ? n : room);
      nextIn += static_cast<uint32_t>(w);
      const size_t m = rng.below(40);
      const size_t avail = r.readableFrames();
      const size_t g = r.read(buf, m);
      CHECK_EQ(g, m < avail ? m : avail);
      for (size_t i = 0; i < g; ++i) CHECK_EQ(get(buf + 2 * i), nextOut + static_cast<uint32_t>(i));
      nextOut += static_cast<uint32_t>(g);
      CHECK_EQ(r.fillFrames(), nextIn - nextOut);
      CHECK(r.fillFrames() <= 32);
    }
    CHECK(nextIn > 10000);  // plenty of traffic across the wrap
  }
}

EPDJ_TEST(ring_matches_a_deque_model) {
  int16_t store[64 * 2];
  SpscAudioRing r(store, 64);
  std::deque<uint32_t> model;
  Rng rng(9);
  uint32_t seq = 0;
  for (int iter = 0; iter < 20000; ++iter) {
    int16_t buf[100 * 2];
    switch (rng.below(3)) {
      case 0: {
        const size_t n = rng.below(100);
        for (size_t i = 0; i < n; ++i) put(buf + 2 * i, seq + static_cast<uint32_t>(i));
        const size_t w = r.write(buf, n);
        CHECK_EQ(w, n < 64 - model.size() ? n : 64 - model.size());
        for (size_t i = 0; i < w; ++i) model.push_back(seq + static_cast<uint32_t>(i));
        seq += static_cast<uint32_t>(w);
        break;
      }
      case 1: {
        const size_t n = rng.below(100);
        const size_t g = r.read(buf, n);
        CHECK_EQ(g, n < model.size() ? n : model.size());
        for (size_t i = 0; i < g; ++i) {
          CHECK_EQ(get(buf + 2 * i), model.front());
          model.pop_front();
        }
        break;
      }
      default: {
        const size_t n = rng.below(100);
        const size_t g = r.discard(n);
        CHECK_EQ(g, n < model.size() ? n : model.size());
        for (size_t i = 0; i < g; ++i) model.pop_front();
      }
    }
    CHECK_EQ(r.fillFrames(), model.size());
  }
}

EPDJ_TEST(ring_two_threads_deliver_every_frame_in_order) {
  StaticSpscAudioRing<1024> ring;
  constexpr uint32_t kTotal = 2000000;
  std::atomic<bool> bad{false};

  std::thread producer([&] {
    Rng rng(1);
    uint32_t next = 0;
    int16_t buf[96 * 2];
    while (next < kTotal) {
      size_t n = 1 + rng.below(96);
      if (n > kTotal - next) n = kTotal - next;
      for (size_t i = 0; i < n; ++i) put(buf + 2 * i, next + static_cast<uint32_t>(i));
      size_t done = 0;
      while (done < n) {
        const size_t w = ring.write(buf + 2 * done, n - done);
        done += w;
        if (w == 0) std::this_thread::yield();
      }
      next += static_cast<uint32_t>(n);
    }
  });
  std::thread consumer([&] {
    Rng rng(2);
    uint32_t expect = 0;
    int16_t buf[96 * 2];
    while (expect < kTotal) {
      const size_t n = 1 + rng.below(96);
      const size_t g = ring.readableFrames() >= n ? ring.read(buf, n) : 0;
      if (g == 0) {
        std::this_thread::yield();
        continue;
      }
      for (size_t i = 0; i < g; ++i) {
        if (get(buf + 2 * i) != expect + static_cast<uint32_t>(i)) bad = true;
      }
      expect += static_cast<uint32_t>(g);
    }
  });
  producer.join();
  consumer.join();
  CHECK(!bad.load());
  CHECK_EQ(ring.fillFrames(), 0);
}
