#include "test_harness.h"

namespace epdj_test {
static int g_caseFailures = 0;
std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
void fail(const char* file, int line, const char* msg) {
  std::printf("    FAIL %s:%d: %s\n", file, line, msg);
  ++g_caseFailures;
}
void failEq(const char* file, int line, const char* a, const char* b, long long av, long long bv) {
  std::printf("    FAIL %s:%d: %s == %s  (%lld vs %lld)\n", file, line, a, b, av, bv);
  ++g_caseFailures;
}
void failBytes(const char* file, int line, const char* what, const std::vector<uint8_t>& got,
               const std::vector<uint8_t>& want) {
  std::printf("    FAIL %s:%d: bytes %s\n      got : ", file, line, what);
  for (uint8_t b : got) std::printf("%02X ", b);
  std::printf("\n      want: ");
  for (uint8_t b : want) std::printf("%02X ", b);
  std::printf("\n");
  ++g_caseFailures;
}
}  // namespace epdj_test

int main() {
  int failedCases = 0;
  for (const auto& c : epdj_test::registry()) {
    epdj_test::g_caseFailures = 0;
    c.fn();
    if (epdj_test::g_caseFailures) {
      std::printf("[FAIL] %s\n", c.name);
      ++failedCases;
    } else {
      std::printf("[ ok ] %s\n", c.name);
    }
  }
  std::printf("%zu tests, %d failed\n", epdj_test::registry().size(), failedCases);
  return failedCases ? 1 : 0;
}
