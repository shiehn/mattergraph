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

std::vector<float> readWavMono(const std::filesystem::path& path,
                               std::uint32_t required_sample_rate) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open wav: " + path.string());
  }
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  auto u32at = [&](std::size_t off) {
    std::uint32_t v;
    std::memcpy(&v, bytes.data() + off, 4);
    return v;
  };
  auto u16at = [&](std::size_t off) {
    std::uint16_t v;
    std::memcpy(&v, bytes.data() + off, 2);
    return v;
  };
  if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    throw std::runtime_error("not a RIFF/WAVE file: " + path.string());
  }

  std::uint16_t format = 0, channels = 0, bits = 0;
  std::uint32_t sample_rate = 0;
  std::size_t data_off = 0, data_len = 0;
  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const std::uint32_t chunk_len = u32at(pos + 4);
    if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0 && chunk_len >= 16) {
      format = u16at(pos + 8);
      channels = u16at(pos + 10);
      sample_rate = u32at(pos + 12);
      bits = u16at(pos + 22);
    } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0) {
      data_off = pos + 8;
      data_len = std::min<std::size_t>(chunk_len, bytes.size() - data_off);
    }
    pos += 8 + chunk_len + (chunk_len & 1);
  }
  if (channels == 0 || data_off == 0) {
    throw std::runtime_error("missing fmt/data chunk: " + path.string());
  }
  if (sample_rate != required_sample_rate) {
    throw std::runtime_error("exciter sample must be " +
                             std::to_string(required_sample_rate) + " Hz: " +
                             path.string());
  }

  std::vector<float> mono;
  if (format == 3 && bits == 32) {
    const std::size_t frames = data_len / (4u * channels);
    mono.resize(frames);
    const float* f = reinterpret_cast<const float*>(bytes.data() + data_off);
    for (std::size_t n = 0; n < frames; ++n) {
      float acc = 0.0f;
      for (std::uint16_t c = 0; c < channels; ++c) {
        acc += f[n * channels + c];
      }
      mono[n] = acc / static_cast<float>(channels);
    }
  } else if (format == 1 && bits == 16) {
    const std::size_t frames = data_len / (2u * channels);
    mono.resize(frames);
    const std::int16_t* s = reinterpret_cast<const std::int16_t*>(bytes.data() + data_off);
    for (std::size_t n = 0; n < frames; ++n) {
      float acc = 0.0f;
      for (std::uint16_t c = 0; c < channels; ++c) {
        acc += static_cast<float>(s[n * channels + c]) / 32768.0f;
      }
      mono[n] = acc / static_cast<float>(channels);
    }
  } else {
    throw std::runtime_error("unsupported wav format (need PCM16 or float32): " +
                             path.string());
  }
  return mono;
}

}  // namespace mattergraph::render
