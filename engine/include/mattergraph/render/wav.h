#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace mattergraph::render {

// Minimal IEEE-float WAV writer (format tag 3). Interleaved samples.
// Throws std::runtime_error on IO failure.
void writeWavF32(const std::filesystem::path& path,
                 const std::vector<float>& interleaved, std::uint32_t sample_rate,
                 std::uint16_t channels);

// Minimal WAV reader for exciter-bank assets: PCM16 or IEEE-float, any channel
// count (mixed to mono), sample rate must equal `required_sample_rate` (the
// curation step normalizes the bank; the engine does not resample). Throws
// std::runtime_error on IO/format problems.
std::vector<float> readWavMono(const std::filesystem::path& path,
                               std::uint32_t required_sample_rate);

}  // namespace mattergraph::render
