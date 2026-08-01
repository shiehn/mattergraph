#include "mattergraph/midi/smf.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <vector>

namespace mattergraph::midi {
namespace {

[[noreturn]] void fail(SmfErrc errc, const std::string& message) {
  throw SmfError(errc, message);
}

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  bool atEnd() const { return pos_ >= data_.size(); }
  std::size_t pos() const { return pos_; }

  std::uint8_t u8() {
    if (pos_ >= data_.size()) {
      fail(SmfErrc::truncated, "SMF: unexpected end of data");
    }
    return data_[pos_++];
  }
  std::uint8_t peek() const {
    if (pos_ >= data_.size()) {
      fail(SmfErrc::truncated, "SMF: unexpected end of data");
    }
    return data_[pos_];
  }
  std::uint16_t u16() { return static_cast<std::uint16_t>(u8() << 8 | u8()); }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v = v << 8 | u8();
    }
    return v;
  }
  // Variable-length quantity, max 4 bytes per the spec.
  std::uint32_t vlq() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      const std::uint8_t b = u8();
      v = v << 7 | (b & 0x7F);
      if ((b & 0x80) == 0) {
        return v;
      }
    }
    fail(SmfErrc::bad_event, "SMF: variable-length quantity too long");
  }
  void skip(std::size_t n) {
    if (pos_ + n > data_.size()) {
      fail(SmfErrc::truncated, "SMF: unexpected end of data");
    }
    pos_ += n;
  }
  void expectTag(const char* tag, SmfErrc errc) {
    for (int i = 0; i < 4; ++i) {
      if (u8() != static_cast<std::uint8_t>(tag[i])) {
        fail(errc, std::string("SMF: expected chunk '") + tag + "'");
      }
    }
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

struct RawNote {
  int track;
  int order;  // in-track completion-independent note-on order
  std::uint64_t on_tick;
  std::uint64_t off_tick;
  std::uint8_t channel;
  std::uint8_t pitch;
  std::uint8_t velocity;
};

struct TempoEvent {
  std::uint64_t tick;
  std::uint32_t usec_per_qn;
  int order;  // stable resolution when several land on one tick: last wins
};

// Exact tick -> sample conversion. N accumulates in 128-bit integers; the
// denominator D = 1'000'000 * ppq is constant per file.
class TickClock {
 public:
  TickClock(std::vector<TempoEvent> tempi, std::uint32_t ppq, std::uint32_t sample_rate)
      : d_(static_cast<std::uint64_t>(1'000'000) * ppq) {
    std::stable_sort(tempi.begin(), tempi.end(), [](const TempoEvent& a, const TempoEvent& b) {
      return a.tick < b.tick || (a.tick == b.tick && a.order < b.order);
    });
    __int128 n = 0;
    std::uint64_t tick = 0;
    std::uint32_t usec = 500'000;  // spec default: 120 bpm
    segments_.push_back({0, 0, usec});
    for (const TempoEvent& t : tempi) {
      n += static_cast<__int128>(t.tick - tick) * usec * sample_rate;
      tick = t.tick;
      usec = t.usec_per_qn;
      if (!segments_.empty() && segments_.back().tick == tick) {
        segments_.back() = {tick, n, usec};  // same-tick tempo: last wins
      } else {
        segments_.push_back({tick, n, usec});
      }
    }
    sr_ = sample_rate;
  }

  std::int64_t sampleAt(std::uint64_t tick) const {
    const Segment* seg = &segments_.front();
    for (const Segment& s : segments_) {
      if (s.tick <= tick) {
        seg = &s;
      } else {
        break;
      }
    }
    const __int128 n =
        seg->n + static_cast<__int128>(tick - seg->tick) * seg->usec * sr_;
    return static_cast<std::int64_t>((n + d_ / 2) / d_);  // round half up
  }

  double initialBpm() const {
    return 60'000'000.0 / static_cast<double>(segments_.front().usec);
  }

 private:
  struct Segment {
    std::uint64_t tick;
    __int128 n;  // exact numerator of the segment start's sample position
    std::uint32_t usec;
  };
  std::vector<Segment> segments_;
  std::uint64_t d_;
  std::uint32_t sr_ = 48'000;
};

}  // namespace

