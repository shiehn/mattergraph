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

enum class ExciterType { noise_burst, impulse };
enum class ReleaseMode { natural, damped };

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
  } exciter;

  struct Body {
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
