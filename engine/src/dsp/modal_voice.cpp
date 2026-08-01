#include "mattergraph/dsp/modal_voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "mattergraph/dsp/rng.h"

namespace mattergraph::dsp {
namespace {

constexpr double kNyquistFraction = 0.45;
constexpr double kMaxTailSeconds = 12.0;
constexpr double kTinyT60 = 0.02;

double mix(double a, double b, double t) { return a + (b - a) * t; }

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// Per-sample amplitude decay factor for a -60 dB time of t60 seconds.
double decayPerSample(double t60_s, double sample_rate) {
  return std::pow(10.0, -3.0 / (std::max(t60_s, kTinyT60) * sample_rate));
}

// Scale a signal to unit energy (sum of squares == 1).
void normalizeEnergy(std::vector<double>& x) {
  double sum_sq = 0.0;
  for (double s : x) {
    sum_sq += s * s;
  }
  if (sum_sq > 0.0) {
    const double inv = 1.0 / std::sqrt(sum_sq);
    for (double& s : x) {
      s *= inv;
    }
  }
}

}  // namespace

ModalVoice::ModalVoice(const skin::SoundSkin& skin, const midi::NoteEvent& note,
                       std::uint32_t sample_rate, std::uint64_t render_seed) {
  const double sr = static_cast<double>(sample_rate);
  on_sample_ = note.on_sample;
  off_sample_ = note.off_sample;

  // Exact equal-temperament pitch (plan §3.2). Tuning tables come later; the
  // formula and the A4 reference are the contract today.
  f0_ = 440.0 * std::exp2((static_cast<double>(note.pitch) - 69.0) / 12.0);

  const double v = static_cast<double>(note.velocity) / 127.0;
  const double color_v =
      clamp01(skin.exciter.color + skin.velocity.to_brightness * (v - 0.5));
  const double hardness_v =
      clamp01(skin.exciter.hardness + skin.velocity.to_hardness * (v - 0.5));
  // to_level = 0 -> velocity-insensitive; 1 -> full v^1.5 energy curve.
  const double energy =
      skin.exciter.level * mix(1.0, std::pow(v, 1.5), skin.velocity.to_level);

  // --- Mode table: pure function of (skin, pitch). ---
  // Stiff-string partial stretch: r_k = k * sqrt(1 + B k^2).
  const double B = skin.body.inharmonicity * skin.body.inharmonicity * 0.15;
  const double tilt_exponent = 1.9 - 1.5 * skin.body.brightness;
  const double release_factor =
      skin.release.mode == skin::ReleaseMode::damped ? skin.release.damp_factor : 1.0;

  double t60_release_max = 0.0;
  double amp_sum = 0.0;
  modes_.reserve(static_cast<std::size_t>(skin.body.mode_count));
  for (int k = 1; k <= skin.body.mode_count; ++k) {
    const double kd = static_cast<double>(k);
    Rng jitter(deriveStream(skin.skin_seed, static_cast<std::uint64_t>(k)));
    const double freq_jitter = 1.0 + skin.body.irregularity * 0.05 * jitter.bipolar();
    const double amp_jitter = 1.0 + skin.body.irregularity * 0.5 * jitter.bipolar();
    const double pan_jitter = jitter.bipolar();

    const double ratio = kd * std::sqrt(1.0 + B * kd * kd) * freq_jitter;
    const double f = f0_ * ratio;
    if (f >= sr * kNyquistFraction) {
      continue;
    }

    Mode m;
    const double w = 2.0 * std::numbers::pi * f / sr;
    m.cos_w = std::cos(w);
    m.sin_w = std::sin(w);

    const double t60 =
        skin.body.t60_base_s * std::pow(ratio, -skin.body.damping_slope);
    m.g_on = decayPerSample(t60, sr);
    m.g_off = decayPerSample(t60 / release_factor, sr);
    t60_release_max = std::max(t60_release_max, t60 / release_factor);

    // Brightness tilt x strike-position comb x irregularity.
    const double comb =
        std::abs(std::sin(std::numbers::pi * kd * mix(0.05, 0.5, skin.body.position)));
    m.amp = std::pow(ratio, -tilt_exponent) * (0.25 + 0.75 * comb) *
            std::max(0.05, amp_jitter);
    amp_sum += m.amp;

    const double pan = 0.5 + 0.5 * skin.radiation.stereo_spread * pan_jitter;
    m.pan_l = std::cos(pan * std::numbers::pi / 2.0);
    m.pan_r = std::sin(pan * std::numbers::pi / 2.0);

    modes_.push_back(m);
  }
  if (amp_sum > 0.0) {
    for (Mode& m : modes_) {
      m.amp /= amp_sum;
    }
  }

  if (skin.exciter.type == skin::ExciterType::friction) {
    // --- Sustained friction excitation: the first non-strike gesture. ---
    // Colored noise drives the body for the note's whole duration, amplitude-
    // shaped by a stick-slip grain train. Normalized to unit RMS (power), not
    // total energy — a longer bow stroke must not get quieter (plan §3.5:
    // loudness is a function of velocity, not duration or noise luck).
    constexpr double kMaxDriveSeconds = 30.0;
    const auto drive_n = static_cast<std::size_t>(std::min(
        static_cast<double>(off_sample_ - on_sample_), kMaxDriveSeconds * sr));
    burst_.assign(std::max<std::size_t>(drive_n, 2), 0.0);

    Rng noise(deriveStream(render_seed,
                           0x46726963ULL ^ static_cast<std::uint64_t>(note.source_index)));
    const double lp_a = mix(0.03, 0.9, std::pow(color_v, 1.5));
    const double attack_s = mix(0.25, 0.01, hardness_v);
    const auto attack_n = std::max<std::size_t>(
        static_cast<std::size_t>(attack_s * sr), 1);
    const double depth = 0.85 * skin.exciter.roughness;
    double lp = 0.0;
    double phase = 0.0;
    double walk = 0.0;
    for (std::size_t n = 0; n < burst_.size(); ++n) {
      lp += lp_a * (noise.bipolar() - lp);
      walk = std::clamp(walk + 0.002 * noise.bipolar(), -1.0, 1.0);
      phase += skin.exciter.grit_rate_hz * (1.0 + 0.7 * skin.exciter.roughness * walk) / sr;
      phase -= std::floor(phase);
      const double grain = std::pow(0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phase)), 1.5);
      const double env = n < attack_n
                             ? 0.5 * (1.0 - std::cos(std::numbers::pi *
                                                     static_cast<double>(n) /
                                                     static_cast<double>(attack_n)))
                             : 1.0;
      burst_[n] = lp * env * ((1.0 - depth) + depth * grain);
    }
    double sum_sq = 0.0;
    for (double s : burst_) {
      sum_sq += s * s;
    }
    const double rms = std::sqrt(sum_sq / static_cast<double>(burst_.size()));
    // Steady-state amplitude of a continuously driven resonator grows with its
    // decay time (∝ τ), so the drive compensates by 1/t60 — otherwise long-
    // ringing bodies clip and short ones whisper. Constant calibrated so
    // anchor-class bodies peak around -7 dBFS.
    const double drive = 0.04 * energy /
                         ((0.3 + skin.body.t60_base_s) * std::max(rms, 1e-9));
    for (double& s : burst_) {
      s *= drive;
    }

