#include "mattergraph/skin/soundskin.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace mattergraph::skin {
namespace {

using nlohmann::json;

constexpr std::string_view kSupportedSchemaVersion = "0.1.0";

[[noreturn]] void fail(const std::string& message) { throw SoundSkinError(message); }

const json& require(const json& obj, const char* key, const std::string& where) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    fail(where + ": missing required field '" + key + "'");
  }
  return *it;
}

double numInRange(const json& obj, const char* key, double lo, double hi,
                  double fallback, const std::string& where) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    return fallback;
  }
  if (!it->is_number()) {
    fail(where + ": '" + key + "' must be a number");
  }
  const double d = it->get<double>();
  if (!std::isfinite(d) || d < lo || d > hi) {
    fail(where + ": '" + key + "' out of range [" + std::to_string(lo) + ", " +
         std::to_string(hi) + "]");
  }
  return d;
}

}  // namespace

SoundSkin loadSoundSkinFromJson(std::string_view json_text) {
  json root = json::parse(json_text, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    fail("SoundSkin: not a valid JSON object");
  }

  const json& version = require(root, "schema_version", "SoundSkin");
  if (!version.is_string() || version.get<std::string>() != kSupportedSchemaVersion) {
    fail("SoundSkin: unsupported schema_version (expected \"" +
         std::string(kSupportedSchemaVersion) + "\")");
  }

  SoundSkin s;
  const json& name = require(root, "name", "SoundSkin");
  if (!name.is_string() || name.get<std::string>().empty()) {
    fail("SoundSkin: 'name' must be a nonempty string");
  }
  s.name = name.get<std::string>();

  {
    const json& seed = require(root, "skin_seed", "SoundSkin");
    if (!seed.is_number_unsigned() && !seed.is_number_integer()) {
      fail("SoundSkin: 'skin_seed' must be an integer");
    }
    s.skin_seed = seed.get<std::uint64_t>();
  }

  if (auto it = root.find("exciter"); it != root.end()) {
    const json& e = *it;
    if (auto t = e.find("type"); t != e.end()) {
      const std::string type = t->get<std::string>();
      if (type == "noise_burst") {
        s.exciter.type = ExciterType::noise_burst;
      } else if (type == "impulse") {
        s.exciter.type = ExciterType::impulse;
      } else if (type == "friction") {
        s.exciter.type = ExciterType::friction;
      } else if (type == "sample") {
        s.exciter.type = ExciterType::sample;
      } else if (type == "periodic") {
        s.exciter.type = ExciterType::periodic;
      } else if (type == "pluck_string") {
        s.exciter.type = ExciterType::pluck_string;
      } else if (type == "breath") {
        s.exciter.type = ExciterType::breath;
      } else if (type == "brass") {
        s.exciter.type = ExciterType::brass;
      } else if (type == "wavetable") {
        s.exciter.type = ExciterType::wavetable;
      } else {
        fail("SoundSkin: exciter.type must be one of 'noise_burst', 'impulse', "
             "'friction', 'sample', 'periodic', 'pluck_string', 'breath', "
             "'brass', 'wavetable'");
      }
    }
    s.exciter.hardness = numInRange(e, "hardness", 0, 1, s.exciter.hardness, "exciter");
    s.exciter.color = numInRange(e, "color", 0, 1, s.exciter.color, "exciter");
    s.exciter.level = numInRange(e, "level", 0, 1, s.exciter.level, "exciter");
    s.exciter.noisiness = numInRange(e, "noisiness", 0, 1, s.exciter.noisiness, "exciter");
    s.exciter.roughness = numInRange(e, "roughness", 0, 1, s.exciter.roughness, "exciter");
    s.exciter.grit_rate_hz =
        numInRange(e, "grit_rate_hz", 5, 400, s.exciter.grit_rate_hz, "exciter");
    if (auto sm = e.find("sample"); sm != e.end()) {
      if (!sm->is_string()) {
        fail("SoundSkin: exciter.sample must be a string filename");
      }
      s.exciter.sample = sm->get<std::string>();
    }
    s.exciter.sample_blend =
        numInRange(e, "sample_blend", 0, 1, s.exciter.sample_blend, "exciter");
    s.exciter.wave = numInRange(e, "wave", 0, 1, s.exciter.wave, "exciter");
    s.exciter.detune_cents =
        numInRange(e, "detune_cents", 0, 30, s.exciter.detune_cents, "exciter");
    s.exciter.drive = numInRange(e, "drive", 0, 1, s.exciter.drive, "exciter");
    if (auto wt = e.find("wavetable"); wt != e.end()) {
      if (!wt->is_string()) {
        fail("SoundSkin: exciter.wavetable must be a string filename");
      }
      s.exciter.wavetable = wt->get<std::string>();
    }
    if (s.exciter.type == ExciterType::sample && s.exciter.sample.empty()) {
      fail("SoundSkin: exciter.type 'sample' requires exciter.sample");
    }
    if (s.exciter.type == ExciterType::wavetable && s.exciter.wavetable.empty()) {
      fail("SoundSkin: exciter.type 'wavetable' requires exciter.wavetable");
    }
  }

  if (auto it = root.find("body"); it != root.end()) {
    const json& b = *it;
    if (auto rl = b.find("ratio_law"); rl != b.end()) {
      const std::string law = rl->get<std::string>();
      if (law == "string") {
        s.body.ratio_law = RatioLaw::string;
      } else if (law == "bar") {
        s.body.ratio_law = RatioLaw::bar;
      } else {
        fail("SoundSkin: body.ratio_law must be 'string' or 'bar'");
      }
    }
    if (auto mc = b.find("mode_count"); mc != b.end()) {
      if (!mc->is_number_integer()) {
        fail("SoundSkin: body.mode_count must be an integer");
      }
      const auto n = mc->get<std::int64_t>();
      if (n < 1 || n > 256) {
        fail("SoundSkin: body.mode_count out of range [1, 256]");
      }
      s.body.mode_count = static_cast<int>(n);
    }
    s.body.inharmonicity = numInRange(b, "inharmonicity", 0, 1, s.body.inharmonicity, "body");
    s.body.brightness = numInRange(b, "brightness", 0, 1, s.body.brightness, "body");
    s.body.t60_base_s = numInRange(b, "t60_base_s", 0.01, 30, s.body.t60_base_s, "body");
    s.body.damping_slope = numInRange(b, "damping_slope", 0, 2, s.body.damping_slope, "body");
    s.body.irregularity = numInRange(b, "irregularity", 0, 1, s.body.irregularity, "body");
    s.body.position = numInRange(b, "position", 0, 1, s.body.position, "body");
  }

  if (auto it = root.find("velocity"); it != root.end()) {
    const json& v = *it;
    s.velocity.to_level = numInRange(v, "to_level", 0, 1, s.velocity.to_level, "velocity");
    s.velocity.to_brightness =
        numInRange(v, "to_brightness", 0, 1, s.velocity.to_brightness, "velocity");
    s.velocity.to_hardness =
        numInRange(v, "to_hardness", 0, 1, s.velocity.to_hardness, "velocity");
  }

  if (auto it = root.find("release"); it != root.end()) {
    const json& r = *it;
    if (auto m = r.find("mode"); m != r.end()) {
      const std::string mode = m->get<std::string>();
      if (mode == "natural") {
        s.release.mode = ReleaseMode::natural;
      } else if (mode == "damped") {
        s.release.mode = ReleaseMode::damped;
      } else {
        fail("SoundSkin: release.mode must be 'natural' or 'damped'");
      }
    }
    s.release.damp_factor = numInRange(r, "damp_factor", 1, 100, s.release.damp_factor, "release");
  }

  if (auto it = root.find("radiation"); it != root.end()) {
    const json& r = *it;
    s.radiation.stereo_spread =
        numInRange(r, "stereo_spread", 0, 1, s.radiation.stereo_spread, "radiation");
    s.radiation.gain = numInRange(r, "gain", 0, 2, s.radiation.gain, "radiation");
    s.radiation.space_mix =
        numInRange(r, "space_mix", 0, 1, s.radiation.space_mix, "radiation");
    s.radiation.space_size =
        numInRange(r, "space_size", 0, 1, s.radiation.space_size, "radiation");
    s.radiation.chorus = numInRange(r, "chorus", 0, 1, s.radiation.chorus, "radiation");
    s.radiation.sat = numInRange(r, "sat", 0, 1, s.radiation.sat, "radiation");
    s.radiation.motion = numInRange(r, "motion", 0, 1, s.radiation.motion, "radiation");
  }

  return s;
}

SoundSkin loadSoundSkinFromFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail("SoundSkin: cannot open file: " + path.string());
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return loadSoundSkinFromJson(buf.str());
}

}  // namespace mattergraph::skin
