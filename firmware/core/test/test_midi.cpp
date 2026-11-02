// MIDI messages, 14-bit/relative encodings, USB-MIDI packets and the streaming parser.
#include <cstring>
#include <vector>

#include "epdj/midi.h"
#include "epdj/protocol.h"
#include "test_harness.h"
#include "vectors.generated.h"

using namespace epdj;
using namespace epdj::midi;
using Bytes = std::vector<uint8_t>;

namespace {

Bytes bytesOf(const Message& m) { return Bytes(m.b, m.b + m.len); }

Message fromBytes(const Bytes& b) {
  Message m;
  m.len = static_cast<uint8_t>(b.size());
  for (size_t i = 0; i < b.size() && i < 3; ++i) m.b[i] = b[i];
  return m;
}

// xorshift32: deterministic, no dependency on <random> implementations.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return next() % n; }
};

// What the parser produced, flattened to bytes: messages as their bytes, SysEx as F0 ... F7.
struct Collected {
  bool sysex;
  Bytes bytes;
  bool operator==(const Collected& o) const { return sysex == o.sysex && bytes == o.bytes; }
};

struct Feeder {
  Bytes buf;
  Parser parser;
  std::vector<Collected> out;
  explicit Feeder(size_t cap) : buf(cap ? cap : 1), parser(buf.data(), cap) {}
  void feed(uint8_t b) {
    Parser::Event ev;
    if (!parser.feed(b, &ev)) return;
    if (ev.kind == Parser::Event::Kind::Message) {
      out.push_back({false, bytesOf(ev.message)});
    } else if (ev.kind == Parser::Event::Kind::SysEx) {
      out.push_back({true, Bytes(ev.sysex, ev.sysex + ev.sysexLen)});
    }
  }
  void feed(const Bytes& bs) {
    for (uint8_t b : bs) feed(b);
  }
};

}  // namespace

// ---- builders and encodings ----

EPDJ_TEST(builders_produce_exact_bytes) {
  CHECK_BYTES(bytesOf(noteOn(0, 36, 127)), (Bytes{0x90, 36, 127}));
  CHECK_BYTES(bytesOf(noteOff(0, 36)), (Bytes{0x80, 36, 0}));
  CHECK_BYTES(bytesOf(noteOn(1, 39, 127)), (Bytes{0x91, 39, 127}));
  CHECK_BYTES(bytesOf(controlChange(1, 22, 67)), (Bytes{0xB1, 22, 67}));
  CHECK_BYTES(bytesOf(programChange(0, 5)), (Bytes{0xC0, 5}));
  CHECK_BYTES(bytesOf(clock()), (Bytes{0xF8}));
  CHECK_BYTES(bytesOf(start()), (Bytes{0xFA}));
  CHECK_BYTES(bytesOf(cont()), (Bytes{0xFB}));
  CHECK_BYTES(bytesOf(stop()), (Bytes{0xFC}));
  CHECK_BYTES(bytesOf(songPosition(6)), (Bytes{0xF2, 6, 0}));
  CHECK_BYTES(bytesOf(songPosition(128)), (Bytes{0xF2, 0, 1}));
  CHECK_BYTES(bytesOf(songPosition(0x3FFF)), (Bytes{0xF2, 127, 127}));
  CHECK_BYTES(bytesOf(songPosition(0x4000)), (Bytes{0xF2, 0, 0}));  // wraps at 14 bits
  CHECK_BYTES(bytesOf(mtcQuarterFrame(118)), (Bytes{0xF1, 118}));
}

EPDJ_TEST(builders_mask_out_of_range_inputs) {
  CHECK_BYTES(bytesOf(noteOn(17, 200, 255)), (Bytes{0x91, 72, 127}));
  CHECK_BYTES(bytesOf(controlChange(255, 0xFF, 0x80)), (Bytes{0xBF, 127, 0}));
}

