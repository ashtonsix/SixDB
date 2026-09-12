#pragma once
#include "model.h"
#include <algorithm>
#include <cstring>
#include <type_traits>

#if defined(__aarch64__)
#include <arm_neon.h>
#define TUPLE_CC
#elif defined(__AVX2__)
#include <immintrin.h>
#if defined(__AVX512VBMI__)
#define TUPLE_CC
#else
#define TUPLE_CC __attribute__((regcall))
#endif
#endif

namespace tuple_runtime {
#if defined(__aarch64__)
struct native_packet { uint8x16_t a, b, c, d; };
using vector16 = uint8x16_t;
#elif defined(__AVX512VBMI__)
using native_packet = __m512i;
using vector16 = __m128i;
#else
struct native_packet { __m256i a, b; };
using vector16 = __m128i;
#endif

namespace native_detail {
// Exact bounded scalar loads assemble a short final source chunk in registers.
// Neither an adjacent tuple nor implicit allocation padding may be read.
[[gnu::always_inline]] inline std::uint64_t tail8(const byte* p, unsigned n) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    if (n & 4) {
        std::uint32_t x;
        std::memcpy(&x, p, 4);
        result = x; p += 4; shift = 32;
    }
    if (n & 2) {
        std::uint16_t x;
        std::memcpy(&x, p, 2);
        result |= std::uint64_t(x) << shift; p += 2; shift += 16;
    }
    if (n & 1) result |= std::uint64_t(*p) << shift;
    return result;
}
[[gnu::always_inline]] inline vector16 load16(const byte* p, unsigned n) {
    if (n == 16) {
#if defined(__aarch64__)
        return vld1q_u8(p);
#else
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#endif
    }
    std::uint64_t low, high = 0;
    if (n >= 8) {
        std::memcpy(&low, p, 8);
        high = tail8(p + 8, n - 8);
    } else low = tail8(p, n);
#if defined(__aarch64__)
    return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(low), vcreate_u64(high)));
#else
    return _mm_set_epi64x(high, low);
#endif
}
[[gnu::always_inline]] inline vector16 zero16() {
#if defined(__aarch64__)
    return vdupq_n_u8(0);
#else
    return _mm_setzero_si128();
#endif
}
[[gnu::always_inline]] inline void store_tail8(byte* p, unsigned n, std::uint64_t value) {
    if (n & 4) { auto v = std::uint32_t(value); std::memcpy(p, &v, 4); p += 4; value >>= 32; }
    if (n & 2) { auto v = std::uint16_t(value); std::memcpy(p, &v, 2); p += 2; value >>= 16; }
    if (n & 1) *p = byte(value);
}
[[gnu::always_inline]] inline void store16(byte* p, unsigned n, vector16 value) {
    if (n == 16) {
#if defined(__aarch64__)
        vst1q_u8(p, value);
#else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), value);
#endif
        return;
    }
#if defined(__aarch64__)
    auto low = vgetq_lane_u64(vreinterpretq_u64_u8(value), 0);
    auto high = vgetq_lane_u64(vreinterpretq_u64_u8(value), 1);
#else
    auto low = std::uint64_t(_mm_cvtsi128_si64(value));
    auto high = std::uint64_t(_mm_extract_epi64(value, 1));
#endif
    if (n >= 8) { std::memcpy(p, &low, 8); store_tail8(p + 8, n - 8, high); }
    else store_tail8(p, n, low);
}
[[gnu::always_inline]] inline native_packet join(vector16 a, vector16 b, vector16 c, vector16 d) {
#if defined(__aarch64__)
    return {a, b, c, d};
#elif defined(__AVX512VBMI__)
    auto p = _mm512_castsi128_si512(a);
    p = _mm512_inserti32x4(p, b, 1);
    p = _mm512_inserti32x4(p, c, 2);
    return _mm512_inserti32x4(p, d, 3);
#else
    return {_mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 1),
            _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 1)};
#endif
}
template <unsigned I> [[gnu::always_inline]] inline vector16 load_chunk(const read_plan& p, const byte* row) {
    if (I >= p.count) return zero16();
    return load16(row + p.chunks[I].offset, p.chunks[I].bytes);
}
template <unsigned I> [[gnu::always_inline]] inline vector16 split(native_packet p) {
#if defined(__aarch64__)
    if constexpr (I == 0) return p.a;
    if constexpr (I == 1) return p.b;
    if constexpr (I == 2) return p.c;
    return p.d;
#elif defined(__AVX512VBMI__)
    return _mm512_extracti32x4_epi32(p, I);
#else
    return _mm256_extracti128_si256(I < 2 ? p.a : p.b, I % 2);
#endif
}
template <unsigned I> [[gnu::always_inline]] inline vector16 row_part(const byte* row, unsigned bytes) {
    if (bytes <= I * 16) return zero16();
    return load16(row + I * 16, std::min(16u, bytes - I * 16));
}
template <unsigned I> [[gnu::always_inline]] inline void store_part(byte* row, unsigned bytes, native_packet value) {
    if (bytes > I * 16) store16(row + I * 16, std::min(16u, bytes - I * 16), split<I>(value));
}

