#pragma once

#include <cstdint>

namespace mattergraph::dsp {

// Deterministic stream derivation (plan §3.5): every random stream is keyed by
// stable identifiers, so adding a node or note never reseeds unrelated voices.

constexpr std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

constexpr std::uint64_t deriveStream(std::uint64_t seed, std::uint64_t stream_id) {
  return splitmix64(splitmix64(seed) ^ splitmix64(stream_id * 0x9e3779b97f4a7c15ULL));
}

// xoshiro256** — fast, deterministic, good enough for audio noise.
class Rng {
 public:
  explicit Rng(std::uint64_t seed) {
    s_[0] = splitmix64(seed);
    s_[1] = splitmix64(s_[0]);
    s_[2] = splitmix64(s_[1]);
    s_[3] = splitmix64(s_[2]);
  }

  std::uint64_t next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform in [-1, 1).
  double bipolar() {
    return static_cast<double>(next() >> 11) * (2.0 / 9007199254740992.0) - 1.0;
  }

 private:
  static constexpr std::uint64_t rotl(std::uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
  }
  std::uint64_t s_[4]{};
};

}  // namespace mattergraph::dsp
