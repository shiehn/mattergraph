#include "mattergraph/render/wav.h"

#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace mattergraph::render {
namespace {

static_assert(std::endian::native == std::endian::little,
              "WAV writer assumes a little-endian host");

void put32(std::ofstream& out, std::uint32_t v) {
  out.write(reinterpret_cast<const char*>(&v), 4);
}
void put16(std::ofstream& out, std::uint16_t v) {
  out.write(reinterpret_cast<const char*>(&v), 2);
}

}  // namespace

void writeWavF32(const std::filesystem::path& path,
                 const std::vector<float>& interleaved, std::uint32_t sample_rate,
                 std::uint16_t channels) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("cannot open for writing: " + path.string());
  }

  const auto data_bytes = static_cast<std::uint32_t>(interleaved.size() * 4);
  const std::uint32_t byte_rate = sample_rate * channels * 4;
  const auto block_align = static_cast<std::uint16_t>(channels * 4);

  out.write("RIFF", 4);
  put32(out, 36 + data_bytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  put32(out, 16);
  put16(out, 3);  // IEEE float
  put16(out, channels);
  put32(out, sample_rate);
  put32(out, byte_rate);
  put16(out, block_align);
  put16(out, 32);  // bits per sample
  out.write("data", 4);
  put32(out, data_bytes);
  out.write(reinterpret_cast<const char*>(interleaved.data()), data_bytes);

  if (!out.good()) {
    throw std::runtime_error("write failed: " + path.string());
  }
}

}  // namespace mattergraph::render