EPDJ_TEST(message_classification) {
  CHECK(noteOn(2, 60, 100).isNoteOn());
  CHECK(!noteOn(2, 60, 0).isNoteOn());
  CHECK(noteOn(2, 60, 0).isNoteOff());  // Note On vel 0 == Note Off
  CHECK(noteOff(2, 60).isNoteOff());
  CHECK_EQ(noteOn(2, 60, 1).channel(), 2);
  CHECK_EQ(noteOn(2, 60, 1).type(), 0x90);
  CHECK(controlChange(0, 1, 1).isControlChange());
  CHECK(clock().isRealTime());
  CHECK(!noteOn(0, 0, 0).isRealTime());
  CHECK_EQ(clock().type(), 0xF8);
  CHECK(Message().empty());
  CHECK(noteOn(0, 1, 2) == noteOn(0, 1, 2));
  CHECK(noteOn(0, 1, 2) != noteOn(0, 1, 3));
  CHECK(clock() != Message());
}

EPDJ_TEST(relative_matches_vectors) {
  for (const auto& v : epdj_vec::relVectors()) CHECK_EQ(encodeRelative(v.delta), v.value);
  CHECK_EQ(encodeRelative(2147483647), 127);
  CHECK_EQ(encodeRelative(-2147483647 - 1), 1);
  for (int d = -63; d <= 63; ++d) CHECK_EQ(decodeRelative(encodeRelative(d)), d);
  CHECK_EQ(decodeRelative(64), 0);
  CHECK_EQ(decodeRelative(0), -64);  // received literally, even though we never send it
  CHECK_EQ(decodeRelative(127), 63);
}

EPDJ_TEST(cc14_matches_vectors) {
  for (const auto& v : epdj_vec::cc14Vectors()) {
    CHECK_EQ(msb14(v.value14), v.msb);
    CHECK_EQ(lsb14(v.value14), v.lsb);
    CHECK_EQ(join14(v.msb, v.lsb), v.value14);
  }
  for (unsigned v = 0; v <= 16383; ++v) CHECK_EQ(join14(msb14(static_cast<uint16_t>(v)), lsb14(static_cast<uint16_t>(v))), v);
}

EPDJ_TEST(cc14_pair_order_and_skipping) {
  Message out[2];
  // Unknown previous value: both, MSB first.
  CHECK_EQ(cc14(1, proto::cc::kBpmMsb, proto::cc::kBpmLsb, 1285, -1, out), 2u);
  CHECK_BYTES(bytesOf(out[0]), (Bytes{0xB1, 20, 10}));
  CHECK_BYTES(bytesOf(out[1]), (Bytes{0xB1, 52, 5}));
  // Same value: nothing at all.
  CHECK_EQ(cc14(1, 20, 52, 1285, 1285, out), 0u);
  // Only the LSB moved.
  CHECK_EQ(cc14(0, 23, 55, 1286, 1285, out), 1u);
  CHECK_BYTES(bytesOf(out[0]), (Bytes{0xB0, 55, 6}));
  // Only the MSB moved (1285 = 10:5 -> 1413 = 11:5).
  CHECK_EQ(cc14(0, 23, 55, 1413, 1285, out), 1u);
  CHECK_BYTES(bytesOf(out[0]), (Bytes{0xB0, 23, 11}));
  // Both moved: MSB first.
  CHECK_EQ(cc14(0, 23, 55, 16383, 0, out), 2u);
  CHECK_EQ(out[0].data1(), 23);
  CHECK_EQ(out[1].data1(), 55);
  // Clamp, and an out-of-range prev is treated as unknown.
  CHECK_EQ(cc14(0, 23, 55, 60000, -1, out), 2u);
  CHECK_EQ(out[0].data2(), 127);
  CHECK_EQ(out[1].data2(), 127);
  CHECK_EQ(cc14(0, 23, 55, 5, 99999, out), 2u);
}

EPDJ_TEST(cc14_receiver_reassembles_either_order) {
  Cc14Receiver rx(20, 52);
  uint16_t v = 0;
  CHECK(!rx.onControlChange(21, 5, &v));
  CHECK(rx.onControlChange(20, 10, &v));
  CHECK_EQ(v, 10 << 7);
  CHECK(rx.onControlChange(52, 5, &v));
  CHECK_EQ(v, 1285);
  CHECK(rx.onControlChange(52, 6, &v));  // sender skipped the unchanged MSB
  CHECK_EQ(v, 1286);
  CHECK(rx.onControlChange(20, 0xFF, &v));  // high bit masked
  CHECK_EQ(v, (127 << 7) | 6);
}

// ---- USB-MIDI ----

