#include "wire.h"
#include <array>
#include <cfenv>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace critical_paths;
  std::array<std::uint8_t, 40> bytes{};
  std::uint64_t cases = 0;
  for (std::uint32_t value = 0; value < (1u << 20); ++value) {
    for (unsigned lane = 0; lane < 8; ++lane) {
      // Independent wire-law encoding. Noise in other lanes catches a bit
      // moving into a neighbour byte during SIMD extraction.
      const unsigned tile = value & 1;
      auto* p = bytes.data() + tile * 20;
      const std::uint16_t high = value >> 4;
      std::memcpy(p + lane * 2, &high, 2);
      for (unsigned bit = 0; bit < 4; ++bit) {
        const auto noise = std::uint8_t(value * 37 + bit * 113 + lane * 19);
        p[16 + bit] = std::uint8_t((noise & ~(1u << lane)) | (((value >> bit) & 1) << lane));
      }
      const auto row = tile * 8 + lane;
      if (local20<Decode::native>(bytes.data(), row) != value ||
          local20<Decode::multiply>(bytes.data(), row) != value ||
          local20<Decode::simd>(bytes.data(), row) != value ||
          local20<Decode::fp>(bytes.data(), row) != value)
        throw std::runtime_error("independent Local20 wire-law mismatch");
      ++cases;
    }
  }
  for (const auto mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode)) throw std::runtime_error("rounding mode unsupported");
    std::feclearexcept(FE_ALL_EXCEPT);
    for (const auto value : {0u,1u,15u,16u,65535u,65536u,1048575u}) {
      const std::uint16_t high = value >> 4;
      std::memcpy(bytes.data(), &high, 2);
      for (unsigned bit = 0; bit < 4; ++bit) bytes[16 + bit] = (value >> bit) & 1;
      if (local20<Decode::fp>(bytes.data(), 0) != value)
        throw std::runtime_error("exact FP decode changed under rounding mode");
    }
    if (std::fetestexcept(FE_ALL_EXCEPT)) throw std::runtime_error("exact FP decode raised an exception");
  }
  std::fesetround(FE_TONEAREST);
  std::cout << "{\"check\":\"independent-wire-law\",\"passed\":true,\"cases\":" << cases
            << ",\"decode_arms\":4,\"rounding_modes\":4}\n";
}