    const double tail_s = std::min(t60_release_max + 0.05, kMaxTailSeconds);
    end_sample_ = off_sample_ + static_cast<std::int64_t>(tail_s * sr) + 1;
    gain_ = skin.radiation.gain;
    return;
  }

  // --- Exciter: deterministic pulse core + noise texture. ---
  // Loudness must be a function of velocity, not of noise luck (plan §3.5:
  // randomness changes microtexture only). The pulse carries the energy; the
  // noise fraction adds texture; the whole strike is normalized to unit energy
  // and then scaled by the deterministic velocity energy curve.
  const double burst_s = skin.exciter.type == skin::ExciterType::impulse
                             ? 0.0005
                             : mix(0.030, 0.0015, hardness_v);
  const auto burst_n = static_cast<std::size_t>(std::max(2.0, burst_s * sr));
  const double lp_a = mix(0.03, 0.9, std::pow(color_v, 1.5));

  std::vector<double> pulse(burst_n);
  {
    // Raised-cosine push through the same color lowpass as the noise.
    double lp = 0.0;
    for (std::size_t n = 0; n < burst_n; ++n) {
      const double ph = static_cast<double>(n) / static_cast<double>(burst_n);
      const double raw = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * ph));
      lp += lp_a * (raw - lp);
      pulse[n] = lp;
    }
  }

  burst_.assign(burst_n, 0.0);
  const double noisiness =
      skin.exciter.type == skin::ExciterType::impulse ? 0.0 : skin.exciter.noisiness;
  if (noisiness > 0.0) {
    Rng noise(deriveStream(render_seed,
                           0x4E6F7465ULL ^ static_cast<std::uint64_t>(note.source_index)));
    const double tau = std::max(burst_s / 3.0, 1e-4) * sr;
    double lp = 0.0;
    for (std::size_t n = 0; n < burst_n; ++n) {
      lp += lp_a * (noise.bipolar() - lp);
      burst_[n] = lp * std::exp(-static_cast<double>(n) / tau);
    }
    normalizeEnergy(burst_);
  }
  normalizeEnergy(pulse);
  for (std::size_t n = 0; n < burst_n; ++n) {
    burst_[n] = (1.0 - noisiness) * pulse[n] + noisiness * burst_[n];
  }
  normalizeEnergy(burst_);
  for (double& s : burst_) {
    s *= energy;
  }

  const double tail_s = std::min(t60_release_max + 0.05, kMaxTailSeconds);
  end_sample_ = off_sample_ + static_cast<std::int64_t>(tail_s * sr) + 1;
  gain_ = skin.radiation.gain;
}

void ModalVoice::renderInto(std::vector<double>& left, std::vector<double>& right) {
  const std::int64_t total = end_sample_ - on_sample_;
  const std::int64_t release_at = off_sample_ - on_sample_;
  const auto burst_n = static_cast<std::int64_t>(burst_.size());

  for (std::int64_t n = 0; n < total; ++n) {
    const double in = n < burst_n ? burst_[static_cast<std::size_t>(n)] : 0.0;
    const bool released = n >= release_at;
    double out_l = 0.0;
    double out_r = 0.0;
    for (Mode& m : modes_) {
      const double g = released ? m.g_off : m.g_on;
      const double x = g * (m.x * m.cos_w - m.y * m.sin_w) + in * m.amp;
      const double y = g * (m.x * m.sin_w + m.y * m.cos_w);
      m.x = x;
      m.y = y;
      out_l += y * m.pan_l;
      out_r += y * m.pan_r;
    }
    const auto pos = static_cast<std::size_t>(on_sample_ + n);
    left[pos] += out_l * gain_;
    right[pos] += out_r * gain_;
  }
}

}  // namespace mattergraph::dsp