EPDJ_TEST(usb_packets_match_vectors_both_ways) {
  for (const auto& v : epdj_vec::usbVectors()) {
    const bool isSysex = v.message[0] == 0xF0;
    std::vector<UsbPacket> got;
    if (isSysex) {
      UsbPacket buf[16];
      const size_t n = encodeSysex(v.cable, v.message.data(), v.message.size(), buf, 16);
      CHECK(n != 0);
      got.assign(buf, buf + n);
    } else {
      UsbPacket p;
      CHECK(encodePacket(v.cable, fromBytes(v.message), &p));
      got.push_back(p);
    }
    CHECK_EQ(got.size(), v.packets.size());
    for (size_t i = 0; i < got.size() && i < v.packets.size(); ++i) {
      uint8_t raw[4];
      got[i].toBytes(raw);
      CHECK_BYTES((Bytes(raw, raw + 4)), v.packets[i]);
      CHECK(UsbPacket::fromBytes(v.packets[i].data()) == got[i]);
    }

    // Decode: feed the vector's packets back through the parser.
    Feeder fd(64);
    for (const auto& pk : v.packets) {
      fd.parser.feed(UsbPacket::fromBytes(pk.data()), [&](const Parser::Event& ev) {
        if (ev.kind == Parser::Event::Kind::Message) fd.out.push_back({false, bytesOf(ev.message)});
        if (ev.kind == Parser::Event::Kind::SysEx) fd.out.push_back({true, Bytes(ev.sysex, ev.sysex + ev.sysexLen)});
      });
    }
    CHECK_EQ(fd.out.size(), 1u);
    if (fd.out.size() == 1) {
      CHECK(fd.out[0].sysex == isSysex);
      CHECK_BYTES(fd.out[0].bytes, v.message);
    }
  }
}

EPDJ_TEST(usb_cin_lengths) {
  const size_t want[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
  for (uint8_t cin = 0; cin < 16; ++cin) {
    UsbPacket p;
    p.cin = cin;
    CHECK_EQ(p.size(), want[cin]);
  }
}

EPDJ_TEST(usb_encode_rejects_what_has_no_packet) {
  UsbPacket p;
  CHECK(!encodePacket(0, Message(), &p));
  CHECK(!encodePacket(0, make(0xF0, 0, 0, 1), &p));
  CHECK(!encodePacket(0, make(0xF7, 0, 0, 1), &p));
  CHECK(!encodePacket(0, make(0xF4, 0, 0, 1), &p));
  CHECK(encodePacket(0, make(0xF6, 0, 0, 1), &p));  // tune request: CIN 5
  CHECK_EQ(p.cin, 5);
  CHECK(encodePacket(0x1F, noteOn(0, 1, 2), &p));  // cable masked to 4 bits
  CHECK_EQ(p.cable, 15);
}

EPDJ_TEST(usb_sysex_chunking_by_length) {
  // 2..10 byte messages: F0 + (len-2) data bytes + F7. Final CIN is 4 + bytes in the last packet.
  for (size_t len = 2; len <= 10; ++len) {
    Bytes m(len, 0x11);
    m.front() = 0xF0;
    m.back() = 0xF7;
    UsbPacket pk[8];
    const size_t n = encodeSysex(3, m.data(), m.size(), pk, 8);
    CHECK_EQ(n, (len + 2) / 3);
    const size_t lastBytes = len - 3 * (n - 1);
    CHECK_EQ(pk[n - 1].cin, 4 + lastBytes);
    for (size_t i = 0; i + 1 < n; ++i) CHECK_EQ(pk[i].cin, 4);
    CHECK_EQ(pk[0].cable, 3);
    // Reassemble.
    Bytes back;
    for (size_t i = 0; i < n; ++i) back.insert(back.end(), pk[i].data, pk[i].data + pk[i].size());
    CHECK_BYTES(back, m);
  }
}

EPDJ_TEST(usb_sysex_encode_validation) {
  UsbPacket pk[2];
  const uint8_t ok[] = {0xF0, 1, 2, 3, 0xF7};
  CHECK_EQ(encodeSysex(0, ok, sizeof(ok), pk, 2), 2u);
  CHECK_EQ(encodeSysex(0, ok, sizeof(ok), pk, 1), 0u);  // does not fit
  const uint8_t noStart[] = {1, 2, 0xF7};
  const uint8_t noEnd[] = {0xF0, 1, 2};
  const uint8_t highBit[] = {0xF0, 0x80, 0xF7};
  const uint8_t embedded[] = {0xF0, 1, 0xF7, 2, 0xF7};
  CHECK_EQ(encodeSysex(0, noStart, sizeof(noStart), pk, 2), 0u);
  CHECK_EQ(encodeSysex(0, noEnd, sizeof(noEnd), pk, 2), 0u);
  CHECK_EQ(encodeSysex(0, highBit, sizeof(highBit), pk, 2), 0u);
  CHECK_EQ(encodeSysex(0, embedded, sizeof(embedded), pk, 2), 0u);
  CHECK_EQ(encodeSysex(0, ok, 1, pk, 2), 0u);
}

// ---- Parser ----

EPDJ_TEST(parser_channel_messages_and_running_status) {
  Feeder f(16);
  f.feed(Bytes{0x90, 60, 100, 61, 101, 62, 0});  // one status, three notes
  f.feed(Bytes{0xC0, 7, 8});                      // program change, 1 data byte each
  f.feed(Bytes{0xB1, 22, 67, 22, 66});
  CHECK_EQ(f.out.size(), 7u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0x90, 60, 100}));
  CHECK_BYTES(f.out[1].bytes, (Bytes{0x90, 61, 101}));
  CHECK_BYTES(f.out[2].bytes, (Bytes{0x90, 62, 0}));
  CHECK_BYTES(f.out[3].bytes, (Bytes{0xC0, 7}));
  CHECK_BYTES(f.out[4].bytes, (Bytes{0xC0, 8}));
  CHECK_BYTES(f.out[5].bytes, (Bytes{0xB1, 22, 67}));
  CHECK_BYTES(f.out[6].bytes, (Bytes{0xB1, 22, 66}));
}

