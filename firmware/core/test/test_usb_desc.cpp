// USB descriptor builder: lengths, wTotalLength, interface numbering, endpoints, MIDI jack/cable topology, IAD
// placement, UAC1 entity graph, string table against the contract's identity; with and without the audio function.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "epdj/protocol.h"
#include "epdj/usb_desc.h"
#include "test_harness.h"

using namespace epdj::usb;
namespace proto = epdj::proto;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes config(bool audio) {
  Bytes b(kMaxConfigDescriptorBytes);
  const size_t n = buildConfigDescriptor(audio, b.data(), b.size());
  b.resize(n);
  return b;
}

// One descriptor inside a configuration descriptor.
struct Desc {
  size_t off;
  const uint8_t* p;
  uint8_t len() const { return p[0]; }
  uint8_t type() const { return p[1]; }
  uint8_t sub() const { return p[2]; }  // bDescriptorSubtype for 0x24/0x25
};

std::vector<Desc> walk(const Bytes& cfg) {
  std::vector<Desc> out;
  size_t off = 0;
  while (off < cfg.size()) {
    const uint8_t len = cfg[off];
    if (len < 2 || off + len > cfg.size()) {
      out.clear();  // malformed: the caller's size checks will fail loudly
      return out;
    }
    out.push_back({off, &cfg[off]});
    off += len;
  }
  return out;
}

constexpr uint8_t kItf = 0x04, kEp = 0x05, kIad = 0x0B, kCsItf = 0x24, kCsEp = 0x25;

struct Itf {
  uint8_t num, alt, numEps, cls, sub, proto, str;
};
Itf itf(const Desc& d) { return {d.p[2], d.p[3], d.p[4], d.p[5], d.p[6], d.p[7], d.p[8]}; }

std::vector<Desc> ofType(const std::vector<Desc>& all, uint8_t type) {
  std::vector<Desc> v;
  for (const Desc& d : all)
    if (d.type() == type) v.push_back(d);
  return v;
}

unsigned u16(const uint8_t* p) { return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8); }
unsigned u24(const uint8_t* p) { return u16(p) | (static_cast<unsigned>(p[2]) << 16); }

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// protocol/epdj3000.json, found relative to this source file (…/firmware/core/test/ -> repo root).
std::string protocolJson() {
  std::string here = __FILE__;
  const size_t cut = here.rfind("/firmware/core/test/");
  if (cut == std::string::npos) return std::string();
  return readFile(here.substr(0, cut) + "/protocol/epdj3000.json");
}

unsigned jsonHex(const std::string& json, const char* key) {
  const size_t k = json.find(std::string("\"") + key + "\"");
  if (k == std::string::npos) return 0xFFFFFFFFu;
  const size_t q = json.find("\"0x", k);
  if (q == std::string::npos) return 0xFFFFFFFFu;
  return static_cast<unsigned>(std::strtoul(json.c_str() + q + 1, nullptr, 16));
}

const uint8_t kMac[6] = {0x24, 0x0A, 0xC4, 0x00, 0xBE, 0xEF};

}  // namespace

EPDJ_TEST(generated_identity_matches_the_contract_file) {
  // protocol.h is generated from the JSON (tools/gen_protocol.py --check guards the files); this guards the
  // values this builder actually reads.
  const std::string json = protocolJson();
  CHECK(!json.empty());
  CHECK_EQ(jsonHex(json, "usbVendorId"), proto::kUsbVendorId);
  CHECK_EQ(jsonHex(json, "usbProductId"), proto::kUsbProductId);
  CHECK(json.find(std::string("\"name\": \"") + proto::kProductName + "\"") != std::string::npos);
  for (const char* deck : proto::kDeckNames) CHECK(json.find(std::string("\"") + deck + "\"") != std::string::npos);
  for (const char* cable : proto::kCableNames)
    CHECK(json.find(std::string("\"name\": \"") + cable + "\"") != std::string::npos);
}

