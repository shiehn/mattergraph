#pragma once

#include <cstdint>
#include <vector>

#include "mattergraph/midi/timeline.h"
#include "mattergraph/skin/soundskin.h"

namespace mattergraph::dsp {

// One note rendered by the v0 topology: exciter -> modal bank -> radiation.
//
// Determinism: the mode table is a pure function of (skin, pitch); the noise
// realization is a pure function of (render seed, note source_index). Rendering
// the same voice twice produces identical samples.
//
// Pitch contract (plan §3.2): the fundamental is exactly
// 440 * 2^((pitch - 69) / 12); every partial is placed relative to it.
class ModalVoice {
 public:
  // exciter_pcm: required iff the skin's exciter type is `sample` — the render
  // layer loads the bank asset once and shares it across voices (the voice
  // itself never touches the filesystem).
  ModalVoice(const skin::SoundSkin& skin, const midi::NoteEvent& note,
             std::uint32_t sample_rate, std::uint64_t render_seed,
             const std::vector<float>* exciter_pcm = nullptr);

  // Number of modes that survived Nyquist pruning; 0 means the note's pitch is
  // out of the skin's playable range and the render must be rejected (§3.4).
  int activeModes() const { return static_cast<int>(modes_.size()); }

  double fundamentalHz() const { return f0_; }

  // Sample position (absolute) after which this voice is inaudible.
  std::int64_t endSample() const { return end_sample_; }

  // Accumulate this voice into absolute-position stereo buffers, which must be
  // at least endSample() long. Consumes the voice state; call once.
  void renderInto(std::vector<double>& left, std::vector<double>& right);

 private:
  struct Mode {
    double x{}, y{};        // resonator state (quadrature pair)
    double cos_w{}, sin_w{};// rotation for exact partial frequency
    double g_on{}, g_off{}; // per-sample decay before/after note-off
    double amp{};           // excitation weight
    double pan_l{}, pan_r{};
  };

  std::vector<Mode> modes_;
  std::vector<double> burst_;   // precomputed exciter signal
  std::int64_t on_sample_{};
  std::int64_t off_sample_{};
  std::int64_t end_sample_{};
  double f0_{};
  double gain_{};
};

}  // namespace mattergraph::dsp
