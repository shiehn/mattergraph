#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "mattergraph/midi/clipspec.h"

using mattergraph::midi::CanonicalTimeline;
using mattergraph::midi::ClipSpecErrc;
using mattergraph::midi::ClipSpecError;
using mattergraph::midi::buildTimelineFromClipSpecFile;
using mattergraph::midi::buildTimelineFromClipSpecJson;

namespace {

std::filesystem::path fixturePath(const char* name) {
  return std::filesystem::path(MG_FIXTURES_DIR) / "probes" / name;
}

ClipSpecErrc errcOf(const char* json) {
  try {
    (void)buildTimelineFromClipSpecJson(json, 48000);
  } catch (const ClipSpecError& e) {
    return e.code();
  }
  FAIL("expected ClipSpecError");
  return ClipSpecErrc::bad_value;  // unreachable
}

}  // namespace

TEST_CASE("velocity ladder fixture: exact sample positions at 120 bpm / 48 kHz") {
  // 120 bpm at 48 kHz: one quarter note = exactly 24000 samples.
  const auto tl = buildTimelineFromClipSpecFile(fixturePath("velocity_ladder.clipspec.json"), 48000);

  REQUIRE(tl.notes().size() == 8);
  CHECK(tl.sampleRate() == 48000);
  CHECK(tl.bpm() == 120.0);
  CHECK(tl.timeSigNumerator() == 4);
  CHECK(tl.timeSigDenominator() == 4);

  const std::int32_t expected_vel[] = {16, 32, 48, 64, 80, 96, 112, 127};
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& n = tl.notes()[i];
    CHECK(n.pitch == 48);
    CHECK(n.velocity == expected_vel[i]);
    CHECK(n.on_sample == static_cast<std::int64_t>(i) * 24000);
    CHECK(n.off_sample == n.on_sample + 21600);  // dur 0.9 qn = 21600 samples
    CHECK(n.channel == 0);
    CHECK(n.source_index == static_cast<std::int32_t>(i));
  }
  CHECK(tl.totalSamples() == 7 * 24000 + 21600);
}

TEST_CASE("bass groove fixture: triplet rounding is exact at 96 bpm / 48 kHz") {
  // 96 bpm at 48 kHz: one quarter note = exactly 30000 samples.
  const auto tl = buildTimelineFromClipSpecFile(fixturePath("bass_groove.clipspec.json"), 48000);

  REQUIRE(tl.notes().size() == 8);
  // Note at 2 + 2/3 qn (triplet): 2.6666666666666665 * 30000 rounds to 80000.
  const auto& triplet = tl.notes()[4];
  CHECK(triplet.pitch == 38);
  CHECK(triplet.on_sample == 80000);
}

TEST_CASE("same-sample events preserve source order") {
  const char* spec = R"({
    "schema_version": "0.1.0", "bpm": 120,
    "notes": [
      {"pitch": 60, "start_qn": 1.0, "dur_qn": 1.0, "vel": 100},
      {"pitch": 48, "start_qn": 0.0, "dur_qn": 1.0, "vel": 100},
      {"pitch": 72, "start_qn": 1.0, "dur_qn": 1.0, "vel": 100}
    ]})";
  const auto tl = buildTimelineFromClipSpecJson(spec, 48000);
  REQUIRE(tl.notes().size() == 3);
  CHECK(tl.notes()[0].pitch == 48);
  // Both at sample 24000; pitch 60 came first in the source, so it stays first.
  CHECK(tl.notes()[1].pitch == 60);
  CHECK(tl.notes()[1].source_index == 0);
  CHECK(tl.notes()[2].pitch == 72);
  CHECK(tl.notes()[2].source_index == 2);
}

TEST_CASE("building twice from the same source yields identical timelines") {
  const auto a = buildTimelineFromClipSpecFile(fixturePath("bass_groove.clipspec.json"), 48000);
  const auto b = buildTimelineFromClipSpecFile(fixturePath("bass_groove.clipspec.json"), 48000);
  CHECK(a == b);
}

TEST_CASE("sub-sample durations still occupy at least one sample") {
  const char* spec = R"({
    "schema_version": "0.1.0", "bpm": 120,
    "notes": [{"pitch": 60, "start_qn": 0.0, "dur_qn": 1e-9, "vel": 100}]})";
  const auto tl = buildTimelineFromClipSpecJson(spec, 48000);
  REQUIRE(tl.notes().size() == 1);
  CHECK(tl.notes()[0].on_sample == 0);
  CHECK(tl.notes()[0].off_sample == 1);
}

TEST_CASE("chan defaults to 0 and is honored when present") {
  const char* spec = R"({
    "schema_version": "0.1.0", "bpm": 120,
    "notes": [
      {"pitch": 60, "start_qn": 0.0, "dur_qn": 1.0, "vel": 100},
      {"pitch": 61, "start_qn": 1.0, "dur_qn": 1.0, "vel": 100, "chan": 9}
    ]})";
  const auto tl = buildTimelineFromClipSpecJson(spec, 48000);
  CHECK(tl.notes()[0].channel == 0);
  CHECK(tl.notes()[1].channel == 9);
}

TEST_CASE("validation rejects contract violations with the right error codes") {
  CHECK(errcOf(R"(not json)") == ClipSpecErrc::bad_json);
  CHECK(errcOf(R"({"bpm": 120, "notes": []})") == ClipSpecErrc::missing_field);
  CHECK(errcOf(R"({"schema_version": "9.9.9", "bpm": 120, "notes": []})") ==
        ClipSpecErrc::bad_schema_version);
  CHECK(errcOf(R"({"schema_version": "0.1.0", "bpm": 0, "notes": []})") ==
        ClipSpecErrc::bad_value);
  CHECK(errcOf(R"({"schema_version": "0.1.0", "bpm": 120,
                   "notes": [{"pitch": 128, "start_qn": 0, "dur_qn": 1, "vel": 100}]})") ==
        ClipSpecErrc::bad_value);
  CHECK(errcOf(R"({"schema_version": "0.1.0", "bpm": 120,
                   "notes": [{"pitch": 60, "start_qn": 0, "dur_qn": 1, "vel": 0}]})") ==
        ClipSpecErrc::bad_value);
  CHECK(errcOf(R"({"schema_version": "0.1.0", "bpm": 120,
                   "notes": [{"pitch": 60, "start_qn": -1, "dur_qn": 1, "vel": 100}]})") ==
        ClipSpecErrc::bad_value);
  CHECK(errcOf(R"({"schema_version": "0.1.0", "bpm": 120,
                   "notes": [{"pitch": 60, "start_qn": 0, "dur_qn": 0, "vel": 100}]})") ==
        ClipSpecErrc::bad_value);
}
