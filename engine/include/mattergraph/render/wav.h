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

}  // namespace mattergraph::render
