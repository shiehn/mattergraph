// mattergraph-render: deterministic offline renderer CLI (plan §10, v0 subset).
//
//   mattergraph-render --midi phrase.clipspec.json --skin glass.json
//       --seed 1 --sample-rate 48000 --out /tmp/job-0001 [--normalize -1.0]
//
// Writes audio.wav, render_result.json, midi_fidelity_audit.json into --out.
// Exit codes: 0 ok, 2 invalid MIDI, 3 invalid skin, 4 render/safety failure,
// 5 IO failure, 6 fidelity audit failure, 64 usage.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "mattergraph/midi/clipspec.h"
#include "mattergraph/midi/smf.h"
#include "mattergraph/render/renderer.h"
#include "mattergraph/render/wav.h"
#include "mattergraph/skin/soundskin.h"
#include "mattergraph/version.h"

namespace {

struct Args {
  std::filesystem::path midi;
  std::filesystem::path skin;
  std::filesystem::path out;
  std::filesystem::path exciter_dir;
  std::uint64_t seed = 0;
  std::uint32_t sample_rate = 48000;
  std::int64_t loop_samples = 0;
  std::optional<double> normalize_dbfs;
};

int usage() {
  std::cerr << "usage: mattergraph-render --midi <clipspec.json> --skin <skin.json> "
               "--out <dir> [--seed N] [--sample-rate HZ] [--normalize DBFS]\n";
  return 64;
}

std::optional<Args> parseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    const char* v = nullptr;
    if (flag == "--midi" && (v = next())) {
      a.midi = v;
    } else if (flag == "--skin" && (v = next())) {
      a.skin = v;
    } else if (flag == "--out" && (v = next())) {
      a.out = v;
    } else if (flag == "--seed" && (v = next())) {
      a.seed = std::stoull(v);
    } else if (flag == "--sample-rate" && (v = next())) {
      a.sample_rate = static_cast<std::uint32_t>(std::stoul(v));
    } else if (flag == "--normalize" && (v = next())) {
      a.normalize_dbfs = std::stod(v);
    } else if (flag == "--exciter-dir" && (v = next())) {
      a.exciter_dir = v;
    } else if (flag == "--loop-samples" && (v = next())) {
      a.loop_samples = std::stoll(v);
    } else {
      return std::nullopt;
    }
  }
  if (a.midi.empty() || a.skin.empty() || a.out.empty()) {
    return std::nullopt;
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  const auto args = parseArgs(argc, argv);
  if (!args) {
    return usage();
  }

  using nlohmann::json;
  const auto t0 = std::chrono::steady_clock::now();

  mattergraph::midi::CanonicalTimeline timeline{0, 0, 0, 0, {}, 0};
  try {
    // Dispatch by magic: Standard MIDI Files start with "MThd"; anything else
    // is treated as ClipSpec JSON.
    char magic[4] = {};
    std::ifstream probe(args->midi, std::ios::binary);
    probe.read(magic, 4);
    if (probe.gcount() == 4 && std::memcmp(magic, "MThd", 4) == 0) {
      timeline = mattergraph::midi::buildTimelineFromSmfFile(args->midi,
                                                             args->sample_rate);
    } else {
      timeline = mattergraph::midi::buildTimelineFromClipSpecFile(args->midi,
                                                                  args->sample_rate);
    }
  } catch (const std::exception& e) {
    std::cerr << "invalid MIDI: " << e.what() << "\n";
    return 2;
  }

  mattergraph::skin::SoundSkin skin;
  try {
    skin = mattergraph::skin::loadSoundSkinFromFile(args->skin);
  } catch (const std::exception& e) {
    std::cerr << "invalid skin: " << e.what() << "\n";
    return 3;
  }

  std::vector<float> exciter_pcm;
  if (skin.exciter.type == mattergraph::skin::ExciterType::sample) {
    if (args->exciter_dir.empty()) {
      std::cerr << "invalid skin: sample-type skin needs --exciter-dir\n";
      return 3;
    }
    try {
      exciter_pcm = mattergraph::render::readWavMono(
          args->exciter_dir / skin.exciter.sample, args->sample_rate);
    } catch (const std::exception& e) {
      std::cerr << "invalid skin: " << e.what() << "\n";
      return 3;
    }
  }

  mattergraph::render::RenderResult result;
  try {
    result = mattergraph::render::renderTimeline(
        timeline, skin, args->seed, args->normalize_dbfs.value_or(0.0),
        exciter_pcm.empty() ? nullptr : &exciter_pcm, args->loop_samples);
  } catch (const std::exception& e) {
    std::cerr << "render failure: " << e.what() << "\n";
    return 4;
  }

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();

  try {
    std::filesystem::create_directories(args->out);
    mattergraph::render::writeWavF32(args->out / "audio.wav", result.interleaved,
                                     args->sample_rate, 2);

    json audit;
    audit["status"] = result.audit.passed ? "passed" : "failed";
    audit["event_count_input"] = result.audit.event_count_input;
    audit["event_count_render_timeline"] = result.audit.event_count_rendered;
    audit["dropped_voices"] = result.audit.dropped_voices;
    audit["max_on_error_samples"] = result.audit.max_on_error_samples;
    audit["max_off_error_samples"] = result.audit.max_off_error_samples;
    json voices = json::array();
    for (const auto& t : result.audit.voices) {
      voices.push_back({{"source_index", t.source_index},
                        {"pitch", t.pitch},
                        {"pitch_hz", t.pitch_hz},
                        {"on_sample", t.rendered_on_sample},
                        {"release_sample", t.rendered_release_sample},
                        {"active_modes", t.active_modes}});
    }
    audit["voices"] = voices;
    std::ofstream(args->out / "midi_fidelity_audit.json") << audit.dump(2) << "\n";

    json res;
    res["engine_version"] = mattergraph::kEngineVersion;
    res["status"] = "succeeded";
    res["skin"] = skin.name;
    res["seed"] = args->seed;
    res["sample_rate"] = args->sample_rate;
    res["channels"] = 2;
    res["frames"] = result.stats.frames;
    res["render_time_ms"] = ms;
    res["peak"] = result.stats.peak;
    res["rms"] = result.stats.rms;
    res["peak_limited"] = result.stats.peak_limited;
    res["normalize_gain"] = result.stats.normalize_gain;
    res["midi_fidelity"] = result.audit.passed ? "passed" : "failed";
    std::ofstream(args->out / "render_result.json") << res.dump(2) << "\n";
  } catch (const std::exception& e) {
    std::cerr << "io failure: " << e.what() << "\n";
    return 5;
  }

  if (!result.audit.passed) {
    std::cerr << "MIDI fidelity audit FAILED\n";
    return 6;
  }
  std::cout << "ok: " << result.stats.frames << " frames in " << ms << " ms ("
            << skin.name << ")\n";
  return 0;
}
