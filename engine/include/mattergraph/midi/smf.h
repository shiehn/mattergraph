#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

#include "mattergraph/midi/timeline.h"

namespace mattergraph::midi {

// Standard MIDI File ingestion, formats 0 and 1, PPQ division only.
//
// Tick -> sample conversion is exact rational arithmetic (plan §3.3): sample
// positions are integers computed as round(N / D) where N accumulates
// tick_delta * usec_per_qn * sample_rate in 128-bit integers and
// D = 1'000'000 * ppq. Accumulated drift across tempo changes is zero by
// construction, not merely "below one sample".
//
// v0 scope: note on/off (velocity 0 = off), tempo map (FF 51), first time
// signature (FF 58), running status, sysex/meta skipping. Pitch bend, CCs,
// pressure, and MPE are parsed past but not yet represented — the timeline
// schema reserves them (plan rev 4 §0 change 6).
//
// Same-tick ordering policy: notes at the same tick order by track, then by
// in-track source order (documented policy per plan §3.3).

enum class SmfErrc {
  io_error,
  bad_header,
  unsupported_format,
  unsupported_smpte,
  truncated,
  bad_event,
};

class SmfError : public std::runtime_error {
 public:
  SmfError(SmfErrc errc, const std::string& message)
      : std::runtime_error(message), errc_(errc) {}
  SmfErrc code() const { return errc_; }

 private:
  SmfErrc errc_;
};

CanonicalTimeline buildTimelineFromSmfBytes(std::span<const std::uint8_t> data,
                                            std::uint32_t sample_rate);
CanonicalTimeline buildTimelineFromSmfFile(const std::filesystem::path& path,
                                           std::uint32_t sample_rate);

}  // namespace mattergraph::midi
