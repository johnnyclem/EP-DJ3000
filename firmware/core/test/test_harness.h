// Minimal dependency-free unit-test harness (no exceptions, so it works with -fno-exceptions).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace epdj_test {
struct Case {
  const char* name;
  void (*fn)();
};
std::vector<Case>& registry();
struct Registrar {
  Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
void fail(const char* file, int line, const char* msg);
void failEq(const char* file, int line, const char* a, const char* b, long long av, long long bv);
void failBytes(const char* file, int line, const char* what, const std::vector<uint8_t>& got,
               const std::vector<uint8_t>& want);
}  // namespace epdj_test

#define EPDJ_TEST(name)                                                  \
  static void name();                                                    \
  static ::epdj_test::Registrar epdj_reg_##name(#name, &name);           \
  static void name()

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) ::epdj_test::fail(__FILE__, __LINE__, #c);  \
  } while (0)

#define CHECK_EQ(a, b)                                                                              \
  do {                                                                                              \
    long long epdj_a_ = static_cast<long long>(a), epdj_b_ = static_cast<long long>(b);             \
    if (epdj_a_ != epdj_b_) ::epdj_test::failEq(__FILE__, __LINE__, #a, #b, epdj_a_, epdj_b_);     \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                                       \
  do {                                                                                              \
    double epdj_a_ = static_cast<double>(a), epdj_b_ = static_cast<double>(b);                      \
    if (!(std::fabs(epdj_a_ - epdj_b_) <= static_cast<double>(eps)))                                \
      ::epdj_test::fail(__FILE__, __LINE__, #a " not near " #b);                                    \
  } while (0)

#define CHECK_BYTES(got, want)                                                             \
  do {                                                                                     \
    if (!((got) == (want))) ::epdj_test::failBytes(__FILE__, __LINE__, #got, (got), (want)); \
  } while (0)