EPDJ_TEST(device_descriptor_fields) {
  for (bool audio : {false, true}) {
    uint8_t d[kDeviceDescriptorBytes];
    CHECK_EQ(buildDeviceDescriptor(audio, d, sizeof d), kDeviceDescriptorBytes);
    CHECK_EQ(d[0], 18);
    CHECK_EQ(d[1], 1);
    CHECK_EQ(u16(d + 2), 0x0200);
    CHECK_EQ(d[4], 0xEF);  // IAD device class triple
    CHECK_EQ(d[5], 0x02);
    CHECK_EQ(d[6], 0x01);
    CHECK_EQ(d[7], 64);
    CHECK_EQ(u16(d + 8), proto::kUsbVendorId);
    CHECK_EQ(u16(d + 10), proto::kUsbProductId);
    CHECK_EQ(u16(d + 12), audio ? 0x0101 : 0x0100);
    CHECK_EQ(d[14], kStrManufacturer);
    CHECK_EQ(d[15], kStrProduct);
    CHECK_EQ(d[16], kStrSerial);
    CHECK_EQ(d[17], 1);
  }
  uint8_t small[17];
  CHECK_EQ(buildDeviceDescriptor(true, small, sizeof small), 0);
}

EPDJ_TEST(config_length_and_total_length_agree) {
  for (bool audio : {false, true}) {
    const Bytes c = config(audio);
    CHECK_EQ(c.size(), configDescriptorBytes(audio));
    CHECK(c.size() <= kMaxConfigDescriptorBytes);
    CHECK_EQ(u16(&c[2]), c.size());
    CHECK_EQ(c[0], 9);
    CHECK_EQ(c[1], 2);
    CHECK_EQ(c[5], 1);     // bConfigurationValue
    CHECK_EQ(c[7], 0x80);  // bus powered, no remote wakeup
    CHECK_EQ(c[8], kMaxPowerMa / 2);
    // Walking by bLength lands exactly on the end: no descriptor straddles wTotalLength.
    const std::vector<Desc> all = walk(c);
    CHECK(!all.empty());
    size_t sum = 0;
    for (const Desc& d : all) sum += d.len();
    CHECK_EQ(sum, c.size());
  }
  // The sizes the layout promises (TinyUSB's crosscheck compares the bytes).
  CHECK_EQ(configDescriptorBytes(false), 173);
  CHECK_EQ(configDescriptorBytes(true), 294);
}

EPDJ_TEST(interfaces_are_numbered_contiguously_and_counted) {
  for (bool audio : {false, true}) {
    const Bytes c = config(audio);
    const std::vector<Desc> all = walk(c);
    CHECK_EQ(c[4], audio ? 4 : 2);  // bNumInterfaces
    unsigned expected = 0;
    for (const Desc& d : ofType(all, kItf)) {
      const Itf i = itf(d);
      if (i.alt == 0) {
        CHECK_EQ(i.num, expected);
        ++expected;
      } else {
        CHECK_EQ(i.num, expected - 1);  // an alternate setting follows its interface
      }
    }
    CHECK_EQ(expected, c[4]);
  }
}

