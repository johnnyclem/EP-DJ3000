// When the engine task asks an AS5600 that is absent or failing again, and what that may cost. Pure timing policy,
// header-only and free of ESP-IDF so that tools/host_checks tests it. The idea (retry every 500 ms; the original Field Jog
// firmware also re-probes a missing chip at that interval) is the original's; the policy around it is ours.
//
//   * While the chip answers, every read is made. One failed read is tolerated (a loose wire, a glitch); the second in a
//     row declares the chip LOST.
//   * While lost, or absent since boot, no bus traffic at all for kRetryIntervalUs, then ONE trial read. A trial that
//     fails costs one read's timeout and starts the next wait; one that succeeds says RECOVERED and normal reads resume.
//
// Cost bound: a failed read blocks the 1 kHz loop for at most the I2C driver's timeout (2 ms in the HAL), so a dead chip
// costs at most two such reads (4 ms) when it dies and one (2 ms) per 500 ms after that: 0.4 %. The previous policy was
// 4 ms in every 100 (4 %), and nothing at all for a chip that never answered at boot.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {

class EncoderRetry {
 public:
  static constexpr unsigned kFailuresBeforeLost = 2;
  static constexpr uint64_t kRetryIntervalUs = 500000;

  enum class Change : uint8_t {
    None,
    Lost,       // the chip was answering and has now failed twice in a row
    Recovered,  // a trial read succeeded: the chip is back, or has answered for the first time since boot
  };

  // `answering`: did the chip answer at boot. If not, the first trial is one interval from now.
  void start(bool answering, uint64_t nowUs) {
    answering_ = answering;
    failStreak_ = 0;
    retryAtUs_ = answering ? 0 : nowUs + kRetryIntervalUs;
  }

  // May the caller touch the bus now? Always while the chip answers; once per interval otherwise.
  bool mayRead(uint64_t nowUs) const { return answering_ || nowUs >= retryAtUs_; }
  bool answering() const { return answering_; }

  // Report the outcome of a read that mayRead() allowed.
  Change report(bool ok, uint64_t nowUs) {
    if (answering_) {
      if (ok) {
        failStreak_ = 0;
        return Change::None;
      }
      if (++failStreak_ < kFailuresBeforeLost) return Change::None;
      answering_ = false;
      failStreak_ = 0;
      retryAtUs_ = nowUs + kRetryIntervalUs;
      return Change::Lost;
    }
    if (ok) {
      answering_ = true;
      failStreak_ = 0;
      return Change::Recovered;
    }
    retryAtUs_ = nowUs + kRetryIntervalUs;
    return Change::None;
  }

 private:
  bool answering_ = false;
  unsigned failStreak_ = 0;
  uint64_t retryAtUs_ = 0;
};

}  // namespace hal
}  // namespace epdj
