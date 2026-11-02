// The engine task's output side: pops the OutEvents the engine produced and hands each to TRS MIDI, the radio and
// USB. Header-only and free of ESP-IDF so tools/host_checks runs this exact code against fake sinks.
//
// USB is the only sink that can refuse (a full endpoint FIFO), and it must never hold the other two back:
//   * a refused packet keeps its event "in hand" and is retried every iteration for up to kUsbGiveUpUs, which rides
//     out a burst the host is still draining;
//   * once that runs out, or there is no host, the sink is declared stalled and every following event is dropped on
//     its first refused packet, instead of waiting out its own 100 ms. A host that simply does not read the port
//     (a MIDI IN nobody opened, an app that was closed) would otherwise cap the whole output at ~10 events/s;
//   * the first accepted packet clears the flag again, so recovery is automatic.
// An event's TRS and radio parts go out exactly once, when it is popped, whatever USB does with it.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/engine.h"

namespace epdj {
namespace app {

constexpr unsigned kMaxOutPerIteration = 64;
constexpr uint64_t kUsbGiveUpUs = 100000;
constexpr size_t kMaxUsbPackets = (OutEvent::kMaxBytes + 2) / 3;

// Io is the platform side. All calls are non-blocking.
//   bool popOut(OutEvent*)                        the engine's output queue
//   bool trsReady()                               false when TRS MIDI is compiled out or failed to start
//   void trsWrite(const uint8_t*, size_t)         drops when its ring is full, never blocks
//   bool radioReady()
//   void radioSend(const uint8_t*, size_t)        a refused send is packet loss, which the link layer tolerates
//   bool usbHostActive()                          a host is there and the bus is not suspended
//   bool usbWrite(const uint8_t packet[4])        false = FIFO full or no host
//   void countUsbStall(), countUsbDrop()
template <class Io>
class OutputPump {
 public:
  explicit OutputPump(Io& io) : io_(io) {}

  // Delivers up to kMaxOutPerIteration events. `nowUs` is the time of this iteration.
  void drain(uint64_t nowUs) {
    for (unsigned i = 0; i < kMaxOutPerIteration; ++i) {
      if (!hand_.valid) {
        if (!io_.popOut(&hand_.ev)) return;
        hand_.valid = true;
        hand_.otherDone = false;
        hand_.usbSent = 0;
        hand_.sinceUs = nowUs;
      }
      if (!deliver(nowUs)) return;
      hand_.valid = false;
    }
  }

  bool usbSinkStalled() const { return usbStalled_; }

 private:
  struct InHand {
    bool valid = false;
    OutEvent ev;
    bool otherDone = false;  // TRS and radio parts are sent exactly once
    size_t usbSent = 0;
    uint64_t sinceUs = 0;
  };

  // True = finished (or abandoned), false = USB is busy, try again next iteration.
  bool deliver(uint64_t nowUs) {
    OutEvent& ev = hand_.ev;
    if (!hand_.otherDone) {
      hand_.otherDone = true;
      if ((ev.portMask & proto::clockPorts::kTrs) != 0 && io_.trsReady()) io_.trsWrite(ev.data, ev.len);
      if (ev.kind == OutEvent::Kind::Link && (ev.portMask & proto::clockPorts::kLink) != 0 && io_.radioReady()) {
        io_.radioSend(ev.data, ev.len);
      }
    }
    if ((ev.portMask & proto::clockPorts::kUsb) == 0 ||
        (ev.kind != OutEvent::Kind::Message && ev.kind != OutEvent::Kind::SysEx)) {
      return true;
    }
    midi::UsbPacket pkts[kMaxUsbPackets];
    const size_t n = encodeUsb(ev, pkts, kMaxUsbPackets);
    while (hand_.usbSent < n) {
      uint8_t raw[4];
      pkts[hand_.usbSent].toBytes(raw);
      if (!io_.usbWrite(raw)) {
        if (!io_.usbHostActive() || usbStalled_ || nowUs - hand_.sinceUs > kUsbGiveUpUs) {
          usbStalled_ = true;
          io_.countUsbDrop();
          return true;  // no host, or it stopped reading: drop the rest of this event
        }
        io_.countUsbStall();
        return false;
      }
      usbStalled_ = false;
      ++hand_.usbSent;
    }
    return true;
  }

  Io& io_;
  InHand hand_;
  bool usbStalled_ = false;
};

}  // namespace app
}  // namespace epdj
