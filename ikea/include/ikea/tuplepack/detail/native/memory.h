#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/native/types.h>
#include <algorithm>
#include <cstring>
#include <type_traits>
#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native::native_detail {
// Exact bounded scalar loads assemble a short final source chunk in registers.
// Neither an adjacent tuple nor implicit allocation padding may be read.
[[gnu::always_inline]] inline std::uint64_t tail8(const byte* p, unsigned n) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    if (n & 4) {
        std::uint32_t x;
        std::memcpy(&x, p, 4);
        result = x;
        p += 4;
        shift = 32;
    }
    if (n & 2) {
        std::uint16_t x;
        std::memcpy(&x, p, 2);
        result |= std::uint64_t(x) << shift;
        p += 2;
        shift += 16;
    }
    if (n & 1)
        result |= std::uint64_t(*p) << shift;
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
    } else
        low = tail8(p, n);
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
    if (n & 4) {
        auto v = std::uint32_t(value);
        std::memcpy(p, &v, 4);
        p += 4;
        value >>= 32;
    }
    if (n & 2) {
        auto v = std::uint16_t(value);
        std::memcpy(p, &v, 2);
        p += 2;
        value >>= 16;
    }
    if (n & 1)
        *p = byte(value);
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
    if (n >= 8) {
        std::memcpy(p, &low, 8);
        store_tail8(p + 8, n - 8, high);
    } else
        store_tail8(p, n, low);
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
template <unsigned I>
[[gnu::always_inline]] inline vector16 load_chunk(const detail::read64& p, const byte* row) {
    if (I >= p.count)
        return zero16();
    return load16(row + p.chunks[I].offset, p.chunks[I].bytes);
}
template <unsigned I> [[gnu::always_inline]] inline vector16 split(native_packet p) {
#if defined(__aarch64__)
    if constexpr (I == 0)
        return p.a;
    if constexpr (I == 1)
        return p.b;
    if constexpr (I == 2)
        return p.c;
    return p.d;
#elif defined(__AVX512VBMI__)
    return _mm512_extracti32x4_epi32(p, I);
#else
    return _mm256_extracti128_si256(I < 2 ? p.a : p.b, I % 2);
#endif
}
template <unsigned I>
[[gnu::always_inline]] inline vector16 row_part(const byte* row, unsigned bytes) {
    if (bytes <= I * 16)
        return zero16();
    return load16(row + I * 16, std::min(16u, bytes - I * 16));
}
template <unsigned I>
[[gnu::always_inline]] inline void store_part(byte* row, unsigned bytes, native_packet value) {
    if (bytes > I * 16)
        store16(row + I * 16, std::min(16u, bytes - I * 16), split<I>(value));
}

} // namespace ikea::tuplepack::native::native_detail
#endif