#if defined(__aarch64__)
template <unsigned I> [[gnu::always_inline]] inline vector16 apply16(native_packet source, const shuffle& p) {
    const auto index = vld1q_u8(p.index.data() + 16 * I);
    uint8x16_t value;
    if (p.routes <= 1) value = vqtbl1q_u8(source.a, index);
    else if (p.routes <= 3) value = vqtbl2q_u8({{source.a, source.b}}, index);
    else if (p.routes <= 7) value = vqtbl3q_u8({{source.a, source.b, source.c}}, index);
    else value = vqtbl4q_u8({{source.a, source.b, source.c, source.d}}, index);
    if (p.shifting) value = vshlq_u8(value, vld1q_s8(p.shift.data() + 16 * I));
    if (p.masking) value = vandq_u8(value, vld1q_u8(p.mask.data() + 16 * I));
    return value;
}
#elif !defined(__AVX512VBMI__)
template <unsigned I, bool Left>
[[gnu::always_inline]] inline __m256i apply32(native_packet source, const shuffle& p) {
    auto result = _mm256_setzero_si256();
    auto append = [&]<unsigned Part>(std::integral_constant<unsigned, Part>) {
        if (!(p.routes & (1u << Part))) return;
        auto half = _mm256_extracti128_si256(Part < 2 ? source.a : source.b, Part % 2);
        auto table = _mm256_broadcastsi128_si256(half);
        auto index = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.avx2_index[Part].data() + I * 32));
        result = _mm256_or_si256(result, _mm256_shuffle_epi8(table, index));
    };
    append(std::integral_constant<unsigned, 0>{});
    append(std::integral_constant<unsigned, 1>{});
    append(std::integral_constant<unsigned, 2>{});
    append(std::integral_constant<unsigned, 3>{});
    if (p.shifting) {
        const auto low = _mm256_set1_epi16(255);
        const auto high = _mm256_set1_epi16(short(0xff00));
        const auto ef = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.even_factor.data() + I * 16));
        const auto of = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.odd_factor.data() + I * 16));
        auto even = _mm256_mullo_epi16(_mm256_and_si256(result, low), ef);
        __m256i odd;
        if constexpr (Left) {
            even = _mm256_and_si256(even, low);
            odd = _mm256_mullo_epi16(_mm256_and_si256(result, high), of);
        } else {
            even = _mm256_srli_epi16(even, 8);
            odd = _mm256_mullo_epi16(_mm256_srli_epi16(result, 8), of);
        }
        result = _mm256_or_si256(even, _mm256_and_si256(odd, high));
    }
    if (p.masking)
        result = _mm256_and_si256(result, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.mask.data() + I * 32)));
    return result;
}
#endif
} // namespace native_detail

[[gnu::always_inline]] inline native_packet load_packet(const byte* p) {
    using namespace native_detail;
    return join(load16(p, 16), load16(p + 16, 16), load16(p + 32, 16), load16(p + 48, 16));
}
[[gnu::always_inline]] inline void store_packet(byte* p, native_packet value) {
#if defined(__aarch64__)
    vst1q_u8(p, value.a); vst1q_u8(p + 16, value.b);
    vst1q_u8(p + 32, value.c); vst1q_u8(p + 48, value.d);
#elif defined(__AVX512VBMI__)
    _mm512_storeu_si512(p, value);
#else
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), value.a);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + 32), value.b);
#endif
}
template <bool Left> [[gnu::always_inline]] inline native_packet transform(native_packet source, const shuffle& p) {
#if defined(__aarch64__)
    return {native_detail::apply16<0>(source, p), native_detail::apply16<1>(source, p),
            native_detail::apply16<2>(source, p), native_detail::apply16<3>(source, p)};
#elif defined(__AVX512VBMI__)
    auto result = _mm512_permutexvar_epi8(_mm512_loadu_si512(p.index.data()), source);
    if (p.shifting) result = _mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.bit_index.data()), result);
    if (p.masking) result = _mm512_and_si512(result, _mm512_loadu_si512(p.mask.data()));
    return result;
#else
    return {native_detail::apply32<0, Left>(source, p), native_detail::apply32<1, Left>(source, p)};
#endif
}
[[gnu::always_inline]] inline native_packet read_body(const read_plan& p, const byte* row) {
    using namespace native_detail;
    return transform<false>(join(load_chunk<0>(p, row), load_chunk<1>(p, row), load_chunk<2>(p, row), load_chunk<3>(p, row)), p.operation);
}

// Non-template endpoint, sharing the exact body with native inline callers.
TUPLE_CC native_packet read_native(const read_plan&, const byte* row);
using native_reader = native_packet (TUPLE_CC *)(const read_plan&, const byte*);

enum class mutation_status { ok, value, native_shape };
TUPLE_CC mutation_status write_native(const write_plan&, byte* row, native_packet input, std::uint64_t& effects);

} // namespace tuple_runtime