CanonicalTimeline buildTimelineFromSmfBytes(std::span<const std::uint8_t> data,
                                            std::uint32_t sample_rate) {
  if (sample_rate == 0) {
    fail(SmfErrc::bad_event, "sample_rate must be nonzero");
  }
  Reader r(data);

  r.expectTag("MThd", SmfErrc::bad_header);
  if (r.u32() != 6) {
    fail(SmfErrc::bad_header, "SMF: bad MThd length");
  }
  const std::uint16_t format = r.u16();
  if (format > 1) {
    fail(SmfErrc::unsupported_format,
         "SMF: only formats 0 and 1 are supported (got " + std::to_string(format) + ")");
  }
  const std::uint16_t ntrks = r.u16();
  const std::uint16_t division = r.u16();
  if (division & 0x8000) {
    fail(SmfErrc::unsupported_smpte, "SMF: SMPTE division is not supported yet");
  }
  const std::uint32_t ppq = division & 0x7FFF;
  if (ppq == 0) {
    fail(SmfErrc::bad_header, "SMF: division must be positive");
  }

  std::vector<RawNote> raw;
  std::vector<TempoEvent> tempi;
  int tempo_order = 0;
  int ts_num = 4;
  int ts_den = 4;
  bool ts_seen = false;

  for (int track = 0; track < ntrks; ++track) {
    r.expectTag("MTrk", SmfErrc::bad_header);
    const std::uint32_t len = r.u32();
    const std::size_t track_end = r.pos() + len;

    std::uint64_t tick = 0;
    std::uint8_t running_status = 0;
    int note_order = 0;
    // (channel, pitch) -> open notes, FIFO per the common overlapping-note policy.
    std::map<std::pair<std::uint8_t, std::uint8_t>, std::deque<std::size_t>> open;

    auto closeNote = [&](std::uint8_t channel, std::uint8_t pitch, std::uint64_t off_tick) {
      auto it = open.find({channel, pitch});
      if (it == open.end() || it->second.empty()) {
        return;  // dangling note-off: tolerated in v0
      }
      raw[it->second.front()].off_tick = off_tick;
      it->second.pop_front();
    };

    while (r.pos() < track_end) {
      tick += r.vlq();
      std::uint8_t status = r.peek();
      if (status & 0x80) {
        r.u8();
        if (status < 0xF0) {
          running_status = status;
        }
      } else {
        if (running_status == 0) {
          fail(SmfErrc::bad_event, "SMF: data byte with no running status");
        }
        status = running_status;
      }

      const std::uint8_t kind = status & 0xF0;
      const std::uint8_t channel = status & 0x0F;
      switch (kind) {
        case 0x80: {  // note off
          const std::uint8_t pitch = r.u8();
          r.u8();  // release velocity: reserved, not yet represented
          closeNote(channel, pitch, tick);
          break;
        }
        case 0x90: {  // note on (velocity 0 == off)
          const std::uint8_t pitch = r.u8();
          const std::uint8_t vel = r.u8();
          if (vel == 0) {
            closeNote(channel, pitch, tick);
          } else {
            open[{channel, pitch}].push_back(raw.size());
            raw.push_back({track, note_order++, tick, 0, channel, pitch, vel});
          }
          break;
        }
        case 0xA0:
        case 0xB0:
        case 0xE0:
          r.skip(2);  // pressure / CC / bend: parsed past, reserved for later
          break;
        case 0xC0:
        case 0xD0:
          r.skip(1);
          break;
        case 0xF0: {
          if (status == 0xF0 || status == 0xF7) {  // sysex
            r.skip(r.vlq());
            running_status = 0;
          } else if (status == 0xFF) {  // meta
            const std::uint8_t type = r.u8();
            const std::uint32_t mlen = r.vlq();
            const std::size_t next = r.pos() + mlen;
            if (type == 0x51 && mlen == 3) {
              std::uint32_t usec = 0;
              for (int i = 0; i < 3; ++i) {
                usec = usec << 8 | r.u8();
              }
              if (usec == 0) {
                fail(SmfErrc::bad_event, "SMF: tempo of zero usec/qn");
              }
              tempi.push_back({tick, usec, tempo_order++});
            } else if (type == 0x58 && mlen >= 2 && !ts_seen) {
              ts_num = r.u8();
              ts_den = 1 << r.u8();
              ts_seen = true;
            }
            if (r.pos() > next) {
              fail(SmfErrc::bad_event, "SMF: meta event overran its length");
            }
            r.skip(next - r.pos());
            running_status = 0;
            if (type == 0x2F) {  // end of track
              // Consume any padding up to the declared chunk end.
              r.skip(track_end - r.pos());
            }
          } else {
            fail(SmfErrc::bad_event,
                 "SMF: unsupported system event 0x" + std::to_string(status));
          }
          break;
        }
        default:
          fail(SmfErrc::bad_event, "SMF: unrecognized event");
      }
    }

    // Unterminated notes close at the end of their track (lenient v0 policy).
    for (auto& [key, queue] : open) {
      for (std::size_t idx : queue) {
        raw[idx].off_tick = std::max(tick, raw[idx].on_tick);
      }
    }
  }

  const TickClock clock(std::move(tempi), ppq, sample_rate);

  // Same-tick policy: track order, then in-track note-on order.
  std::stable_sort(raw.begin(), raw.end(), [](const RawNote& a, const RawNote& b) {
    if (a.on_tick != b.on_tick) {
      return a.on_tick < b.on_tick;
    }
    if (a.track != b.track) {
      return a.track < b.track;
    }
    return a.order < b.order;
  });

  std::vector<NoteEvent> notes;
  notes.reserve(raw.size());
  std::int64_t total = 0;
  std::int32_t source_index = 0;
  for (const RawNote& n : raw) {
    NoteEvent ev;
    ev.pitch = n.pitch;
    ev.channel = n.channel;
    ev.velocity = n.velocity;
    ev.on_sample = clock.sampleAt(n.on_tick);
    ev.off_sample = clock.sampleAt(n.off_tick);
    if (ev.off_sample <= ev.on_sample) {
      ev.off_sample = ev.on_sample + 1;
    }
    ev.start_qn = static_cast<double>(n.on_tick) / static_cast<double>(ppq);
    ev.dur_qn = static_cast<double>(n.off_tick - n.on_tick) / static_cast<double>(ppq);
    ev.source_index = source_index++;
    total = std::max(total, ev.off_sample);
    notes.push_back(ev);
  }

  // Notes are already in canonical order; stable re-sort is a no-op safeguard.
  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    return a.on_sample < b.on_sample;
  });

  return CanonicalTimeline(sample_rate, clock.initialBpm(), ts_num, ts_den,
                           std::move(notes), total);
}

CanonicalTimeline buildTimelineFromSmfFile(const std::filesystem::path& path,
                                           std::uint32_t sample_rate) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail(SmfErrc::io_error, "SMF: cannot open file: " + path.string());
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  return buildTimelineFromSmfBytes(bytes, sample_rate);
}

}  // namespace mattergraph::midi
