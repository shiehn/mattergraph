#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "mattergraph/midi/timeline.h"
#include "mattergraph/skin/soundskin.h"

namespace mattergraph::render {

// Per-note record proving what the engine actually scheduled (plan §3.6).
struct VoiceTrace {
  std::int32_t source_index{};
  std::int32_t pitch{};
  double pitch_hz{};
  std::int64_t scheduled_on_sample{};
  std::int64_t scheduled_off_sample{};
  std::int64_t rendered_on_sample{};
  std::int64_t rendered_release_sample{};
  int active_modes{};
};

struct FidelityAudit {
  bool passed{};
  std::size_t event_count_input{};
  std::size_t event_count_rendered{};
  std::int64_t max_on_error_samples{};
  std::int64_t max_off_error_samples{};
  int dropped_voices{};
  std::vector<VoiceTrace> voices;
};

struct RenderStats {
  std::int64_t frames{};
  double peak{};          // absolute, post-gain, pre-normalize
  double rms{};
  bool peak_limited{};    // true if safety scaling was applied
  double normalize_gain{1.0};
};

struct RenderResult {
  // Interleaved stereo f32, ready for the WAV writer.
  std::vector<float> interleaved;
  RenderStats stats;
  FidelityAudit audit;
};

class RenderError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Offline deterministic render: one voice per note (no stealing, plan §3.4),
// voices accumulated in timeline order (fixed summation order = reproducible).
// Throws RenderError on out-of-range pitch (zero active modes) or non-finite
// output. If normalize_peak_dbfs is negative-finite, scales the final mix so
// the peak sits at that level and records the gain in stats.
RenderResult renderTimeline(const midi::CanonicalTimeline& timeline,
                            const skin::SoundSkin& skin, std::uint64_t seed,
                            double normalize_peak_dbfs = 0.0);

}  // namespace mattergraph::render
