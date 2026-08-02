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

TEST_CASE("bar ratio law places the bar partial family, string law cannot") {
  // A bar body's second partial sits near 4x the fundamental (marimba family);
  // the stiff-string law tops out near 2.3x. Verify via spectral peaks of a
  // single C4 strike with few, long-ringing modes.
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_strike.clipspec.json"), 48000);
  const auto skin = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "bar_test", "skin_seed": 4,
    "exciter": {"type": "impulse"},
    "body": {"ratio_law": "bar", "mode_count": 3, "inharmonicity": 0.0,
             "irregularity": 0.0, "t60_base_s": 2.0, "brightness": 1.0,
             "position": 0.3},
    "release": {"mode": "natural"}})");
  const RenderResult r = renderTimeline(tl, skin, 4);
  // Spectral probe of the tail (attack excluded), clamped inside the buffer.
  const std::size_t n0 = 24000;
  const std::size_t n1 = std::min<std::size_t>(
      72000, static_cast<std::size_t>(r.stats.frames));
  REQUIRE(n1 > n0 + 24000);
  std::vector<double> x(n1 - n0);
  for (std::size_t n = n0; n < n1; ++n) {
    x[n - n0] = static_cast<double>(r.interleaved[2 * n]) +
                static_cast<double>(r.interleaved[2 * n + 1]);
  }
  // Goertzel probe at candidate partial frequencies of C4 (261.63 Hz).
  auto power_at = [&](double f) {
    const double w = 2.0 * 3.14159265358979 * f / 48000.0;
    double s0 = 0, s1 = 0, s2 = 0;
    for (double v : x) {
      s0 = v + 2.0 * std::cos(w) * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2;
  };
  const double f0 = 261.6255653005986;
  const double p_fund = power_at(f0);
  const double p_bar2 = power_at(4.0 * f0);    // bar's 2nd partial (k²)
  const double p_string2 = power_at(2.0 * f0); // string's 2nd would sit ~2x
  REQUIRE(p_fund > 0.0);
  CHECK(p_bar2 > p_string2 * 10.0);  // energy lives at 4x, not 2x
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

TEST_CASE("space stage: mix 0 is byte-identical bypass; mix > 0 is deterministic") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_strike.clipspec.json"), 48000);
  const auto dry = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "sp0", "skin_seed": 5,
    "body": {"mode_count": 8, "t60_base_s": 0.8},
    "radiation": {"space_mix": 0.0}})");
  const auto dry2 = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "sp0", "skin_seed": 5,
    "body": {"mode_count": 8, "t60_base_s": 0.8}})");
  REQUIRE(renderTimeline(tl, dry, 9).interleaved ==
          renderTimeline(tl, dry2, 9).interleaved);

  const auto wet = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "sp1", "skin_seed": 5,
    "body": {"mode_count": 8, "t60_base_s": 0.8},
    "radiation": {"space_mix": 0.3, "space_size": 0.6}})");
  const RenderResult a = renderTimeline(tl, wet, 9);
  const RenderResult b = renderTimeline(tl, wet, 9);
  REQUIRE(a.interleaved == b.interleaved);
  CHECK(a.stats.frames > renderTimeline(tl, dry, 9).stats.frames);  // tail extended
}

TEST_CASE("loop fold: render is exactly loop length and keeps the tail energy") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_strike.clipspec.json"), 48000);
  // Quiet long-ringing skin: peaks stay < 0.999 in BOTH renders, so the safety
  // stage never scales and fold conservation is observable directly.
  const auto skin = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "fold_test", "skin_seed": 3,
    "body": {"mode_count": 10, "t60_base_s": 4.0},
    "release": {"mode": "natural"},
    "radiation": {"gain": 0.05}})");
  const std::int64_t loop = 48000;  // 1 s
  const RenderResult folded = renderTimeline(tl, skin, 4, 0.0, nullptr, loop);
  CHECK(folded.stats.frames == loop);
  const RenderResult full = renderTimeline(tl, skin, 4);
  REQUIRE(full.stats.frames > loop);
  REQUIRE(!folded.stats.peak_limited);
  REQUIRE(!full.stats.peak_limited);
  // Folding must carry MORE energy into the loop window than truncation.
  auto energy = [](const std::vector<float>& x, std::size_t upto) {
    double e = 0;
    for (std::size_t i = 0; i < std::min(upto, x.size()); i++) e += double(x[i]) * x[i];
    return e; };
  const auto loopFrames = static_cast<std::size_t>(2 * loop);
  CHECK(energy(folded.interleaved, loopFrames) >
        energy(full.interleaved, loopFrames) * 1.01);
}

