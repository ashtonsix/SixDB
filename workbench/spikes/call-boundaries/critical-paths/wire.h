#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace critical_paths {
enum class Decode { native, multiply, simd, fp };
template<class T> [[gnu::always_inline]] inline T load(const void* p) noexcept {
  static_assert(std::endian::native == std::endian::little);
  T value; std::memcpy(&value, p, sizeof(T)); return value;
}
template<Decode D>
[[gnu::always_inline]] inline std::uint32_t local20(const std::uint8_t* series, std::size_t row) noexcept {
  const auto* tile = series + (row / 8) * 20;
  const auto lane = unsigned(row & 7);
  const auto high = load<std::uint16_t>(tile + lane * 2);
  const auto planes = load<std::uint32_t>(tile + 16);
  std::uint32_t tail;
  if constexpr (D == Decode::simd) {
#if defined(__x86_64__)
    // The bit at lane in each of four bytes becomes its sign bit. Carries
    // between paired bytes cannot reach bit 7 of the next byte for shift <= 7.
    const auto v = _mm_cvtsi32_si128(int(planes));
    tail = unsigned(_mm_movemask_epi8(_mm_sll_epi16(v, _mm_cvtsi32_si128(7 - lane)))) & 15;
#elif defined(__aarch64__)
    const auto v = vreinterpret_u8_u32(vcreate_u32(planes));
    const auto bits = vand_u8(vshr_n_u8(vshl_u8(v, vdup_n_s8(std::int8_t(7 - lane))), 7), vdup_n_u8(1));
    const std::uint8_t weights[8]{1,2,4,8,0,0,0,0};
    tail = vaddv_u8(vmul_u8(bits, vld1_u8(weights)));
#else
#error Unsupported diagnostic target
#endif
  } else {
    const auto bits = std::uint64_t(planes >> lane) & 0x01010101u;
#if defined(__BMI2__)
    if constexpr (D == Decode::native || D == Decode::fp)
      tail = unsigned(_pext_u64(bits, 0x01010101u));
    else
#endif
      tail = unsigned((bits * 0x0102040810204080ULL) >> 56);
  }
  if constexpr (D == Decode::fp) {
    // Range is a uint16 body plus four bit planes: all inputs, intermediate
    // values and results are exact binary32 integers below 2^20. No fast-math,
    // implicit contraction, approximation or changed checksum is permitted.
    const float value = float(high) * 16.0f + float(tail);
    return std::uint32_t(value);
  }
  return (std::uint32_t(high) << 4) | tail;
}
} // namespace critical_paths