EPDJ_TEST(parser_running_status_survives_realtime_but_not_system_common) {
  Feeder f(16);
  f.feed(Bytes{0x90, 60, 100, 0xF8, 61, 0xF8, 101});  // clocks in the middle of a running-status message
  CHECK_EQ(f.out.size(), 4u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0x90, 60, 100}));
  CHECK_BYTES(f.out[1].bytes, (Bytes{0xF8}));
  CHECK_BYTES(f.out[2].bytes, (Bytes{0xF8}));
  CHECK_BYTES(f.out[3].bytes, (Bytes{0x90, 61, 101}));
  // A system common message ends running status: the data bytes after it belong to nothing.
  f.out.clear();
  f.feed(Bytes{0xF3, 5, 70, 71});
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF3, 5}));
  // ... and so does SysEx.
  f.out.clear();
  f.feed(Bytes{0x90, 60, 100, 0xF0, 1, 0xF7, 61, 101});
  CHECK_EQ(f.out.size(), 2u);
  CHECK(f.out[1].sysex);
}

EPDJ_TEST(parser_realtime_inside_message_is_delivered_first) {
  Feeder f(16);
  f.feed(Bytes{0xB0, 22, 0xF8, 67});
  CHECK_EQ(f.out.size(), 2u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF8}));
  CHECK_BYTES(f.out[1].bytes, (Bytes{0xB0, 22, 67}));
}

EPDJ_TEST(parser_system_common) {
  Feeder f(16);
  f.feed(Bytes{0xF1, 118, 0xF2, 6, 0, 0xF3, 9, 0xF6});
  CHECK_EQ(f.out.size(), 4u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF1, 118}));
  CHECK_BYTES(f.out[1].bytes, (Bytes{0xF2, 6, 0}));
  CHECK_BYTES(f.out[2].bytes, (Bytes{0xF3, 9}));
  CHECK_BYTES(f.out[3].bytes, (Bytes{0xF6}));
}

EPDJ_TEST(parser_ignores_undefined_and_stray_bytes) {
  Feeder f(16);
  f.feed(Bytes{1, 2, 3, 0xF4, 5, 0xF5, 6, 0xF9, 0xFD, 0xF7, 7});
  CHECK_EQ(f.out.size(), 0u);
  f.feed(Bytes{0xFE, 0xFF});  // active sensing, system reset: delivered, consumers ignore them
  CHECK_EQ(f.out.size(), 2u);
}

EPDJ_TEST(parser_truncated_message_is_replaced_by_next_status) {
  Feeder f(16);
  f.feed(Bytes{0x90, 60, 0x80, 60, 0});  // note-on lost its velocity
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0x80, 60, 0}));
}