EPDJ_TEST(iads_precede_and_cover_exactly_their_function) {
  for (bool audio : {false, true}) {
    const Bytes c = config(audio);
    const std::vector<Desc> all = walk(c);
    const std::vector<Desc> iads = ofType(all, kIad);
    CHECK_EQ(iads.size(), audio ? 2 : 1);
    for (const Desc& a : iads) {
      CHECK_EQ(a.len(), 8);
      CHECK_EQ(a.p[4], 1);  // audio function class
      CHECK_EQ(a.p[5], 0);  // no function subclass
      CHECK_EQ(a.p[6], 0);  // audio 1.0 / MIDI: protocol 0 (a 0x20 here would announce audio 2.0)
      const unsigned first = a.p[2], count = a.p[3];
      // The descriptor right after the IAD is the first interface, and exactly `count` interfaces belong to it
      // before the next IAD or the end.
      size_t idx = 0;
      while (all[idx].off != a.off) ++idx;
      CHECK_EQ(all[idx + 1].type(), kItf);
      CHECK_EQ(itf(all[idx + 1]).num, first);
      unsigned seen = 0;
      unsigned last = first;
      for (size_t j = idx + 1; j < all.size() && all[j].type() != kIad; ++j) {
        if (all[j].type() == kItf && itf(all[j]).alt == 0) {
          ++seen;
          last = itf(all[j]).num;
        }
      }
      CHECK_EQ(seen, count);
      CHECK_EQ(last, first + count - 1);
    }
    // The first IAD is the MIDI one and it directly follows the configuration descriptor.
    CHECK_EQ(iads[0].off, 9);
    CHECK_EQ(iads[0].p[2], kItfMidiControl);
    CHECK_EQ(iads[0].p[3], 2);
    if (audio) {
      CHECK_EQ(iads[1].p[2], kItfAudioControl);
      CHECK_EQ(iads[1].p[3], 2);
    }
  }
}

EPDJ_TEST(every_interface_in_the_audio_function_is_audio_1_0) {
  // TinyUSB's audio driver tells UAC1 from UAC2 by the protocol code of the AudioControl interface; a stray
  // 0x20 on any interface of the function would make hosts expect audio 2.0 entities.
  const Bytes c = config(true);
  const std::vector<Desc> all = walk(c);
  for (const Desc& d : ofType(all, kItf)) {
    CHECK_EQ(itf(d).cls, 1);
    CHECK_EQ(itf(d).proto, 0);
  }
}

EPDJ_TEST(endpoints_addresses_sizes_and_uniqueness) {
  for (bool audio : {false, true}) {
    const Bytes c = config(audio);
    const std::vector<Desc> all = walk(c);
    std::vector<uint8_t> addrs;
    for (const Desc& d : ofType(all, kEp)) {
      const uint8_t addr = d.p[2];
      for (uint8_t a : addrs) CHECK(a != addr);
      addrs.push_back(addr);
      CHECK_EQ(d.len(), 9);  // audio 1.0 style endpoint descriptors: bRefresh + bSynchAddress
      if (addr == kEpMidiOut || addr == kEpMidiIn) {
        CHECK_EQ(d.p[3], 0x02);
        CHECK_EQ(u16(d.p + 4), 64);
      } else if (addr == kEpAudioOut) {
        CHECK_EQ(d.p[3], 0x05);  // isochronous, asynchronous, data
        CHECK_EQ(u16(d.p + 4), 196);
        CHECK_EQ(u16(d.p + 4), kAudioPacketBytes);
        CHECK_EQ(d.p[6], 1);          // every frame
        CHECK_EQ(d.p[7], 0);          // bRefresh
        CHECK_EQ(d.p[8], kEpAudioFeedback);  // bSynchAddress: the feedback endpoint below
      } else if (addr == kEpAudioFeedback) {
        CHECK_EQ(d.p[3], 0x01);  // isochronous, no sync: TinyUSB's UAC1 driver finds the feedback endpoint by this
        CHECK_EQ(u16(d.p + 4), 3);  // 10.14 in three bytes
        CHECK_EQ(u16(d.p + 4), kFeedbackPacketBytes);
        CHECK_EQ(d.p[6], 1);
        CHECK_EQ(d.p[7], 0);  // bRefresh 0 on purpose, see usb_desc.cpp
        CHECK_EQ(d.p[8], 0);
      } else {
        CHECK(false);  // no other endpoint may exist
      }
    }
    CHECK_EQ(addrs.size(), audio ? 4 : 2);
    // The ESP32-S3 DWC2 has 5 IN endpoints besides EP0: each IN address is used once.
    // Each interface descriptor announces exactly the endpoints that follow it.
    for (size_t i = 0; i < all.size(); ++i) {
      if (all[i].type() != kItf) continue;
      unsigned n = 0;
      for (size_t j = i + 1; j < all.size() && all[j].type() != kItf && all[j].type() != kIad; ++j)
        if (all[j].type() == kEp) ++n;
      CHECK_EQ(itf(all[i]).numEps, n);
    }
  }
}