TEST_CASE("pluck_string: exact pitch, plucked decay, deterministic") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  const auto skin = loadSoundSkinFromFile(skinPath("guitar_pluck.json"));
  const RenderResult a = renderTimeline(tl, skin, 11);
  const RenderResult b = renderTimeline(tl, skin, 11);
  REQUIRE(a.audit.passed);
  REQUIRE(a.interleaved == b.interleaved);
  // Pitch: strongest energy at C3's fundamental (Goertzel probe, tail window).
  const std::size_t n0 = 12000;
  const std::size_t n1 = std::min<std::size_t>(60000, static_cast<std::size_t>(a.stats.frames));
  std::vector<double> x(n1 - n0);
  for (std::size_t n = n0; n < n1; ++n) {
    x[n - n0] = double(a.interleaved[2 * n]) + double(a.interleaved[2 * n + 1]);
  }
  auto power_at = [&](double f) {
    const double w = 2.0 * 3.14159265358979 * f / 48000.0;
    double s0 = 0, s1 = 0, s2 = 0;
    for (double v : x) { s0 = v + 2.0 * std::cos(w) * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - 2.0 * std::cos(w) * s1 * s2;
  };
  const double f0 = 130.8127826502993;  // C3
  CHECK(power_at(f0) > power_at(f0 * 1.335) * 3.0);  // vs an off-harmonic point
  // Plucked: decays while held (unlike friction's flat sustain).
  const double early = windowRms(a.interleaved, 6000, 30000);
  const double late = windowRms(a.interleaved, 120000, 144000);
  CHECK(late < early * 0.8);
}

TEST_CASE("breath and brass sustain at pitch, deterministically") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  for (const char* name : {"airy_flute.json", "brass_swell.json"}) {
    const auto skin = loadSoundSkinFromFile(skinPath(name));
    const RenderResult a = renderTimeline(tl, skin, 13);
    const RenderResult b = renderTimeline(tl, skin, 13);
    REQUIRE(a.audit.passed);
    REQUIRE(a.interleaved == b.interleaved);
    const double early = windowRms(a.interleaved, 24000, 48000);
    const double late = windowRms(a.interleaved, 120000, 144000);
    INFO(name);
    REQUIRE(early > 1e-4);
    CHECK(late > early * 0.2);  // sustained gesture
  }
}

TEST_CASE("wavetable exciter loops a provided table at exact pitch") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_sustain.clipspec.json"), 48000);
  const auto skin = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "wt_test", "skin_seed": 6,
    "exciter": {"type": "wavetable", "wavetable": "inline.wav", "detune_cents": 6},
    "body": {"mode_count": 10, "inharmonicity": 0.0, "t60_base_s": 1.2},
    "release": {"mode": "natural"}})");
  // Simple band-limited table: fundamental + 3 harmonics, 2048 samples.
  std::vector<float> table(2048);
  for (std::size_t n = 0; n < table.size(); ++n) {
    const double ph = 2.0 * 3.14159265358979 * static_cast<double>(n) / 2048.0;
    table[n] = static_cast<float>(std::sin(ph) + 0.5 * std::sin(2 * ph) +
                                  0.25 * std::sin(3 * ph));
  }
  const RenderResult a = renderTimeline(tl, skin, 5, 0.0, nullptr, 0, &table);
  const RenderResult b = renderTimeline(tl, skin, 5, 0.0, nullptr, 0, &table);
  REQUIRE(a.audit.passed);
  REQUIRE(a.interleaved == b.interleaved);
  CHECK(windowRms(a.interleaved, 24000, 96000) > 1e-4);
  CHECK_THROWS(renderTimeline(tl, skin, 5, 0.0, nullptr, 0, nullptr));
}

TEST_CASE("polish genes at zero are byte-identical bypass") {
  const auto tl = buildTimelineFromClipSpecFile(fixture("probes/diag_strike.clipspec.json"), 48000);
  const auto plain = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "p0", "skin_seed": 8,
    "body": {"mode_count": 8, "t60_base_s": 0.6}})");
  const auto zeroed = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "p0", "skin_seed": 8,
    "body": {"mode_count": 8, "t60_base_s": 0.6},
    "radiation": {"chorus": 0.0, "sat": 0.0, "motion": 0.0}})");
  REQUIRE(renderTimeline(tl, plain, 3).interleaved ==
          renderTimeline(tl, zeroed, 3).interleaved);
  const auto polished = mattergraph::skin::loadSoundSkinFromJson(R"({
    "schema_version": "0.1.0", "name": "p1", "skin_seed": 8,
    "body": {"mode_count": 8, "t60_base_s": 0.6},
    "radiation": {"chorus": 0.4, "sat": 0.3, "motion": 0.3}})");
  const RenderResult a = renderTimeline(tl, polished, 3);
  const RenderResult b = renderTimeline(tl, polished, 3);
  REQUIRE(a.interleaved == b.interleaved);
  CHECK(a.interleaved != renderTimeline(tl, plain, 3).interleaved);
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
