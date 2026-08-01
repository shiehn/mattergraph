#pragma once

#include <cstdint>
#include <vector>

namespace mattergraph::midi {

// One note in the immutable performance timeline. Sample positions are exact:
// they are the authoritative schedule the renderer must honor (plan §3.3).
struct NoteEvent {
  std::int32_t pitch{};         // MIDI note number, 0-127
  std::int32_t channel{};       // 0-15
  std::int32_t velocity{};      // 1-127
  std::int64_t on_sample{};     // absolute sample position of note-on
  std::int64_t off_sample{};    // absolute sample position of note-off; > on_sample
  double start_qn{};            // source position in quarter notes (provenance)
  double dur_qn{};              // source duration in quarter notes (provenance)
  std::int32_t source_index{};  // index in the source document, for audits

  friend bool operator==(const NoteEvent&, const NoteEvent&) = default;
};

// Immutable canonical performance timeline (plan §3.1). Built once by a builder
// (clipspec.h now, smf.h later); read-only afterwards — no mutating accessors
// exist, and none may be added. Sound generation consumes it, never rewrites it.
class CanonicalTimeline {
 public:
  CanonicalTimeline(std::uint32_t sample_rate, double bpm, int ts_num, int ts_den,
                    std::vector<NoteEvent> notes, std::int64_t total_samples)
      : sample_rate_(sample_rate), bpm_(bpm), ts_num_(ts_num), ts_den_(ts_den),
        notes_(std::move(notes)), total_samples_(total_samples) {}

  std::uint32_t sampleRate() const { return sample_rate_; }
  double bpm() const { return bpm_; }
  int timeSigNumerator() const { return ts_num_; }
  int timeSigDenominator() const { return ts_den_; }
  // Sorted by on_sample; same-sample events keep source order (plan §3.3).
  const std::vector<NoteEvent>& notes() const { return notes_; }
  // Last note-off position; release tails beyond this are a render concern.
  std::int64_t totalSamples() const { return total_samples_; }

  friend bool operator==(const CanonicalTimeline&, const CanonicalTimeline&) = default;

 private:
  std::uint32_t sample_rate_;
  double bpm_;
  int ts_num_;
  int ts_den_;
  std::vector<NoteEvent> notes_;
  std::int64_t total_samples_;
};

}  // namespace mattergraph::midi