EPDJ_TEST(midi_streaming_jack_and_cable_topology) {
  const Bytes c = config(false);
  const std::vector<Desc> all = walk(c);

  struct Jack {
    uint8_t kind, id, str, srcId;
    bool isOut;
  };
  std::vector<Jack> jacks;
  const Desc* header = nullptr;
  const Desc* epCs[2] = {nullptr, nullptr};
  unsigned epCsSeen = 0;
  size_t msStart = 0, msEnd = 0;
  for (size_t i = 0; i < all.size(); ++i) {
    const Desc& d = all[i];
    if (d.type() == kCsItf && d.sub() == 0x01 && d.len() == 7) {
      header = &d;
      msStart = d.off;
    } else if (d.type() == kCsItf && d.sub() == 0x02) {
      CHECK_EQ(d.len(), 6);
      jacks.push_back({d.p[3], d.p[4], d.p[5], 0, false});
    } else if (d.type() == kCsItf && d.sub() == 0x03) {
      CHECK_EQ(d.len(), 9);
      CHECK_EQ(d.p[5], 1);  // one input pin
      CHECK_EQ(d.p[7], 1);  // fed from its pin 1
      jacks.push_back({d.p[3], d.p[4], d.p[8], d.p[6], true});
    } else if (d.type() == kCsEp) {
      CHECK(epCsSeen < 2);
      epCs[epCsSeen++] = &d;
      msEnd = d.off + d.len();
    }
  }
  CHECK(header != nullptr);
  CHECK_EQ(epCsSeen, 2);
  CHECK_EQ(jacks.size(), 4u * kMidiCables);
  // MS header wTotalLength covers the header, every jack and both endpoint pairs.
  CHECK_EQ(u16(header->p + 5), msEnd - msStart);

  // Jack ids are unique and every jack carries its cable's name.
  for (size_t i = 0; i < jacks.size(); ++i) {
    for (size_t j = i + 1; j < jacks.size(); ++j) CHECK(jacks[i].id != jacks[j].id);
    CHECK_EQ(jacks[i].str, kStrCable0 + i / 4);
  }
  // Per cable: embedded OUT <- external IN, external OUT <- embedded IN.
  for (unsigned cable = 0; cable < kMidiCables; ++cable) {
    const Jack& inEmb = jacks[cable * 4 + 0];
    const Jack& inExt = jacks[cable * 4 + 1];
    const Jack& outEmb = jacks[cable * 4 + 2];
    const Jack& outExt = jacks[cable * 4 + 3];
    CHECK_EQ(inEmb.kind, 0x01); CHECK(!inEmb.isOut);
    CHECK_EQ(inExt.kind, 0x02); CHECK(!inExt.isOut);
    CHECK_EQ(outEmb.kind, 0x01); CHECK(outEmb.isOut);
    CHECK_EQ(outExt.kind, 0x02); CHECK(outExt.isOut);
    CHECK_EQ(outEmb.srcId, inExt.id);
    CHECK_EQ(outExt.srcId, inEmb.id);
  }
  // Bulk endpoints: the n-th associated jack is USB-MIDI cable n. OUT <- embedded IN jacks, IN -> embedded OUT jacks.
  for (unsigned e = 0; e < 2; ++e) {
    const Desc& cs = *epCs[e];
    CHECK_EQ(cs.sub(), 0x01);
    CHECK_EQ(cs.p[3], kMidiCables);
    CHECK_EQ(cs.len(), 4 + kMidiCables);
    for (unsigned cable = 0; cable < kMidiCables; ++cable) {
      const Jack& want = jacks[cable * 4 + (e == 0 ? 0 : 2)];
      CHECK_EQ(cs.p[4 + cable], want.id);
    }
  }
  // The first endpoint descriptor pair is the OUT one.
  for (size_t i = 0; i + 1 < all.size(); ++i) {
    if (all[i].type() == kEp && all[i + 1].type() == kCsEp)
      CHECK_EQ(all[i + 1].off, epCs[all[i].p[2] == kEpMidiOut ? 0 : 1]->off);
  }
  // String index range
  for (const Jack& j : jacks) CHECK(j.str < kStringCount);
}

