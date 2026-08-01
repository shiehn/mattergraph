#include "mattergraph/render/renderer.h"

#include <algorithm>
#include <cmath>

#include "mattergraph/dsp/modal_voice.h"

namespace mattergraph::render {

RenderResult renderTimeline(const midi::CanonicalTimeline& timeline,
                            const skin::SoundSkin& skin, std::uint64_t seed,
                            double normalize_peak_dbfs) {
  const auto& notes = timeline.notes();

  // Build every voice first: sizes the buffer and enforces the polyphony /
  // pitch-range contract before a single sample is produced.
  std::vector<dsp::ModalVoice> voices;
  voices.reserve(notes.size());
  std::int64_t end = timeline.totalSamples();
  for (const midi::NoteEvent& note : notes) {
    dsp::ModalVoice v(skin, note, timeline.sampleRate(), seed);
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
