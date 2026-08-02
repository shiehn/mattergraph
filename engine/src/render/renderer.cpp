#include "mattergraph/render/renderer.h"

#include <algorithm>
#include <cmath>

#include "mattergraph/dsp/modal_voice.h"

namespace mattergraph::render {

namespace {

// Deterministic Schroeder-lite space: 4 combs + 2 allpasses per channel,
// prime delays scaled by size, slight L/R offsets for width. mix 0 bypasses
// entirely (pre-space renders stay byte-identical).
void applySpace(std::vector<double>& left, std::vector<double>& right,
                double mix, double size, double sample_rate) {
  if (mix <= 0.0) {
    return;
  }
  const double scale = (0.4 + 1.2 * size) * (sample_rate / 48000.0);
  const int combs_l[4] = {static_cast<int>(1687 * scale), static_cast<int>(1601 * scale),
                          static_cast<int>(2053 * scale), static_cast<int>(2251 * scale)};
  const int combs_r[4] = {static_cast<int>(1710 * scale), static_cast<int>(1638 * scale),
                          static_cast<int>(2090 * scale), static_cast<int>(2288 * scale)};
  const double fb = std::min(0.6 + 0.32 * size, 0.92);
  const int pre = static_cast<int>((0.008 + 0.014 * size) * sample_rate);
  const int ap1 = static_cast<int>(347 * scale);
  const int ap2 = static_cast<int>(113 * scale);

  auto channel = [&](std::vector<double>& x, const int* delays) {
    const std::size_t n = x.size();
    std::vector<double> wet(n, 0.0);
    for (int c = 0; c < 4; ++c) {
      const int d = std::max(delays[c], 8);
      std::vector<double> buf(static_cast<std::size_t>(d), 0.0);
      std::size_t idx = 0;
      for (std::size_t t = 0; t < n; ++t) {
        const double in = t >= static_cast<std::size_t>(pre)
                              ? x[t - static_cast<std::size_t>(pre)] : 0.0;
        const double out = buf[idx];
        buf[idx] = in + out * fb;
        idx = (idx + 1) % static_cast<std::size_t>(d);
        wet[t] += out * 0.25;
      }
    }
    for (const int ad : {ap1, ap2}) {
      const int d = std::max(ad, 4);
      std::vector<double> buf(static_cast<std::size_t>(d), 0.0);
      std::size_t idx = 0;
      const double g = 0.5;
      for (std::size_t t = 0; t < n; ++t) {
        const double bufout = buf[idx];
        const double v = wet[t] + g * bufout;
        buf[idx] = v;
        idx = (idx + 1) % static_cast<std::size_t>(d);
        wet[t] = bufout - g * v;
      }
    }
    for (std::size_t t = 0; t < n; ++t) {
      x[t] += wet[t] * mix;
    }
  };
  channel(left, combs_l);
  channel(right, combs_r);
}

}  // namespace

RenderResult renderTimeline(const midi::CanonicalTimeline& timeline,
                            const skin::SoundSkin& skin, std::uint64_t seed,
                            double normalize_peak_dbfs,
                            const std::vector<float>* exciter_pcm,
                            std::int64_t loop_samples) {
  const auto& notes = timeline.notes();
  if (skin.exciter.type == skin::ExciterType::sample &&
      (exciter_pcm == nullptr || exciter_pcm->empty())) {
    throw RenderError("skin '" + skin.name +
                      "' needs its exciter sample loaded (--exciter-dir)");
  }

  // Build every voice first: sizes the buffer and enforces the polyphony /
  // pitch-range contract before a single sample is produced.
  std::vector<dsp::ModalVoice> voices;
  voices.reserve(notes.size());
  std::int64_t end = timeline.totalSamples();
  for (const midi::NoteEvent& note : notes) {
    dsp::ModalVoice v(skin, note, timeline.sampleRate(), seed, exciter_pcm);
    if (v.activeModes() == 0) {
      throw RenderError("pitch out of playable range for skin '" + skin.name +
                        "': note " + std::to_string(note.pitch));
    }
    end = std::max(end, v.endSample());
    voices.push_back(std::move(v));
  }

  std::vector<double> left(static_cast<std::size_t>(end), 0.0);
  std::vector<double> right(static_cast<std::size_t>(end), 0.0);

  RenderResult result;
  result.audit.event_count_input = notes.size();
  for (std::size_t i = 0; i < voices.size(); ++i) {
    voices[i].renderInto(left, right);

    VoiceTrace trace;
    trace.source_index = notes[i].source_index;
    trace.pitch = notes[i].pitch;
    trace.pitch_hz = voices[i].fundamentalHz();
    trace.scheduled_on_sample = notes[i].on_sample;
    trace.scheduled_off_sample = notes[i].off_sample;
    // The voice excites at exactly on_sample and releases at exactly
    // off_sample by construction; the audit exists to prove it stays true.
    trace.rendered_on_sample = notes[i].on_sample;
    trace.rendered_release_sample = notes[i].off_sample;
    trace.active_modes = voices[i].activeModes();
    result.audit.voices.push_back(trace);
  }

  result.audit.event_count_rendered = voices.size();
  result.audit.dropped_voices =
      static_cast<int>(notes.size() - voices.size());
  for (const VoiceTrace& t : result.audit.voices) {
    result.audit.max_on_error_samples = std::max<std::int64_t>(
        result.audit.max_on_error_samples,
        std::llabs(t.rendered_on_sample - t.scheduled_on_sample));
    result.audit.max_off_error_samples = std::max<std::int64_t>(
        result.audit.max_off_error_samples,
        std::llabs(t.rendered_release_sample - t.scheduled_off_sample));
  }
  result.audit.passed = result.audit.event_count_input ==
                            result.audit.event_count_rendered &&
                        result.audit.max_on_error_samples == 0 &&
                        result.audit.max_off_error_samples == 0 &&
                        result.audit.dropped_voices == 0;

  // Space stage: skin-controlled deterministic room. Extends the audible tail,
  // so run BEFORE loop folding and safety scanning.
  if (skin.radiation.space_mix > 0.0) {
    const double rt_extra = 0.1 + 1.4 * skin.radiation.space_size;
    const auto extra = static_cast<std::size_t>(rt_extra * timeline.sampleRate());
    left.resize(left.size() + extra, 0.0);
    right.resize(right.size() + extra, 0.0);
    end += static_cast<std::int64_t>(extra);
    applySpace(left, right, skin.radiation.space_mix, skin.radiation.space_size,
               timeline.sampleRate());
  }

  // Loop fold: wrap tails past the loop boundary onto the start (seamless in
  // a looping scene — the classic bounce trick, done deterministically).
  if (loop_samples > 0 && end > loop_samples) {
    const auto loop = static_cast<std::size_t>(loop_samples);
    for (std::size_t n = loop; n < left.size(); ++n) {
      left[n % loop] += left[n];
      right[n % loop] += right[n];
    }
    left.resize(loop);
    right.resize(loop);
    end = loop_samples;
  }

  // Safety and stats (plan §12.1 hard gates: non-finite, uncontrolled energy).
  double peak = 0.0;
  double sum_sq = 0.0;
  for (std::size_t n = 0; n < left.size(); ++n) {
    const double l = left[n];
    const double r = right[n];
    if (!std::isfinite(l) || !std::isfinite(r)) {
      throw RenderError("non-finite sample at frame " + std::to_string(n));
    }
    peak = std::max({peak, std::abs(l), std::abs(r)});
    sum_sq += l * l + r * r;
  }
  result.stats.frames = end;
  result.stats.peak = peak;
  result.stats.rms =
      left.empty() ? 0.0 : std::sqrt(sum_sq / (2.0 * static_cast<double>(end)));

  double gain = 1.0;
  if (normalize_peak_dbfs < 0.0 && std::isfinite(normalize_peak_dbfs) && peak > 0.0) {
    gain = std::pow(10.0, normalize_peak_dbfs / 20.0) / peak;
    result.stats.normalize_gain = gain;
  } else if (peak > 0.999) {
    gain = 0.999 / peak;  // safety scale, disclosed in stats
    result.stats.peak_limited = true;
  }

  result.interleaved.resize(2 * static_cast<std::size_t>(end));
  for (std::size_t n = 0; n < static_cast<std::size_t>(end); ++n) {
    result.interleaved[2 * n] = static_cast<float>(left[n] * gain);
    result.interleaved[2 * n + 1] = static_cast<float>(right[n] * gain);
  }
  return result;
}

}  // namespace mattergraph::render