EPDJ_TEST(uac1_entity_graph) {
  const Bytes c = config(true);
  const std::vector<Desc> all = walk(c);
  // Find the audio function: everything after the second IAD.
  size_t start = 0;
  for (size_t i = 0; i < all.size(); ++i)
    if (all[i].type() == kIad && all[i].p[2] == kItfAudioControl) start = i;
  CHECK(start != 0);

  const Desc* header = nullptr;
  const Desc *it = nullptr, *ot = nullptr, *fu = nullptr;
  size_t acEnd = 0;
  unsigned nEntities = 0;
  for (size_t i = start; i < all.size(); ++i) {
    const Desc& d = all[i];
    if (d.type() == kItf && itf(d).num == kItfAudioStreaming) break;  // AudioControl ends here
    if (d.type() != kCsItf) continue;
    ++nEntities;
    if (d.sub() == 0x01) header = &d;
    if (d.sub() == 0x02) it = &d;
    if (d.sub() == 0x03) ot = &d;
    if (d.sub() == 0x06) fu = &d;
    acEnd = d.off + d.len();
  }
  CHECK(header && it && ot && fu);
  CHECK_EQ(nEntities, 4u);  // header, IT, OT, FU: audio 1.0 has no clock source entity

  // Class-specific AC header: bcdADC 1.00, wTotalLength spans header..last unit, one streaming interface.
  CHECK_EQ(header->len(), 9);
  CHECK_EQ(u16(header->p + 3), 0x0100);
  CHECK_EQ(u16(header->p + 5), acEnd - header->off);
  CHECK_EQ(header->p[7], 1);
  CHECK_EQ(header->p[8], kItfAudioStreaming);

  // Entity ids are what the control request handlers address.
  CHECK_EQ(it->p[3], kEntityInputTerminal);
  CHECK_EQ(ot->p[3], kEntityOutputTerminal);
  CHECK_EQ(fu->p[3], kEntityFeatureUnit);
  CHECK(kEntityInputTerminal != kEntityFeatureUnit && kEntityFeatureUnit != kEntityOutputTerminal &&
        kEntityInputTerminal != kEntityOutputTerminal);
  CHECK(kEntityInputTerminal != 0 && kEntityFeatureUnit != 0 && kEntityOutputTerminal != 0);

  // Input terminal: USB streaming, 2 channels, front left + right, no associated terminal.
  CHECK_EQ(it->len(), 12);
  CHECK_EQ(u16(it->p + 4), 0x0101);
  CHECK_EQ(it->p[6], 0);
  CHECK_EQ(it->p[7], kChannels);
  CHECK_EQ(u16(it->p + 8), 0x0003);
  // Output terminal: line out, fed by the feature unit.
  CHECK_EQ(ot->len(), 9);
  CHECK_EQ(u16(ot->p + 4), kTerminalLineConnector);
  CHECK_EQ(ot->p[7], kEntityFeatureUnit);
  // Feature unit: input terminal -> unit, 2-byte bitmaps, mute + volume on the master and both channels.
  CHECK_EQ(fu->len(), 7 + (kChannels + 1) * 2);
  CHECK_EQ(fu->p[4], kEntityInputTerminal);
  CHECK_EQ(fu->p[5], 2);
  for (unsigned ch = 0; ch <= kChannels; ++ch) CHECK_EQ(u16(fu->p + 6 + 2 * ch), 0x0003);

  // Streaming interface: alt 0 idle, alt 1 with the two endpoints; AS general links to the input terminal.
  bool sawAlt0 = false, sawAlt1 = false;
  const Desc *asGeneral = nullptr, *fmt = nullptr, *csEp = nullptr;
  for (size_t i = start; i < all.size(); ++i) {
    const Desc& d = all[i];
    if (d.type() == kItf && itf(d).num == kItfAudioStreaming) {
      const Itf x = itf(d);
      CHECK_EQ(x.cls, 1);
      CHECK_EQ(x.sub, 2);
      CHECK_EQ(x.proto, 0);
      if (x.alt == 0) { sawAlt0 = true; CHECK_EQ(x.numEps, 0); }
      if (x.alt == 1) { sawAlt1 = true; CHECK_EQ(x.numEps, 2); }
    }
    if (d.type() == kCsItf && d.sub() == 0x01 && d.len() == 7) asGeneral = &d;
    if (d.type() == kCsItf && d.sub() == 0x02 && d.len() == 11) fmt = &d;
    if (d.type() == kCsEp) csEp = &d;
  }
  CHECK(sawAlt0 && sawAlt1);
  CHECK(asGeneral && fmt && csEp);
  CHECK_EQ(asGeneral->p[3], kEntityInputTerminal);
  CHECK_EQ(u16(asGeneral->p + 5), 1);  // PCM
  CHECK_EQ(fmt->p[3], 1);              // format type I
  CHECK_EQ(fmt->p[4], kChannels);
  CHECK_EQ(fmt->p[5], kSubslotBytes);
  CHECK_EQ(fmt->p[6], kBitResolution);
  CHECK_EQ(fmt->p[7], 1);              // exactly one discrete rate
  CHECK_EQ(u24(fmt->p + 8), 48000);
  CHECK_EQ(u24(fmt->p + 8), kSampleRateHz);
  // The data endpoint advertises a sampling-frequency control, so the firmware must answer it (usb_audio.cpp).
  CHECK_EQ(csEp->len(), 7);
  CHECK_EQ(csEp->sub(), 0x01);
  CHECK_EQ(csEp->p[3], 0x01);
  // The audio packet fits one USB frame at +1 sample.
  CHECK_EQ(kAudioPacketBytes, (48 + 1) * 2 * 2);
}