EPDJ_TEST(parser_sysex_assembly) {
  Feeder f(16);
  const Bytes msg = {0xF0, 0x7D, 0x45, 0x44, 1, 0, 1, 0x7E, 0xF7};
  f.feed(msg);
  CHECK_EQ(f.out.size(), 1u);
  CHECK(f.out[0].sysex);
  CHECK_BYTES(f.out[0].bytes, msg);
  // Exactly fills the buffer.
  Feeder g(5);
  g.feed(Bytes{0xF0, 1, 2, 3, 0xF7});
  CHECK_EQ(g.out.size(), 1u);
  CHECK_EQ(g.parser.droppedSysex(), 0u);
  // Empty SysEx.
  g.feed(Bytes{0xF0, 0xF7});
  CHECK_EQ(g.out.size(), 2u);
  CHECK_BYTES(g.out[1].bytes, (Bytes{0xF0, 0xF7}));
}

EPDJ_TEST(parser_sysex_with_interleaved_realtime) {
  Feeder f(16);
  f.feed(Bytes{0xF0, 1, 0xF8, 2, 0xFA, 3, 0xF7});
  CHECK_EQ(f.out.size(), 3u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF8}));
  CHECK_BYTES(f.out[1].bytes, (Bytes{0xFA}));
  CHECK(f.out[2].sysex);
  CHECK_BYTES(f.out[2].bytes, (Bytes{0xF0, 1, 2, 3, 0xF7}));
}

EPDJ_TEST(parser_sysex_overflow_drops_whole_message_and_resyncs) {
  Feeder f(6);
  // 7-byte message cannot fit in 6: dropped, counted, nothing delivered.
  f.feed(Bytes{0xF0, 1, 2, 3, 4, 5, 0xF7});
  CHECK_EQ(f.out.size(), 0u);
  CHECK_EQ(f.parser.droppedSysex(), 1u);
  // A real-time byte during the drop is still delivered.
  f.feed(Bytes{0xF0, 1, 2, 3, 4, 5, 6, 7, 0xF8, 8, 9});
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF8}));
  CHECK_EQ(f.parser.droppedSysex(), 2u);
  // Resync on the next status byte: a channel message right after the overrun is parsed.
  f.out.clear();
  f.feed(Bytes{0x90, 60, 100});
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0x90, 60, 100}));
  // And so is a following well-formed SysEx.
  f.out.clear();
  f.feed(Bytes{0xF0, 1, 0xF7});
  CHECK_EQ(f.out.size(), 1u);
  CHECK(f.out[0].sysex);
  CHECK_EQ(f.parser.droppedSysex(), 2u);
}

EPDJ_TEST(parser_overflow_followed_directly_by_new_sysex) {
  Feeder f(4);
  f.feed(Bytes{0xF0, 1, 2, 3, 4, 5, 0xF0, 9, 0xF7});  // second F0 restarts without an F7
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF0, 9, 0xF7}));
  CHECK_EQ(f.parser.droppedSysex(), 1u);
}

EPDJ_TEST(parser_sysex_cut_short_by_status_is_dropped_silently) {
  Feeder f(16);
  f.feed(Bytes{0xF0, 1, 2, 0x90, 60, 100});
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0x90, 60, 100}));
  f.out.clear();
  f.feed(Bytes{0xF0, 1, 2, 0xF0, 3, 0xF7});  // F0 inside SysEx: restart
  CHECK_EQ(f.out.size(), 1u);
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF0, 3, 0xF7}));
  // A cut-short SysEx that then meets a stray F7 produces nothing.
  f.out.clear();
  f.feed(Bytes{0xF0, 1, 2, 0xF6, 0xF7});
  CHECK_EQ(f.out.size(), 1u);  // only the tune request
  CHECK_BYTES(f.out[0].bytes, (Bytes{0xF6}));
}

EPDJ_TEST(parser_tiny_or_missing_buffer_drops_every_sysex) {
  Parser none(nullptr, 0);
  Parser::Event ev;
  const Bytes msg = {0xF0, 1, 2, 0xF7, 0x90, 60, 100};
  int events = 0;
  for (uint8_t b : msg) {
    if (none.feed(b, &ev)) {
      CHECK(ev.kind == Parser::Event::Kind::Message);  // only the note survives
      ++events;
    }
  }
  CHECK_EQ(events, 1);
  CHECK_EQ(none.droppedSysex(), 1u);

  uint8_t one[1];
  Parser tiny(one, 1);
  for (uint8_t b : msg) tiny.feed(b, &ev);
  CHECK_EQ(tiny.droppedSysex(), 1u);
}

