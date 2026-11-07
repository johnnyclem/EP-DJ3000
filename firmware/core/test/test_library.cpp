#include "epdj/library.h"
#include "test_harness.h"

#include <cstring>
#include <vector>

using namespace epdj;
using namespace epdj::library;

namespace {

void put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v & 0xFF));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

std::vector<uint8_t> wav(uint16_t ch, uint32_t rate, const std::vector<int16_t>& samples) {
  const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2u);
  std::vector<uint8_t> b;
  b.insert(b.end(), {'R', 'I', 'F', 'F'});
  put32(b, 36 + dataBytes);
  b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put32(b, 16);
  put16(b, 1);
  put16(b, ch);
  put32(b, rate);
  put32(b, rate * ch * 2);
  put16(b, static_cast<uint16_t>(ch * 2));
  put16(b, 16);
  b.insert(b.end(), {'d', 'a', 't', 'a'});
  put32(b, dataBytes);
  for (int16_t s : samples) put16(b, static_cast<uint16_t>(s));
  return b;
}

struct Mem {
  const uint8_t* p = nullptr;
  size_t n = 0;
  size_t at = 0;
};

size_t memRead(void* ctx, uint8_t* dst, size_t n) {
  auto* m = static_cast<Mem*>(ctx);
  if (m->at >= m->n) return 0;
  const size_t have = m->n - m->at;
  if (n > have) n = have;
  std::memcpy(dst, m->p + m->at, n);
  m->at += n;
  return n;
}
bool memSeek(void* ctx, uint32_t off) {
  auto* m = static_cast<Mem*>(ctx);
  if (off > m->n) return false;
  m->at = off;
  return true;
}

}  // namespace

EPDJ_TEST(wav_header_round_trip) {
  const auto b = wav(2, 48000, {1, 2, 3, 4});
  WavInfo info;
  CHECK(parseWav(b.data(), b.size(), info) == WavError::Ok);
  CHECK_EQ(info.channels, 2);
  CHECK_EQ(info.sampleRate, 48000);
  CHECK_EQ(info.dataBytes, 8);
  CHECK_EQ(info.dataOffset, 44);
}

EPDJ_TEST(wav_rejects_24_bit_and_junk) {
  auto b = wav(1, 48000, {0});
  b[34] = 24;  // bits per sample
  WavInfo info;
  CHECK(parseWav(b.data(), b.size(), info) == WavError::NotPcm16);
  const uint8_t junk[] = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(parseWav(junk, sizeof junk, info) == WavError::NotRiff);
}

EPDJ_TEST(catalog_wraps_and_refuses_a_long_name) {
  Catalog cat;
  CHECK(cat.add("a.wav"));
  CHECK(cat.add("b.wav"));
  CHECK_EQ(cat.count(), 2);
  char big[kNameLen + 2];
  std::memset(big, 'x', sizeof big);
  big[sizeof big - 1] = '\0';
  CHECK(!cat.add(big));
  Browser br;
  br.bind(&cat);
  CHECK_EQ(std::strcmp(br.current(), "a.wav"), 0);
  br.next();
  CHECK_EQ(std::strcmp(br.current(), "b.wav"), 0);
  br.next();
  CHECK_EQ(std::strcmp(br.current(), "a.wav"), 0);
  br.prev();
  CHECK_EQ(std::strcmp(br.current(), "b.wav"), 0);
}

EPDJ_TEST(player_copies_48k_stereo) {
  const auto b = wav(2, 48000, {1000, -1000, 2000, -2000, 3000, -3000});
  Mem m{b.data(), b.size(), 0};
  Player p;
  CHECK(p.load(&memRead, &memSeek, &m) == WavError::Ok);
  p.play();
  int16_t out[8] = {};
  CHECK_EQ(p.pull(out, 4), 3);
  CHECK_EQ(out[0], 1000);
  CHECK_EQ(out[1], -1000);
  CHECK_EQ(out[2], 2000);
  CHECK_EQ(out[3], -2000);
  CHECK_EQ(out[4], 3000);
  CHECK_EQ(out[5], -3000);
  CHECK(p.state() == PlayState::Ended);
}

EPDJ_TEST(player_pause_and_mono) {
  const auto b = wav(1, 48000, {7, 8, 9, 10});
  Mem m{b.data(), b.size(), 0};
  Player p;
  CHECK(p.load(&memRead, &memSeek, &m) == WavError::Ok);
  p.play();
  int16_t out[4] = {};
  CHECK_EQ(p.pull(out, 1), 1);
  CHECK_EQ(out[0], 7);
  CHECK_EQ(out[1], 7);
  p.pause();
  CHECK_EQ(p.pull(out, 1), 0);
  p.play();
  CHECK_EQ(p.pull(out, 1), 1);
  CHECK_EQ(out[0], 8);
}

EPDJ_TEST(player_44100_lands_near_the_right_length) {
  std::vector<int16_t> frames(441, 1000);
  const auto b = wav(1, 44100, frames);
  Mem m{b.data(), b.size(), 0};
  Player p;
  CHECK(p.load(&memRead, &memSeek, &m) == WavError::Ok);
  p.play();
  int16_t block[96];
  size_t got = 0;
  for (;;) {
    const size_t n = p.pull(block, 48);
    got += n;
    if (n < 48) break;
    CHECK(got < 1000);
  }
  // 441 samples at 44.1k is 480 samples at 48k. The hold starts on the first sample, so the count is 481.
  CHECK_EQ(got, 481);
}

EPDJ_TEST(screen_names_the_track_and_the_state) {
  Catalog cat;
  cat.add("kick.wav");
  cat.add("hat.wav");
  Screen s;
  formatScreen(s, cat, 1, PlayState::Playing);
  CHECK_EQ(std::strcmp(s.top, "2/2 PLAY"), 0);
  CHECK_EQ(std::strcmp(s.bottom, "hat.wav"), 0);
  Catalog empty;
  formatScreen(s, empty, 0, PlayState::Empty);
  CHECK_EQ(std::strcmp(s.top, "NO CARD"), 0);
}