EPDJ_TEST(audio_function_adds_exactly_the_audio_bytes) {
  const Bytes a = config(false), b = config(true);
  CHECK_EQ(b.size() - a.size(), 121);
  // MIDI part is byte-identical apart from the header's interface count and wTotalLength.
  for (size_t i = 9; i < a.size(); ++i) CHECK_EQ(a[i], b[i]);
  CHECK_EQ(a[4], 2);
  CHECK_EQ(b[4], 4);
}

EPDJ_TEST(builders_never_write_past_the_buffer) {
  for (bool audio : {false, true}) {
    const size_t n = configDescriptorBytes(audio);
    Bytes buf(n + 16, 0xA5);
    CHECK_EQ(buildConfigDescriptor(audio, buf.data(), n), n);
    for (size_t i = n; i < buf.size(); ++i) CHECK_EQ(buf[i], 0xA5);
    Bytes small(n - 1, 0x5A);
    CHECK_EQ(buildConfigDescriptor(audio, small.data(), small.size()), 0);
    for (uint8_t v : small) CHECK_EQ(v, 0x5A);
  }
  CHECK_EQ(buildConfigDescriptor(true, nullptr, 0), 0);
}

EPDJ_TEST(string_helpers) {
  char s[32];
  CHECK_EQ(formatProductName(0, s, sizeof s), 16);
  CHECK(std::strcmp(s, "EP-DJ3000 Deck A") == 0);
  CHECK_EQ(formatProductName(1, s, sizeof s), 16);
  CHECK(std::strcmp(s, "EP-DJ3000 Deck B") == 0);
  CHECK_EQ(formatProductName(200, s, sizeof s), 16);
  CHECK(std::strcmp(s, "EP-DJ3000 Deck B") == 0);
  CHECK_EQ(formatProductName(0, s, 16), 0);  // no room for the NUL
  CHECK_EQ(formatProductName(0, s, 17), 16);

  CHECK_EQ(formatSerial(kMac, s, sizeof s), 12);
  CHECK(std::strcmp(s, "240AC400BEEF") == 0);
  CHECK_EQ(formatSerial(kMac, s, 12), 0);
}