EPDJ_TEST(parser_reset_forgets_partial_message) {
  Feeder f(8);
  f.feed(Bytes{0x90, 60});
  f.parser.reset();
  f.feed(Bytes{100});
  CHECK_EQ(f.out.size(), 0u);
  f.feed(Bytes{0xF0, 1});
  f.parser.reset();
  f.feed(Bytes{2, 0xF7});
  CHECK_EQ(f.out.size(), 0u);
}

// Generates a random valid byte stream and the events it must produce, in stream order.
static void genStream(Rng& rng, size_t messages, size_t sysexCap, Bytes* stream, std::vector<Collected>* expect) {
  uint8_t running = 0;  // status the stream is currently "running" on (0 = none)
  auto maybeRealtime = [&]() {
    if (rng.below(6) != 0) return;
    static const uint8_t rt[] = {0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF};
    const uint8_t b = rt[rng.below(6)];
    stream->push_back(b);
    expect->push_back({false, Bytes{b}});
  };
  for (size_t i = 0; i < messages; ++i) {
    maybeRealtime();
    const uint32_t kind = rng.below(10);
    if (kind < 6) {  // channel voice, sometimes with running status
      static const uint8_t types[] = {0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0};
      const uint8_t status = static_cast<uint8_t>(types[rng.below(7)] | rng.below(16));
      const size_t n = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 1 : 2;
      Bytes msg = {status};
      for (size_t k = 0; k < n; ++k) msg.push_back(static_cast<uint8_t>(rng.below(128)));
      const bool useRunning = running == status && rng.below(2) == 0;
      if (!useRunning) {
        stream->push_back(status);
        maybeRealtime();
      }
      running = status;
      for (size_t k = 1; k < msg.size(); ++k) {
        stream->push_back(msg[k]);
        if (k + 1 < msg.size()) maybeRealtime();
      }
      expect->push_back({false, msg});
    } else if (kind < 8) {  // system common
      running = 0;
      const uint32_t which = rng.below(4);
      Bytes msg;
      if (which == 0) msg = {0xF1, static_cast<uint8_t>(rng.below(128))};
      else if (which == 1) msg = {0xF2, static_cast<uint8_t>(rng.below(128)), static_cast<uint8_t>(rng.below(128))};
      else if (which == 2) msg = {0xF3, static_cast<uint8_t>(rng.below(128))};
      else msg = {0xF6};
      for (size_t k = 0; k < msg.size(); ++k) {
        stream->push_back(msg[k]);
        if (k + 1 < msg.size()) maybeRealtime();
      }
      expect->push_back({false, msg});
    } else {  // SysEx; some too long for the buffer (those must vanish)
      running = 0;
      const size_t body = rng.below(static_cast<uint32_t>(sysexCap + 4));
      Bytes msg = {0xF0};
      for (size_t k = 0; k < body; ++k) msg.push_back(static_cast<uint8_t>(rng.below(128)));
      msg.push_back(0xF7);
      for (size_t k = 0; k < msg.size(); ++k) {
        stream->push_back(msg[k]);
        if (k + 1 < msg.size()) maybeRealtime();
      }
      if (msg.size() <= sysexCap) expect->push_back({true, msg});
    }
  }
}

EPDJ_TEST(parser_randomised_roundtrip_matches_model) {
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    Rng rng(seed * 2654435761u);
    const size_t cap = 4 + rng.below(30);
    Bytes stream;
    std::vector<Collected> expect;
    genStream(rng, 60, cap, &stream, &expect);
    Feeder f(cap);
    f.feed(stream);
    CHECK_EQ(f.out.size(), expect.size());
    const size_t n = f.out.size() < expect.size() ? f.out.size() : expect.size();
    for (size_t i = 0; i < n; ++i) {
      if (!(f.out[i] == expect[i])) {
        CHECK_BYTES(f.out[i].bytes, expect[i].bytes);
        break;
      }
    }
  }
}

