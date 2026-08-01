#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "mattergraph/midi/smf.h"

using mattergraph::midi::buildTimelineFromSmfBytes;
using mattergraph::midi::SmfErrc;
using mattergraph::midi::SmfError;

namespace {

// Minimal SMF byte-stream builder for fixtures.
class SmfBuilder {
 public:
  SmfBuilder& header(std::uint16_t format, std::uint16_t ntrks, std::uint16_t division) {
    tag("MThd");
    u32(6);
    u16(format);
    u16(ntrks);
    u16(division);
    return *this;
  }
  SmfBuilder& beginTrack() {
    track_.clear();
    return *this;
  }
  SmfBuilder& vlq(std::uint32_t v) {
    std::uint8_t stack[4];
    int n = 0;
    do {
      stack[n++] = static_cast<std::uint8_t>(v & 0x7F);
      v >>= 7;
    } while (v != 0);
    while (n-- > 0) {
      track_.push_back(static_cast<std::uint8_t>(stack[n] | (n > 0 ? 0x80 : 0)));
    }
    return *this;
  }
  SmfBuilder& bytes(std::initializer_list<std::uint8_t> b) {
    track_.insert(track_.end(), b);
    return *this;
  }
  SmfBuilder& tempo(std::uint32_t delta, std::uint32_t usec) {
    vlq(delta);
    bytes({0xFF, 0x51, 0x03, static_cast<std::uint8_t>(usec >> 16),
           static_cast<std::uint8_t>(usec >> 8), static_cast<std::uint8_t>(usec)});
    return *this;
  }
  SmfBuilder& noteOn(std::uint32_t delta, std::uint8_t ch, std::uint8_t pitch, std::uint8_t vel) {
    vlq(delta);
    bytes({static_cast<std::uint8_t>(0x90 | ch), pitch, vel});
    return *this;
  }
  SmfBuilder& noteOff(std::uint32_t delta, std::uint8_t ch, std::uint8_t pitch) {
    vlq(delta);
    bytes({static_cast<std::uint8_t>(0x80 | ch), pitch, 64});
    return *this;
  }
  SmfBuilder& raw(std::initializer_list<std::uint8_t> b) { return bytes(b); }
  SmfBuilder& endTrack() {
    vlq(0);
    bytes({0xFF, 0x2F, 0x00});
    tag("MTrk");
    u32(static_cast<std::uint32_t>(track_.size()));
    out_.insert(out_.end(), track_.begin(), track_.end());
    return *this;
  }
  const std::vector<std::uint8_t>& data() const { return out_; }

 private:
  void tag(const char* t) {
    for (int i = 0; i < 4; ++i) {
      out_.push_back(static_cast<std::uint8_t>(t[i]));
    }
  }
  void u16(std::uint16_t v) {
    out_.push_back(static_cast<std::uint8_t>(v >> 8));
    out_.push_back(static_cast<std::uint8_t>(v));
  }
  void u32(std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
      out_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
  }
  std::vector<std::uint8_t> track_;
  std::vector<std::uint8_t> out_;
};

}  // namespace

TEST_CASE("SMF fixed tempo: one quarter note at 120 bpm is exactly 24000 samples") {
  SmfBuilder b;
  b.header(0, 1, 480)
      .beginTrack()
      .tempo(0, 500000)
      .noteOn(480, 0, 60, 100)
      .noteOff(480, 0, 60)
      .endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 1);
  CHECK(tl.notes()[0].on_sample == 24000);
  CHECK(tl.notes()[0].off_sample == 48000);
  CHECK(tl.bpm() == 120.0);
}

TEST_CASE("SMF tempo change lands events at exact hand-computed samples") {
  // 480 ticks at 500000 usec/qn (0.5 s) then 480 ticks at 250000 (0.25 s):
  // note at tick 960 must land at exactly 0.75 s = 36000 samples.
  SmfBuilder b;
  b.header(0, 1, 480)
      .beginTrack()
      .tempo(0, 500000)
      .tempo(480, 250000)
      .noteOn(480, 0, 60, 100)  // delta from tempo event at 480 -> tick 960
      .noteOff(240, 0, 60)
      .endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 1);
  CHECK(tl.notes()[0].on_sample == 36000);
  // 240 ticks at 250000 usec/qn = 0.125 s.
  CHECK(tl.notes()[0].off_sample == 36000 + 6000);
}