EPDJ_TEST(string_table_carries_the_contracts_identity) {
  for (uint8_t deck = 0; deck < 2; ++deck) {
    Strings s;
    CHECK(buildStrings(deck, kMac, &s));
    for (const char* str : s.table) CHECK(str != nullptr);

    // string 0 is the LANGID, two raw bytes (English, US)
    CHECK_EQ(static_cast<uint8_t>(s.table[kStrLangId][0]), 0x09);
    CHECK_EQ(static_cast<uint8_t>(s.table[kStrLangId][1]), 0x04);
    CHECK(std::string(s.table[kStrManufacturer]) == proto::kProductName);
    CHECK(std::string(s.table[kStrProduct]) == std::string(proto::kProductName) + " " + proto::kDeckNames[deck]);
    CHECK(std::string(s.table[kStrSerial]) == "240AC400BEEF");
    for (unsigned c = 0; c < kMidiCables; ++c) CHECK(std::string(s.table[kStrCable0 + c]) == proto::kCableNames[c]);
    CHECK(std::string(s.table[kStrAudio]) == std::string(proto::kProductName) + " Audio");

    // esp_tinyusb serves at most 31 characters of a string.
    for (unsigned i = kStrManufacturer; i < kStringCount; ++i) CHECK(std::strlen(s.table[i]) <= kMaxStringChars);
    // Strings that depend on the unit live in the struct (so they outlive the call), the rest is static text.
    CHECK(s.table[kStrProduct] == s.product);
    CHECK(s.table[kStrSerial] == s.serial);
    CHECK(s.table[kStrAudio] == s.audio);
  }
  // Two units never share a serial number.
  const uint8_t other[6] = {0x24, 0x0A, 0xC4, 0x00, 0xBE, 0xF0};
  Strings a, b;
  CHECK(buildStrings(0, kMac, &a));
  CHECK(buildStrings(0, other, &b));
  CHECK(std::strcmp(a.table[kStrSerial], b.table[kStrSerial]) != 0);

  CHECK(!buildStrings(0, kMac, nullptr));
  Strings s;
  CHECK(!buildStrings(0, nullptr, &s));
  CHECK_EQ(kStrCable0 + kMidiCables, kStrAudio);
  CHECK_EQ(kStrAudio + 1, kStringCount);
}

EPDJ_TEST(every_string_index_the_descriptors_use_is_in_the_table) {
  Strings s;
  CHECK(buildStrings(0, kMac, &s));
  for (bool audio : {false, true}) {
    std::vector<uint8_t> used;
    uint8_t dev[kDeviceDescriptorBytes];
    CHECK_EQ(buildDeviceDescriptor(audio, dev, sizeof dev), kDeviceDescriptorBytes);
    used.push_back(dev[14]);
    used.push_back(dev[15]);
    used.push_back(dev[16]);
    const Bytes c = config(audio);
    const std::vector<Desc> all = walk(c);
    for (const Desc& d : all) {
      if (d.type() == kItf) used.push_back(d.p[8]);
      if (d.type() == kCsItf && d.sub() == 0x02 && d.len() == 6) used.push_back(d.p[5]);   // MIDI IN jack
      if (d.type() == kCsItf && d.sub() == 0x03 && d.len() == 9) used.push_back(d.p[8]);   // MIDI OUT jack
    }
    unsigned nonZero = 0;
    for (uint8_t idx : used) {
      if (idx == 0) continue;  // "no string"
      ++nonZero;
      CHECK(idx < kStringCount);
      CHECK(s.table[idx] != nullptr && s.table[idx][0] != '\0');
    }
    CHECK(nonZero > 0);
    // The audio function's name is used exactly when the function exists.
    bool usesAudioName = false;
    for (uint8_t idx : used) usesAudioName = usesAudioName || idx == kStrAudio;
    CHECK_EQ(usesAudioName, audio);
  }
}
