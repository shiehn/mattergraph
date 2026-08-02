#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mattergraph::skin {

// SoundSkin v0: the playable realization contract for the fixed v0 topology
// (NoiseBurst/Impulse exciter -> ModalBank body -> Radiation). Schema v0.1.0.
// All normalized fields are validated to [0, 1]; times are seconds.
//
// This is the plan's SoundSkin (§2.1) at its smallest viable scope: genome
// macros + velocity mappings + release behavior + radiation + provenance seed.

enum class ExciterType { noise_burst, impulse, friction, sample, periodic };
enum class ReleaseMode { natural, damped };
// Partial-frequency law of the body. "string": stiff-string stretch
// r_k = k·sqrt(1+Bk²) (max stretch ≈ 1:2.3 — strings, bells-ish, plucks).
// "bar": struck-bar law r_k ≈ k² (marimba ≈ 1:4:9.2) — unreachable by the
// string law, which made wooden-bar prompts physically unanswerable.
enum class RatioLaw { string, bar };

struct SoundSkin {
  std::string name;
  std::uint64_t skin_seed{};  // fixes per-skin irregularity/pan streams

  struct Exciter {
    ExciterType type{ExciterType::noise_burst};
    double hardness{0.5};   // 0..1 -> burst length 30 ms .. 1.5 ms
    double color{0.5};      // 0..1 -> one-pole lowpass dark .. bright
    double level{0.8};      // 0..1 excitation energy
    double noisiness{0.35}; // 0..1 noise-texture fraction of the strike.
                            // The pulse core is deterministic so that loudness
                            // is a function of velocity, not of noise luck
                            // (randomness boundary, plan §3.5).
    // friction-type only: sustained excitation for the note's full duration.
    double roughness{0.5};      // 0..1 stick-slip grit depth
    double grit_rate_hz{90.0};  // 5..400 nominal slip-grain rate
    // sample-type only: a bank transient excites the body. The engine stays
    // IO-free at voice level — the render layer loads the PCM and passes it in.
    std::string sample;         // bank filename (resolved against --exciter-dir)
    double sample_blend{0.85};  // 0..1 sample vs synthetic-strike mix
    // periodic-type only: band-limited tonal excitation at the note's pitch.
    double wave{0.0};           // 0 saw .. 1 square (polyBLEP band-limited)
    double detune_cents{8.0};   // 0..30 spread of the three voices
    double drive{0.3};          // 0..1 saturation into the body
  } exciter;

  struct Body {
    RatioLaw ratio_law{RatioLaw::string};
    int mode_count{16};        // 1..256 (pre-Nyquist pruning may reduce it)
    double inharmonicity{0.2}; // 0..1 -> stiff-string stretch B = inh^2 * 0.15
    double brightness{0.5};    // 0..1 amplitude tilt across partials
    double t60_base_s{1.0};    // decay time of the fundamental, 0.01..30 s
    double damping_slope{0.5}; // 0..2: higher partials decay faster
    double irregularity{0.1};  // 0..1 deterministic per-skin freq/amp jitter
    double position{0.35};     // 0..1 strike position (comb weighting)
  } body;

  struct Velocity {
    double to_level{0.8};      // 0..1 velocity -> energy sensitivity
    double to_brightness{0.4}; // 0..1 velocity -> exciter color shift
    double to_hardness{0.3};   // 0..1 velocity -> attack hardness shift
  } velocity;

  struct Release {
    ReleaseMode mode{ReleaseMode::damped};
    double damp_factor{6.0};   // t60 divided by this at note-off, 1..100
  } release;

  struct Radiation {
    double stereo_spread{0.5}; // 0..1 per-mode pan spread
    double gain{0.35};         // 0..2 master gain before safety stage
  } radiation;
};

class SoundSkinError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

SoundSkin loadSoundSkinFromJson(std::string_view json_text);
SoundSkin loadSoundSkinFromFile(const std::filesystem::path& path);

}  // namespace mattergraph::skin
