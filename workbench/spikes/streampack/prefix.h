#pragma once
#include <cstdint>
#include <bit>
#include <type_traits>
#include <utility>
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace streampack_next {
template<class Width> inline void copy16_native(Width* out, const Width* in) {
  static_assert(sizeof(Width) == 1 || sizeof(Width) == 2 || sizeof(Width) == 4);
#if defined(__aarch64__)
  if constexpr (sizeof(Width) == 1) vst1q_u8(out, vld1q_u8(in));
  else if constexpr (sizeof(Width) == 2) { vst1q_u16(out, vld1q_u16(in)); vst1q_u16(out + 8, vld1q_u16(in + 8)); }
  else for (unsigned i = 0; i < 16; i += 4) vst1q_u32(out + i, vld1q_u32(in + i));
#elif defined(__AVX2__)
  if constexpr (sizeof(Width) == 1) _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_loadu_si128(reinterpret_cast<const __m128i*>(in)));
  else if constexpr (sizeof(Width) == 2) _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in)));
  else {
#if defined(__AVX512F__)
    _mm512_storeu_si512(out, _mm512_loadu_si512(in));
#else
    for (unsigned i = 0; i < 16; i += 8) _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + i)));
#endif
  }
#else
  for (unsigned i = 0; i < 16; ++i) out[i] = in[i];
#endif
}

// TuplePack's byte-contained codes can split a used length at a non-byte bit
// boundary. A grouped projection supplies one sixteen-byte plane per code.
// Reassemble all sixteen values explicitly; no scalar per-row decoder calls.
template<class Out, unsigned Bits, unsigned FirstBits>
[[gnu::always_inline]] inline void assemble16_native(const std::uint8_t* codes, Out* out) {
  static_assert(Bits > 0 && Bits <= 32 && FirstBits > 0 && FirstBits <= 8);
  static_assert(Bits <= sizeof(Out) * 8);
  constexpr unsigned count = 1 + (Bits > FirstBits ? (Bits - FirstBits + 7) / 8 : 0);
  auto shifts = [&]<class Add>(Add add) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      (add.template operator()<I, I ? FirstBits + (I - 1) * 8 : 0>(), ...);
    }(std::make_index_sequence<count>{});
  };
#if defined(__aarch64__)
  if constexpr (sizeof(Out) == 1) {
    auto value = vdupq_n_u8(0);
    shifts([&]<unsigned C, unsigned S>() { value = vorrq_u8(value, vshlq_n_u8(vld1q_u8(codes + 16 * C), S)); });
    vst1q_u8(out, value);
  }
  else if constexpr (sizeof(Out) == 2) for (unsigned i = 0; i < 16; i += 8) {
    auto value = vdupq_n_u16(0);
    shifts([&]<unsigned C, unsigned S>() { value = vorrq_u16(value, vshlq_n_u16(vmovl_u8(vld1_u8(codes + 16 * C + i)), S)); });
    vst1q_u16(out + i, value);
  } else for (unsigned i = 0; i < 16; i += 4) {
    auto value = vdupq_n_u32(0);
    shifts([&]<unsigned C, unsigned S>() {
      const auto bytes = vreinterpret_u8_u32(vld1_dup_u32(reinterpret_cast<const std::uint32_t*>(codes + 16 * C + i)));
      value = vorrq_u32(value, vshlq_n_u32(vmovl_u16(vget_low_u16(vmovl_u8(bytes))), S));
    });
    vst1q_u32(out + i, value);
  }
#elif defined(__AVX2__)
  if constexpr (sizeof(Out) == 1) {
    auto value = _mm_setzero_si128();
    // Each admitted code's shifted bits fit in its byte, so a 16-bit shift
    // cannot carry bits from one byte value into its neighbor.
    shifts([&]<unsigned C, unsigned S>() { value = _mm_or_si128(value, _mm_slli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(codes + 16 * C)), S)); });
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), value);
  }
  else if constexpr (sizeof(Out) == 2) {
    auto value = _mm256_setzero_si256();
    shifts([&]<unsigned C, unsigned S>() { value = _mm256_or_si256(value, _mm256_slli_epi16(_mm256_cvtepu8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(codes + 16 * C))), S)); });
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), value);
  } else {
#if defined(__AVX512F__)
    auto value = _mm512_setzero_si512();
    shifts([&]<unsigned C, unsigned S>() { value = _mm512_or_si512(value, _mm512_slli_epi32(_mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(codes + 16 * C))), S)); });
    _mm512_storeu_si512(out, value);
#else
    for (unsigned i = 0; i < 16; i += 8) {
      auto value = _mm256_setzero_si256();
      shifts([&]<unsigned C, unsigned S>() { value = _mm256_or_si256(value, _mm256_slli_epi32(_mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(codes + 16 * C + i))), S)); });
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), value);
    }
#endif
  }
#else
  for (unsigned i = 0; i < 16; ++i) {
    out[i] = 0;
    shifts([&]<unsigned C, unsigned S>() { out[i] |= unsigned(codes[16 * C + i]) << S; });
  }
