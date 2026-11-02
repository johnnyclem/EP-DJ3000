// OutputPump (main/output_pump.h) against fake sinks: the real delivery code, with USB behaving the way a host
// that stops reading does. The point of the policy is that a refusing USB endpoint never holds TRS MIDI and the
// radio back for more than a single give-up window.
#include <array>
#include <cstring>
#include <deque>
#include <vector>

#include "output_pump.h"
#include "test_harness.h"

using namespace epdj;
using namespace epdj::app;

namespace {

using Packet = std::array<uint8_t, 4>;

struct FakeIo {
  std::deque<OutEvent> queue;
  bool hostActive = true;
  int usbAccepts = 1 << 30;  // packets the FIFO takes before it refuses; refilled by the test
  std::vector<Packet> usb;
  unsigned trsWrites = 0, radioSends = 0, stalls = 0, drops = 0;

  bool popOut(OutEvent* ev) {
    if (queue.empty()) return false;
    *ev = queue.front();
    queue.pop_front();
    return true;
  }
  bool trsReady() const { return true; }
  void trsWrite(const uint8_t*, size_t) { ++trsWrites; }
  bool radioReady() const { return true; }
  void radioSend(const uint8_t*, size_t) { ++radioSends; }
  bool usbHostActive() const { return hostActive; }
  bool usbWrite(const uint8_t pkt[4]) {
    if (!hostActive || usbAccepts <= 0) return false;
    --usbAccepts;
    Packet p;
    std::memcpy(p.data(), pkt, 4);
    usb.push_back(p);
    return true;
  }
  void countUsbStall() { ++stalls; }
  void countUsbDrop() { ++drops; }
};

// A CC message for the USB and TRS ports; `value` identifies it in the USB stream.
OutEvent cc(uint8_t value) {
  OutEvent e;
  e.kind = OutEvent::Kind::Message;
  e.portMask = proto::clockPorts::kUsb | proto::clockPorts::kTrs;
  e.cable = 0;
  e.len = 3;
  e.data[0] = 0xB0;
  e.data[1] = 22;
  e.data[2] = value;
  return e;
}

OutEvent linkEvent() {
  OutEvent e;
  e.kind = OutEvent::Kind::Link;
  e.portMask = proto::clockPorts::kLink;
  e.len = 8;
  return e;
}

// A SysEx of `bytes` bytes (F0 ... F7) for USB: ceil(bytes / 3) packets.
OutEvent sysex(unsigned bytes) {
  OutEvent e;
  e.kind = OutEvent::Kind::SysEx;
  e.portMask = proto::clockPorts::kUsb | proto::clockPorts::kTrs;
  e.cable = 0;
  e.len = static_cast<uint16_t>(bytes);
  e.data[0] = 0xF0;
  for (unsigned i = 1; i + 1 < bytes; ++i) e.data[i] = static_cast<uint8_t>(i & 0x7F);
  e.data[bytes - 1] = 0xF7;
  return e;
}

constexpr uint64_t kMs = 1000;

}  // namespace

EPDJ_TEST(healthy_sink_gets_everything_and_nothing_is_counted) {
  FakeIo io;
  OutputPump<FakeIo> pump(io);
  for (uint8_t i = 0; i < 40; ++i) io.queue.push_back(cc(i));
  pump.drain(0);
  CHECK_EQ(io.usb.size(), 40);
  CHECK_EQ(io.trsWrites, 40);
  CHECK_EQ(io.stalls, 0);
  CHECK_EQ(io.drops, 0);
  CHECK(!pump.usbSinkStalled());
  CHECK(io.queue.empty());
}

EPDJ_TEST(per_iteration_cap_holds) {
  FakeIo io;
  OutputPump<FakeIo> pump(io);
  for (unsigned i = 0; i < 200; ++i) io.queue.push_back(cc(static_cast<uint8_t>(i & 0x7F)));
  pump.drain(0);
  CHECK_EQ(io.trsWrites, kMaxOutPerIteration);
  CHECK_EQ(io.queue.size(), 200 - kMaxOutPerIteration);
}

