#include "epdj/util.h"
#include "test_harness.h"

using namespace epdj;

EPDJ_TEST(clamp_bounds) {
  CHECK_EQ(epdj::clamp(5, 0, 10), 5);
  CHECK_EQ(epdj::clamp(-5, 0, 10), 0);
  CHECK_EQ(epdj::clamp(15, 0, 10), 10);
  CHECK_NEAR(epdj::clamp(0.25f, 0.0f, 1.0f), 0.25, 1e-9);
}

EPDJ_TEST(sat_add_saturates_both_ways) {
  CHECK_EQ(satAdd32(1, 2), 3);
  CHECK_EQ(satAdd32(INT32_MAX, 1), INT32_MAX);
  CHECK_EQ(satAdd32(INT32_MAX - 5, 10), INT32_MAX);
  CHECK_EQ(satAdd32(INT32_MIN, -1), INT32_MIN);
  CHECK_EQ(satAdd32(INT32_MIN + 3, -10), INT32_MIN);
  CHECK_EQ(satAdd32(INT32_MAX, INT32_MIN), -1);
}

EPDJ_TEST(angle_diff_wraps_shortest_way) {
  CHECK_EQ(angleDiff4096(0, 0), 0);
  CHECK_EQ(angleDiff4096(10, 14), 4);
  CHECK_EQ(angleDiff4096(14, 10), -4);
  CHECK_EQ(angleDiff4096(4095, 0), 1);
  CHECK_EQ(angleDiff4096(0, 4095), -1);
  CHECK_EQ(angleDiff4096(4090, 5), 11);
  CHECK_EQ(angleDiff4096(5, 4090), -11);
  // Exactly half a turn maps to -2048 (the range is [-2048, 2047]).
  CHECK_EQ(angleDiff4096(0, 2048), -2048);
  CHECK_EQ(angleDiff4096(2048, 0), -2048);
  CHECK_EQ(angleDiff4096(0, 2047), 2047);
}

EPDJ_TEST(angle_diff_is_antisymmetric_and_in_range) {
  uint32_t s = 12345;
  for (int i = 0; i < 20000; ++i) {
    s = s * 1664525u + 1013904223u;
    const uint16_t a = static_cast<uint16_t>((s >> 8) & 4095);
    s = s * 1664525u + 1013904223u;
    const uint16_t b = static_cast<uint16_t>((s >> 8) & 4095);
    const int32_t d = angleDiff4096(a, b);
    CHECK(d >= -2048 && d <= 2047);
    CHECK_EQ(((a + d) % 4096 + 4096) % 4096, b);
    if (d != -2048) CHECK_EQ(angleDiff4096(b, a), -d);
  }
}

EPDJ_TEST(adaptive_alpha_shape) {
  const float rest = 0.05f;
  CHECK_NEAR(adaptiveAlpha(0.0f, rest, 4.0f, 8.0f), rest, 1e-9);
  CHECK_NEAR(adaptiveAlpha(4.0f, rest, 4.0f, 8.0f), rest, 1e-9);
  CHECK_NEAR(adaptiveAlpha(8.0f, rest, 4.0f, 8.0f), 1.0, 1e-9);
  CHECK_NEAR(adaptiveAlpha(100.0f, rest, 4.0f, 8.0f), 1.0, 1e-9);
  float prev = 0.0f;
  for (float m = 0.0f; m < 12.0f; m += 0.1f) {
    const float a = adaptiveAlpha(m, rest, 4.0f, 8.0f);
    CHECK(a >= prev);
    CHECK(a >= rest && a <= 1.0f);
    prev = a;
  }
}

EPDJ_TEST(low_pass_alpha_range) {
  CHECK_NEAR(lowPassAlpha(0.001f, 0.001f), 0.5, 1e-6);
  CHECK(lowPassAlpha(0.001f, 5.0f) < 0.001f);
  CHECK(lowPassAlpha(0.001f, 0.0f) == 1.0f);
}

EPDJ_TEST(dt_seconds_is_clamped_and_monotonic_safe) {
  CHECK_NEAR(dtSeconds(2000, 1000), 0.001, 1e-9);
  CHECK_NEAR(dtSeconds(1000, 1000), 0.0001, 1e-9);        // repeated timestamp -> 100 us floor
  CHECK_NEAR(dtSeconds(500, 1000), 0.0001, 1e-9);         // going backwards -> floor, never a huge wrap
  CHECK_NEAR(dtSeconds(50, 0), 0.0001, 1e-9);
  CHECK_NEAR(dtSeconds(1000000, 0), 0.02, 1e-9);          // a 1 s stall -> 20 ms ceiling
  CHECK_NEAR(dtSeconds(UINT64_MAX, 0), 0.02, 1e-9);
  CHECK_NEAR(dtSeconds(5000000000ull + 1500, 5000000000ull), 0.0015, 1e-9);  // beyond 32-bit microseconds
}