#endif
}

// Direct stores exclusive endpoints. Shift sixteen decoded endpoints by one,
// inserting the previous group's endpoint; no prefix sum is required.
template<class End> [[gnu::always_inline]] inline unsigned endpoint_starts16_native(
    const End* ends, unsigned* starts, unsigned incoming) {
  static_assert(sizeof(End) == 2 || sizeof(End) == 4);
#if defined(__AVX512F__) && defined(__AVX512BW__)
  const auto values = [&] {
    if constexpr (sizeof(End) == 2) return _mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(ends)));
    else return _mm512_loadu_si512(ends);
  }();
  const auto shifted = _mm512_permutexvar_epi32(_mm512_setr_epi32(0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14), values);
  _mm512_storeu_si512(starts, _mm512_mask_mov_epi32(shifted, 1, _mm512_set1_epi32(incoming)));
  return unsigned(_mm_extract_epi32(_mm512_extracti32x4_epi32(values, 3), 3));
#elif defined(__AVX2__)
  for (unsigned i = 0; i < 16; i += 8) {
    const auto values = [&] {
      if constexpr (sizeof(End) == 2) return _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(ends + i)));
      else return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ends + i));
    }();
    const auto shifted = _mm256_permutevar8x32_epi32(values, _mm256_setr_epi32(0,0,1,2,3,4,5,6));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(starts + i), _mm256_blend_epi32(shifted, _mm256_set1_epi32(incoming), 1));
    incoming = unsigned(_mm256_extract_epi32(values, 7));
  }
  return incoming;
#elif defined(__aarch64__)
  for (unsigned i = 0; i < 16; i += 4) {
    const auto values = [&] {
      if constexpr (sizeof(End) == 2) return vmovl_u16(vld1_u16(ends + i));
      else return vld1q_u32(ends + i);
    }();
    vst1q_u32(starts + i, vextq_u32(vdupq_n_u32(incoming), values, 3));
    incoming = vgetq_lane_u32(values, 3);
  }
  return incoming;
#else
  for (unsigned i = 0; i < 16; ++i) { starts[i] = incoming; incoming = ends[i]; }
  return incoming;
#endif
}

// Sixteen exclusive starts plus the next group's carry. Every addition widens
// to 32 bits first: sixteen legal u16 reservations can total 1,048,560 bytes.
template<class Width> inline unsigned starts16_native(const Width* widths, unsigned* starts, unsigned carry) {
#if defined(__AVX512F__) && defined(__AVX512BW__)
  const auto values = [&] {
    if constexpr (sizeof(Width) == 1) return _mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(widths)));
    else return _mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(widths)));
  }();
  auto sum = values;
  sum = _mm512_add_epi32(sum, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_setr_epi32(0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14), sum));
  sum = _mm512_add_epi32(sum, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_setr_epi32(0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13), sum));
  sum = _mm512_add_epi32(sum, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_setr_epi32(0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11), sum));
  sum = _mm512_add_epi32(sum, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_setr_epi32(0,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7), sum));
  _mm512_storeu_si512(starts, _mm512_add_epi32(_mm512_sub_epi32(sum, values), _mm512_set1_epi32(carry)));
  return carry + unsigned(_mm_extract_epi32(_mm512_extracti32x4_epi32(sum, 3), 3));
#elif defined(__AVX2__)
  auto part = [&](const Width* in, unsigned* out, unsigned incoming) {
    const auto values = [&] {
      if constexpr (sizeof(Width) == 1) return _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(in)));
      else return _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(in)));
    }();
    auto sum = _mm256_add_epi32(values, _mm256_slli_si256(values, 4));
    sum = _mm256_add_epi32(sum, _mm256_slli_si256(sum, 8));
    const auto lower_carry = _mm256_and_si256(_mm256_permutevar8x32_epi32(sum, _mm256_set1_epi32(3)), _mm256_setr_epi32(0,0,0,0,-1,-1,-1,-1));
    sum = _mm256_add_epi32(sum, lower_carry);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_add_epi32(_mm256_sub_epi32(sum, values), _mm256_set1_epi32(incoming)));
    return incoming + unsigned(_mm256_extract_epi32(sum, 7));
  };
  return part(widths + 8, starts + 8, part(widths, starts, carry));
#elif defined(__aarch64__)
  auto part = [&](const Width* in, unsigned* out, unsigned incoming) {
    const auto values = [&] {
      if constexpr (sizeof(Width) == 1) {
        std::uint32_t word; __builtin_memcpy(&word, in, sizeof(word));
        return vmovl_u16(vget_low_u16(vmovl_u8(vcreate_u8(word))));
      } else return vmovl_u16(vld1_u16(in));
    }();
    const auto zero = vdupq_n_u32(0);
    auto sum = vaddq_u32(values, vextq_u32(zero, values, 3));
    sum = vaddq_u32(sum, vextq_u32(zero, sum, 2));
    vst1q_u32(out, vaddq_u32(vsubq_u32(sum, values), vdupq_n_u32(incoming)));
    return incoming + vgetq_lane_u32(sum, 3);
  };
  carry = part(widths, starts, carry); carry = part(widths + 4, starts + 4, carry);
  carry = part(widths + 8, starts + 8, carry); return part(widths + 12, starts + 12, carry);
