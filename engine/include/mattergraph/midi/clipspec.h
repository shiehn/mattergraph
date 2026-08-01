#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mattergraph/midi/timeline.h"

namespace mattergraph::midi {

// ClipSpec is Signals & Sorcery's wire format for generated MIDI: quarter-note
// positions at a fixed tempo. Schema v0.1.0:
//   {
//     "schema_version": "0.1.0",
//     "bpm": 120.0,
//     "time_signature": {"numerator": 4, "denominator": 4},   // optional, default 4/4
//     "notes": [
//       {"pitch": 36, "start_qn": 0.0, "dur_qn": 0.5, "vel": 100, "chan": 0}
//     ]
//   }
// "chan" is optional (default 0). Unknown fields are ignored for forward
// compatibility; a missing or unsupported schema_version is an error.
//
// Conversion is fixed-tempo: on_sample = llround(start_qn * 60/bpm * sample_rate).
// This is deterministic (IEEE-754 double, no accumulation, absolute positions).
// The SMF builder, which must handle tempo maps, will use integer tick
// arithmetic instead (plan §3.3); ClipSpec has no tempo changes by construction.

enum class ClipSpecErrc {
  io_error,
  bad_json,
  missing_field,
  bad_schema_version,
  bad_value,
};

class ClipSpecError : public std::runtime_error {
 public:
  ClipSpecError(ClipSpecErrc errc, const std::string& message)
      : std::runtime_error(message), errc_(errc) {}
  ClipSpecErrc code() const { return errc_; }

 private:
  ClipSpecErrc errc_;
};

// Both builders validate every field and throw ClipSpecError on violations.
CanonicalTimeline buildTimelineFromClipSpecJson(std::string_view json_text,
                                                std::uint32_t sample_rate);
CanonicalTimeline buildTimelineFromClipSpecFile(const std::filesystem::path& path,
                                                std::uint32_t sample_rate);

}  // namespace mattergraph::midi