TEST_CASE("SMF exact-arithmetic canary: numerator beyond double precision") {
  // 2000 segments of 1000 ticks at 999999 usec/qn accumulate a numerator of
  // 9.59999904e16 > 2^53; exact integer math must land at exactly
  // 2000 * 1000 * 999999 * 48000 / (1e6 * 96) = 999999000 samples.
  SmfBuilder b;
  b.header(0, 1, 96).beginTrack();
  b.tempo(0, 999999);
  for (int i = 0; i < 1999; ++i) {
    b.tempo(1000, 999999);  // same tempo, forcing segment accumulation
  }
  b.noteOn(1000, 0, 60, 100).noteOff(96, 0, 60).endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 1);
  CHECK(tl.notes()[0].on_sample == 999999000LL);
}

TEST_CASE("SMF running status and velocity-0-as-note-off") {
  SmfBuilder b;
  b.header(0, 1, 480)
      .beginTrack()
      .noteOn(0, 0, 60, 100)
      // Running status: no status byte, direct data (pitch 64 on, then both off via vel 0).
      .raw({0x60, 64, 90})        // delta 0x60=96 ticks, note on 64 vel 90
      .raw({0x00, 60, 0})         // delta 0, note 60 off via vel 0
      .raw({0x60, 64, 0})         // delta 96, note 64 off via vel 0
      .endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 2);
  CHECK(tl.notes()[0].pitch == 60);
  CHECK(tl.notes()[0].on_sample == 0);
  CHECK(tl.notes()[0].off_sample == 4800);   // 96 ticks at 120bpm/480ppq = 0.1 s
  CHECK(tl.notes()[1].pitch == 64);
  CHECK(tl.notes()[1].on_sample == 4800);
  CHECK(tl.notes()[1].off_sample == 9600);
}

TEST_CASE("SMF format 1: same-tick notes order by track then source order") {
  SmfBuilder b;
  b.header(1, 2, 480)
      .beginTrack()
      .tempo(0, 500000)
      .noteOn(0, 0, 72, 100)
      .noteOff(480, 0, 72)
      .endTrack()
      .beginTrack()
      .noteOn(0, 1, 48, 100)
      .noteOff(480, 1, 48)
      .endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 2);
  CHECK(tl.notes()[0].pitch == 72);  // track 0 first at the shared tick
  CHECK(tl.notes()[1].pitch == 48);
  CHECK(tl.notes()[0].source_index == 0);
  CHECK(tl.notes()[1].source_index == 1);
}

TEST_CASE("SMF unterminated note closes at end of track") {
  SmfBuilder b;
  b.header(0, 1, 480)
      .beginTrack()
      .noteOn(0, 0, 60, 100)
      .noteOn(480, 0, 64, 100)
      .noteOff(480, 0, 64)   // note 60 never gets an off; track ends at tick 960
      .endTrack();
  const auto tl = buildTimelineFromSmfBytes(b.data(), 48000);
  REQUIRE(tl.notes().size() == 2);
  CHECK(tl.notes()[0].pitch == 60);
  CHECK(tl.notes()[0].off_sample == 48000);  // tick 960 = 1.0 s
}

TEST_CASE("SMF rejects SMPTE division and bad headers") {
  {
    SmfBuilder b;
    b.header(0, 1, 0x8000 | 0x1E00).beginTrack().endTrack();
    try {
      (void)buildTimelineFromSmfBytes(b.data(), 48000);
      FAIL("expected SmfError");
    } catch (const SmfError& e) {
      CHECK(e.code() == SmfErrc::unsupported_smpte);
    }
  }
  {
    const std::vector<std::uint8_t> junk = {'R', 'I', 'F', 'F', 0, 0, 0, 0};
    try {
      (void)buildTimelineFromSmfBytes(junk, 48000);
      FAIL("expected SmfError");
    } catch (const SmfError& e) {
      CHECK(e.code() == SmfErrc::bad_header);
    }
  }
}