#else
  for (unsigned i = 0; i < 16; ++i) { starts[i] = carry; carry += widths[i]; }
  return carry;
#endif
}

template<class Width> inline std::uint32_t prefix16_scalar(const Width* widths, unsigned count) {
  std::uint32_t sum = 0;
  for (unsigned i = 0; i < 16; ++i) sum += i < count ? widths[i] : 0;
  return sum;
}
/// count is in [0,16]. Sixteen initialized widths are readable, even when count is smaller. Sum
/// widens before reduction; sixteen legal uint16 widths exceed uint16 range.
template<class Width> inline std::uint32_t prefix16_native(const Width* widths, unsigned count) {
  static_assert(std::is_same_v<Width, std::uint8_t> || std::is_same_v<Width, std::uint16_t>);
#if defined(__aarch64__)
  if constexpr (sizeof(Width) == 1) {
    const uint8x16_t lanes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    return vaddlvq_u8(vandq_u8(vld1q_u8(widths), vcltq_u8(lanes, vdupq_n_u8(count))));
  } else {
    const uint16x8_t lo{0, 1, 2, 3, 4, 5, 6, 7}, hi{8, 9, 10, 11, 12, 13, 14, 15};
    const auto bound = vdupq_n_u16(count);
    return vaddlvq_u16(vandq_u16(vld1q_u16(widths), vcltq_u16(lo, bound))) +
        vaddlvq_u16(vandq_u16(vld1q_u16(widths + 8), vcltq_u16(hi, bound)));
  }
#elif defined(__AVX2__)
  if constexpr (sizeof(Width) == 1) {
    const auto mask = _mm_cmpgt_epi8(_mm_set1_epi8(count), _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    const auto sums = _mm_sad_epu8(_mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(widths)), mask), _mm_setzero_si128());
    return _mm_cvtsi128_si32(sums) + _mm_cvtsi128_si32(_mm_srli_si128(sums, 8));
  } else {
    const auto mask = _mm256_cmpgt_epi16(_mm256_set1_epi16(count), _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    const auto selected = _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(widths)), mask);
    const auto sums = _mm256_add_epi32(_mm256_cvtepu16_epi32(_mm256_castsi256_si128(selected)),
        _mm256_cvtepu16_epi32(_mm256_extracti128_si256(selected, 1)));
    auto four = _mm_add_epi32(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
    four = _mm_add_epi32(four, _mm_srli_si128(four, 8));
    return _mm_cvtsi128_si32(_mm_add_epi32(four, _mm_srli_si128(four, 4)));
  }
#else
  return prefix16_scalar(widths, count);
#endif
}

/// First inclusive endpoint greater than offset, or 16 if none. Sixteen
/// initialized byte widths are readable; offset is <=4080. No persistent
/// decoded geometry is produced. Widen before adding: legal totals reach 4080.
inline unsigned owner16_native(const std::uint8_t* widths, unsigned offset) {
#if defined(__aarch64__)
  const auto bytes = vld1q_u8(widths); const auto zero = vdupq_n_u16(0);
  auto lo = vmovl_u8(vget_low_u8(bytes)), hi = vmovl_u8(vget_high_u8(bytes));
  lo = vaddq_u16(lo, vextq_u16(zero, lo, 7)); hi = vaddq_u16(hi, vextq_u16(zero, hi, 7));
  lo = vaddq_u16(lo, vextq_u16(zero, lo, 6)); hi = vaddq_u16(hi, vextq_u16(zero, hi, 6));
  lo = vaddq_u16(lo, vextq_u16(zero, lo, 4)); hi = vaddq_u16(hi, vextq_u16(zero, hi, 4));
  hi = vaddq_u16(hi, vdupq_n_u16(vgetq_lane_u16(lo, 7)));
  const auto bound = vdupq_n_u16(offset);
  return vaddvq_u16(vaddq_u16(vshrq_n_u16(vcleq_u16(lo, bound), 15), vshrq_n_u16(vcleq_u16(hi, bound), 15)));
#elif defined(__AVX2__)
  auto sums = _mm256_cvtepu8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(widths)));
  sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 2));
  sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 4));
  sums = _mm256_add_epi16(sums, _mm256_slli_si256(sums, 8));
  const auto carry = _mm_set1_epi16(_mm_extract_epi16(_mm256_castsi256_si128(sums), 7));
  sums = _mm256_add_epi16(sums, _mm256_inserti128_si256(_mm256_setzero_si256(), carry, 1));
  const auto greater = _mm256_cmpgt_epi16(sums, _mm256_set1_epi16(offset));
  return std::countr_zero(unsigned(_mm256_movemask_epi8(greater))) / 2;
#else
  unsigned end = 0;
  for (unsigned r = 0; r < 16; ++r) { end += widths[r]; if (offset < end) return r; }
  return 16;
#endif
}
} // namespace streampack_next
