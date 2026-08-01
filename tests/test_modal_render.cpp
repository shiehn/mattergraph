#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

#include "mattergraph/midi/clipspec.h"
#include "mattergraph/render/renderer.h"
#include "mattergraph/render/wav.h"
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

TEST_CASE("friction exciter sustains while the note is held, then decays") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("bowed_glass.json"));
  const RenderResult r = renderTimeline(tl, skin, 11);
  REQUIRE(r.audit.passed);
  // Note: C3 held 4 s (6 qn at 90 bpm). Early vs late held windows must both
  // carry energy of the same order — the gesture sustains, unlike a strike.
  const double early = windowRms(r.interleaved, 24000, 48000);   // 0.5-1.0 s
  const double late = windowRms(r.interleaved, 120000, 144000);  // 2.5-3.0 s
  REQUIRE(early > 1e-4);
  REQUIRE(late > 1e-4);
  CHECK(late > early * 0.25);   // within ~12 dB: sustained, not decayed away
  // After note-off (4 s), the tail must actually decay.
  const std::int64_t off = tl.notes()[0].off_sample;
  const double tail = windowRms(r.interleaved, off + 48000,
                                std::min<std::int64_t>(off + 72000, r.stats.frames));
  CHECK(tail < late * 0.3);
}

TEST_CASE("friction renders are deterministic") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("scraped_metal.json"));
  const RenderResult a = renderTimeline(tl, skin, 5);
  const RenderResult b = renderTimeline(tl, skin, 5);
  REQUIRE(a.interleaved == b.interleaved);
}

TEST_CASE("wav reader roundtrips the writer's output") {
  namespace fs = std::filesystem;
  std::vector<float> tone(4800);
  for (std::size_t n = 0; n < tone.size(); ++n) {
    tone[n] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f *
                              static_cast<float>(n) / 48000.0f);
  }
  const fs::path tmp = fs::temp_directory_path() / "mg_roundtrip.wav";
  mattergraph::render::writeWavF32(tmp, tone, 48000, 1);
  const auto back = mattergraph::render::readWavMono(tmp, 48000);
  REQUIRE(back.size() == tone.size());
  for (std::size_t n = 0; n < tone.size(); n += 480) {
    CHECK(std::abs(back[n] - tone[n]) < 1e-6f);
  }
  fs::remove(tmp);
}

TEST_CASE("periodic exciter sustains at pitch and decays after note-off") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("periodic_fat_bass.json"));
  const RenderResult a = renderTimeline(tl, skin, 21);
  const RenderResult b = renderTimeline(tl, skin, 21);
  REQUIRE(a.audit.passed);
  REQUIRE(a.interleaved == b.interleaved);  // fixed phases: fully deterministic
  const double early = windowRms(a.interleaved, 24000, 48000);
  const double late = windowRms(a.interleaved, 120000, 144000);
  REQUIRE(early > 1e-4);
  CHECK(late > early * 0.25);
  const std::int64_t off = tl.notes()[0].off_sample;
  const double tail = windowRms(a.interleaved, off + 48000,
                                std::min<std::int64_t>(off + 72000, a.stats.frames));
  CHECK(tail < late * 0.3);
}

TEST_CASE("sample exciter renders deterministically from provided PCM") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_strike.clipspec.json"), 48000);
  const auto skin = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "sample_test", "skin_seed": 9,
    "exciter": {"type": "sample", "sample": "inline.wav", "sample_blend": 0.8},
    "body": {"mode_count": 12, "t60_base_s": 1.0}})");
  // Deterministic synthetic transient in place of a bank asset.
  std::vector<float> pcm(2400);
  for (std::size_t n = 0; n < pcm.size(); ++n) {
    const float t = static_cast<float>(n) / 48000.0f;
    pcm[n] = std::exp(-t * 80.0f) * std::sin(2.0f * 3.14159265f * 900.0f * t);
  }
  const RenderResult a = renderTimeline(tl, skin, 3, 0.0, &pcm);
  const RenderResult b = renderTimeline(tl, skin, 3, 0.0, &pcm);
  REQUIRE(a.audit.passed);
  REQUIRE(a.interleaved == b.interleaved);
  const auto& note = tl.notes()[0];
  CHECK(windowRms(a.interleaved, note.on_sample, note.off_sample) > 1e-4);
  // A sample-type skin without PCM must be rejected, not rendered silent.
  CHECK_THROWS(renderTimeline(tl, skin, 3, 0.0, nullptr));
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