// The reviewed failure: a mounted host that never reads, one event per millisecond. TRS and the radio have to see
// every event; the old policy let each event wait out its own 100 ms, so ~10 events per second got through.
EPDJ_TEST(host_that_never_reads_does_not_throttle_trs_and_radio) {
  FakeIo io;
  io.usbAccepts = 0;
  OutputPump<FakeIo> pump(io);
  constexpr unsigned kEvents = 1000;
  size_t worstBacklog = 0;
  for (unsigned i = 0; i < kEvents; ++i) {
    io.queue.push_back(cc(static_cast<uint8_t>(i & 0x7F)));
    io.queue.push_back(linkEvent());
    pump.drain(i * kMs);
    if (io.queue.size() > worstBacklog) worstBacklog = io.queue.size();
  }
  // At most the one give-up window of backlog: ~100 iterations x 2 events, then it is flushed at 64 per iteration.
  CHECK(worstBacklog <= 2 * (kUsbGiveUpUs / kMs + 2));
  pump.drain(kEvents * kMs);
  pump.drain((kEvents + 1) * kMs);
  pump.drain((kEvents + 2) * kMs);
  CHECK_EQ(io.trsWrites, kEvents);
  CHECK_EQ(io.radioSends, kEvents);
  CHECK(io.queue.empty());
  CHECK_EQ(io.usb.size(), 0);
  CHECK_EQ(io.drops, kEvents);  // every USB event dropped
  CHECK(io.stalls <= kUsbGiveUpUs / kMs + 2);  // the single give-up window, not one per event
  CHECK(pump.usbSinkStalled());
}

EPDJ_TEST(a_reader_that_comes_back_is_served_again) {
  FakeIo io;
  io.usbAccepts = 0;
  OutputPump<FakeIo> pump(io);
  uint64_t t = 0;
  for (unsigned i = 0; i < 300; ++i, t += kMs) {
    io.queue.push_back(cc(1));
    pump.drain(t);
  }
  CHECK(pump.usbSinkStalled());
  const unsigned dropsBefore = io.drops;

  io.usbAccepts = 1 << 30;  // an app opens the port
  io.queue.push_back(cc(99));
  pump.drain(t);
  CHECK(!pump.usbSinkStalled());
  CHECK_EQ(io.drops, dropsBefore);
  CHECK_EQ(io.usb.size(), 1);
  CHECK_EQ(io.usb.back()[3], 99);
}

EPDJ_TEST(no_host_drops_at_once_without_stalling) {
  FakeIo io;
  io.hostActive = false;
  OutputPump<FakeIo> pump(io);
  for (unsigned i = 0; i < 50; ++i) io.queue.push_back(cc(static_cast<uint8_t>(i)));
  pump.drain(0);
  CHECK_EQ(io.trsWrites, 50);
  CHECK_EQ(io.drops, 50);
  CHECK_EQ(io.stalls, 0);
  CHECK(io.queue.empty());
}

// A burst the host is still draining: the event waits in hand, nothing is lost, and the sink is not written off.
EPDJ_TEST(a_fifo_that_frees_up_in_time_delivers_every_packet_in_order) {
  FakeIo io;
  OutputPump<FakeIo> pump(io);
  io.queue.push_back(sysex(14));  // 5 packets
  io.queue.push_back(cc(7));
  io.usbAccepts = 2;
  uint64_t t = 0;
  for (; t < 10 * kMs; t += kMs) pump.drain(t);  // refuses after 2 packets, 10 iterations
  CHECK_EQ(io.usb.size(), 2);
  CHECK_EQ(io.trsWrites, 1);  // the held event's TRS part went out once, the next event waits behind it
  CHECK_EQ(io.drops, 0);
  CHECK(io.stalls >= 9);
  io.usbAccepts = 1 << 30;
  pump.drain(t);
  CHECK_EQ(io.usb.size(), 5 + 1);
  CHECK_EQ(io.trsWrites, 2);
  CHECK_EQ(io.drops, 0);
  CHECK(!pump.usbSinkStalled());
  CHECK_EQ(io.usb[0][0] & 0x0F, 0x4);  // SysEx starts or continues
  CHECK_EQ(io.usb[4][0] & 0x0F, 0x6);  // 14 = 4 x 3 + 2: ends with a 2-byte packet
  CHECK_EQ(io.usb[5][3], 7);           // then the next event
}

EPDJ_TEST(an_event_left_in_hand_is_given_up_after_the_window_then_later_events_are_not_held) {
  FakeIo io;
  io.usbAccepts = 0;
  OutputPump<FakeIo> pump(io);
  io.queue.push_back(cc(1));
  io.queue.push_back(cc(2));
  io.queue.push_back(cc(3));
  pump.drain(0);
  CHECK_EQ(io.trsWrites, 1);  // 2 and 3 are behind the event in hand
  pump.drain(kUsbGiveUpUs);   // exactly the window: still waiting
  CHECK_EQ(io.trsWrites, 1);
  pump.drain(kUsbGiveUpUs + 1);  // over it: gives up on 1, and 2 and 3 pass straight through
  CHECK_EQ(io.trsWrites, 3);
  CHECK_EQ(io.drops, 3);
}
