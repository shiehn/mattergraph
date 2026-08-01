#include "mattergraph/midi/clipspec.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace mattergraph::midi {
namespace {

using nlohmann::json;

constexpr std::string_view kSupportedSchemaVersion = "0.1.0";
constexpr double kMaxBpm = 1000.0;
// Guards against pathological positions that would overflow sample math long
// before any real phrase does (~10 hours at 120 bpm).
constexpr double kMaxQn = 1e7;

[[noreturn]] void fail(ClipSpecErrc errc, const std::string& message) {
  throw ClipSpecError(errc, message);
}

const json& require(const json& obj, const char* key, const std::string& where) {
  auto it = obj.find(key);
  if (it == obj.end()) {
    fail(ClipSpecErrc::missing_field, where + ": missing required field '" + key + "'");
  }
  return *it;
}

std::int32_t requireIntInRange(const json& obj, const char* key, std::int64_t lo,
                               std::int64_t hi, const std::string& where) {
  const json& v = require(obj, key, where);
  if (!v.is_number_integer()) {
    fail(ClipSpecErrc::bad_value, where + ": '" + key + "' must be an integer");
  }
  const auto n = v.get<std::int64_t>();
  if (n < lo || n > hi) {
    fail(ClipSpecErrc::bad_value, where + ": '" + key + "' = " + std::to_string(n) +
                                      " out of range [" + std::to_string(lo) + ", " +
                                      std::to_string(hi) + "]");
  }
  return static_cast<std::int32_t>(n);
}

double requireFinite(const json& obj, const char* key, const std::string& where) {
  const json& v = require(obj, key, where);
  if (!v.is_number()) {
    fail(ClipSpecErrc::bad_value, where + ": '" + key + "' must be a number");
  }
  const double d = v.get<double>();
  if (!std::isfinite(d)) {
    fail(ClipSpecErrc::bad_value, where + ": '" + key + "' must be finite");
  }
  return d;
}

}  // namespace

CanonicalTimeline buildTimelineFromClipSpecJson(std::string_view json_text,
                                                std::uint32_t sample_rate) {
  if (sample_rate == 0) {
    fail(ClipSpecErrc::bad_value, "sample_rate must be nonzero");
  }

  json root = json::parse(json_text, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    fail(ClipSpecErrc::bad_json, "ClipSpec: not a valid JSON object");
  }

  const json& version = require(root, "schema_version", "ClipSpec");
  if (!version.is_string() || version.get<std::string>() != kSupportedSchemaVersion) {
    fail(ClipSpecErrc::bad_schema_version,
         "ClipSpec: unsupported schema_version (expected \"" +
             std::string(kSupportedSchemaVersion) + "\")");
  }

  const double bpm = requireFinite(root, "bpm", "ClipSpec");
  if (bpm <= 0.0 || bpm > kMaxBpm) {
    fail(ClipSpecErrc::bad_value, "ClipSpec: bpm out of range (0, 1000]");
  }

  int ts_num = 4;
  int ts_den = 4;
  if (auto it = root.find("time_signature"); it != root.end()) {
    if (!it->is_object()) {
      fail(ClipSpecErrc::bad_value, "ClipSpec: time_signature must be an object");
    }
    ts_num = requireIntInRange(*it, "numerator", 1, 64, "time_signature");
    ts_den = requireIntInRange(*it, "denominator", 1, 64, "time_signature");
  }

  const json& notes_json = require(root, "notes", "ClipSpec");
  if (!notes_json.is_array()) {
    fail(ClipSpecErrc::bad_value, "ClipSpec: notes must be an array");
  }

  const double samples_per_qn = 60.0 / bpm * static_cast<double>(sample_rate);

  std::vector<NoteEvent> notes;
  notes.reserve(notes_json.size());
  std::int32_t index = 0;
  for (const json& n : notes_json) {
    const std::string where = "notes[" + std::to_string(index) + "]";
    if (!n.is_object()) {
      fail(ClipSpecErrc::bad_value, where + ": must be an object");
    }

    NoteEvent ev;
    ev.source_index = index;
    ev.pitch = requireIntInRange(n, "pitch", 0, 127, where);
    ev.velocity = requireIntInRange(n, "vel", 1, 127, where);
    ev.channel = n.contains("chan") ? requireIntInRange(n, "chan", 0, 15, where) : 0;

    ev.start_qn = requireFinite(n, "start_qn", where);
    if (ev.start_qn < 0.0 || ev.start_qn > kMaxQn) {
      fail(ClipSpecErrc::bad_value, where + ": start_qn out of range [0, 1e7]");
    }
    ev.dur_qn = requireFinite(n, "dur_qn", where);
    if (ev.dur_qn <= 0.0 || ev.dur_qn > kMaxQn) {
      fail(ClipSpecErrc::bad_value, where + ": dur_qn out of range (0, 1e7]");
    }

    ev.on_sample = std::llround(ev.start_qn * samples_per_qn);
    ev.off_sample = std::llround((ev.start_qn + ev.dur_qn) * samples_per_qn);
    // A note must occupy at least one sample even if rounding collapses it.
    if (ev.off_sample <= ev.on_sample) {
      ev.off_sample = ev.on_sample + 1;
    }

    notes.push_back(ev);
    ++index;
  }

  // Stable: same-sample events keep source order (plan §3.3 ordering policy).
  std::stable_sort(notes.begin(), notes.end(),
                   [](const NoteEvent& a, const NoteEvent& b) {
                     return a.on_sample < b.on_sample;
                   });

  std::int64_t total_samples = 0;
  for (const NoteEvent& ev : notes) {
    total_samples = std::max(total_samples, ev.off_sample);
  }

  return CanonicalTimeline(sample_rate, bpm, ts_num, ts_den, std::move(notes),
                           total_samples);
}

CanonicalTimeline buildTimelineFromClipSpecFile(const std::filesystem::path& path,
                                                std::uint32_t sample_rate) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail(ClipSpecErrc::io_error, "ClipSpec: cannot open file: " + path.string());
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  if (!in.good() && !in.eof()) {
    fail(ClipSpecErrc::io_error, "ClipSpec: read error: " + path.string());
  }
  return buildTimelineFromClipSpecJson(buf.str(), sample_rate);
}

}  // namespace mattergraph::midi