// Every event the parser emits must be well formed, whatever it was fed.
static void checkEvent(const Parser::Event& ev, size_t cap) {
  if (ev.kind == Parser::Event::Kind::Message) {
    const Message& m = ev.message;
    CHECK(m.len >= 1 && m.len <= 3);
    CHECK(m.b[0] >= 0x80);
    for (uint8_t i = 1; i < m.len; ++i) CHECK(m.b[i] < 0x80);
    if (m.isChannelVoice()) {
      const uint8_t t = m.type();
      CHECK_EQ(m.len, (t == 0xC0 || t == 0xD0) ? 2 : 3);
    }
    CHECK(m.b[0] != 0xF0 && m.b[0] != 0xF7 && m.b[0] != 0xF4 && m.b[0] != 0xF5 && m.b[0] != 0xF9 && m.b[0] != 0xFD);
  } else if (ev.kind == Parser::Event::Kind::SysEx) {
    CHECK(ev.sysexLen >= 2 && ev.sysexLen <= cap);
    CHECK_EQ(ev.sysex[0], 0xF0);
    CHECK_EQ(ev.sysex[ev.sysexLen - 1], 0xF7);
    for (size_t i = 1; i + 1 < ev.sysexLen; ++i) CHECK(ev.sysex[i] < 0x80);
  }
}

EPDJ_TEST(parser_fuzz_random_bytes_200k) {
  Rng rng(0xC0FFEE);
  for (size_t cap : {size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{64}}) {
    Bytes buf(cap ? cap : 1);
    Parser p(cap ? buf.data() : nullptr, cap);
    for (int i = 0; i < 200000; ++i) {
      // Bias towards status bytes so SysEx and message boundaries are exercised constantly.
      const uint32_t r = rng.next();
      const uint8_t b = (r & 3) == 0 ? static_cast<uint8_t>(0x80 | ((r >> 8) & 0x7F)) : static_cast<uint8_t>(r >> 8);
      Parser::Event ev;
      if (p.feed(b, &ev)) checkEvent(ev, cap);
    }
  }
}

EPDJ_TEST(parser_fuzz_uniform_bytes_and_truncated_streams) {
  Rng rng(1234567);
  Bytes buf(32);
  Parser p(buf.data(), buf.size());
  for (int i = 0; i < 200000; ++i) {
    Parser::Event ev;
    if (p.feed(static_cast<uint8_t>(rng.next() >> 11), &ev)) checkEvent(ev, buf.size());
  }
  // Valid streams cut at every possible length, then continued by a clean message: no state leaks.
  Rng g(99);
  Bytes stream;
  std::vector<Collected> expect;
  genStream(g, 40, 32, &stream, &expect);
  for (size_t cut = 0; cut <= stream.size(); ++cut) {
    Feeder f(32);
    for (size_t i = 0; i < cut; ++i) f.feed(stream[i]);
    f.out.clear();
    f.feed(Bytes{0xF0, 0xF7});  // F0 aborts anything partial; a clean SysEx must always come out intact
    CHECK_EQ(f.out.size(), 1u);
    f.out.clear();
    f.feed(Bytes{0x90, 60, 100});
    CHECK_EQ(f.out.size(), 1u);
  }
}

EPDJ_TEST(parser_usb_packet_feed_obeys_cin_length) {
  Feeder f(16);
  UsbPacket p;
  p.cin = 0x9;
  p.data[0] = 0x90; p.data[1] = 60; p.data[2] = 100;
  int n = 0;
  f.parser.feed(p, [&](const Parser::Event& ev) { ++n; CHECK(ev.message == noteOn(0, 60, 100)); });
  CHECK_EQ(n, 1);
  // Reserved CINs carry no MIDI bytes even if the padding is non-zero.
  UsbPacket junk;
  junk.cin = 0x0;
  junk.data[0] = 0xF8; junk.data[1] = 0xF8; junk.data[2] = 0xF8;
  n = 0;
  f.parser.feed(junk, [&](const Parser::Event&) { ++n; });
  CHECK_EQ(n, 0);
  // A single-byte CIN 0xF packet ignores the padding bytes.
  UsbPacket clk;
  clk.cin = 0xF;
  clk.data[0] = 0xF8; clk.data[1] = 0x55; clk.data[2] = 0x55;
  n = 0;
  f.parser.feed(clk, [&](const Parser::Event& ev) { ++n; CHECK(ev.message == midi::clock()); });
  CHECK_EQ(n, 1);
}
