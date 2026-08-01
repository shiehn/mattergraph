#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

#include "mattergraph/midi/clipspec.h"
#include "mattergraph/render/renderer.h"
#include "mattergraph/skin/soundskin.h"

using mattergraph::midi::buildTimelineFromClipSpecFile;
using mattergraph::render::renderTimeline;
using mattergraph::render::RenderResult;
using mattergraph::skin::loadSoundSkinFromFile;

namespace {

std::filesystem::path fixture(const char* rel) {
  return std::filesystem::path(MG_FIXTURES_DIR) / rel;
}
std::filesystem::path skinPath(const char* name) {
  return std::filesystem::path(MG_SKINS_DIR) / "anchors" / name;
}

// RMS of one note's window, interleaved stereo.
double windowRms(const std::vector<float>& x, std::int64_t from, std::int64_t to) {
  double sum = 0.0;
  for (std::int64_t n = from; n < to; ++n) {
    const double l = x[static_cast<std::size_t>(2 * n)];
    const double r = x[static_cast<std::size_t>(2 * n + 1)];
    sum += l * l + r * r;
  }
  return std::sqrt(sum / (2.0 * static_cast<double>(to - from)));
}

}  // namespace

TEST_CASE("render is deterministic: same inputs, identical samples") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/bass_groove.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("glass.json"));
  const RenderResult a = renderTimeline(tl, skin, 48291);
  const RenderResult b = renderTimeline(tl, skin, 48291);
  REQUIRE(a.interleaved.size() == b.interleaved.size());
  REQUIRE(a.interleaved == b.interleaved);
}

TEST_CASE("different seeds change the noise realization, not the schedule") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/bass_groove.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("glass.json"));
  const RenderResult a = renderTimeline(tl, skin, 1);
  const RenderResult b = renderTimeline(tl, skin, 2);
  CHECK(a.interleaved.size() == b.interleaved.size());
  CHECK(a.interleaved != b.interleaved);
  CHECK(a.audit.passed);
  CHECK(b.audit.passed);
}

TEST_CASE("fidelity audit: every note rendered at its exact sample") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/bass_groove.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("wood_bar.json"));
  const RenderResult r = renderTimeline(tl, skin, 7);
  CHECK(r.audit.passed);
  CHECK(r.audit.event_count_input == 8);
  CHECK(r.audit.event_count_rendered == 8);
  CHECK(r.audit.max_on_error_samples == 0);
  CHECK(r.audit.max_off_error_samples == 0);
  CHECK(r.audit.dropped_voices == 0);
}

TEST_CASE("exact pitch: audit reports the equal-temperament frequency") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/velocity_ladder.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("glass.json"));
  const RenderResult r = renderTimeline(tl, skin, 7);
  for (const auto& v : r.audit.voices) {
    // C3 = MIDI 48 -> 440 * 2^((48-69)/12)
    CHECK(std::abs(v.pitch_hz - 130.8127826502993) < 1e-9);
  }
}

TEST_CASE("no silence where notes exist (hard gate §12.1)") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/velocity_ladder.clipspec.json"), 48000);
  for (const char* name : {"glass.json", "wood_bar.json", "metal_bell.json", "membrane.json"}) {
    const auto skin = loadSoundSkinFromFile(skinPath(name));
    const RenderResult r = renderTimeline(tl, skin, 42);
    REQUIRE(r.audit.passed);
    for (const auto& note : tl.notes()) {
      const double rms = windowRms(r.interleaved, note.on_sample, note.off_sample);
      INFO(name << " note at sample " << note.on_sample);
      CHECK(rms > 1e-4);  // > -80 dBFS: audibly present
    }
  }
}

TEST_CASE("velocity ladder produces monotonically increasing energy") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/velocity_ladder.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("glass.json"));
  const RenderResult r = renderTimeline(tl, skin, 42);
  double prev = 0.0;
  for (const auto& note : tl.notes()) {
    const double rms = windowRms(r.interleaved, note.on_sample, note.off_sample);
    CHECK(rms > prev);
    prev = rms;
  }
}

TEST_CASE("energy decays: tail is quieter than attack for a damped skin") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/velocity_ladder.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("membrane.json"));
  const RenderResult r = renderTimeline(tl, skin, 42);
  const auto& last = tl.notes().back();
  const double attack = windowRms(r.interleaved, last.on_sample, last.on_sample + 4800);
  const std::int64_t end = r.stats.frames;
  const double tail = windowRms(r.interleaved, end - 4800, end);
  CHECK(tail < attack * 0.1);
}

TEST_CASE("normalize option hits the requested peak") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/bass_groove.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("metal_bell.json"));
  const RenderResult r = renderTimeline(tl, skin, 5, -1.0);
  float peak = 0.0f;
  for (float s : r.interleaved) {
    peak = std::max(peak, std::abs(s));
  }
  CHECK(std::abs(20.0 * std::log10(static_cast<double>(peak)) - (-1.0)) < 0.05);
}
