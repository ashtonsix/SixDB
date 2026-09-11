#pragma once

#include <array>
#include <cstdint>

#if defined(__AVX512BW__)
#include <immintrin.h>
namespace ikea_predecessor::seriespack::detail::avx512 {

namespace body_detail {

template<unsigned Q, unsigned LaneBytes>
inline constexpr auto decode_indices = [] {
    constexpr unsigned input_bytes = 64 / LaneBytes * Q;
    std::array<std::uint8_t, 64> indices{};
    for (unsigned i = 0; i < 64; ++i)
        indices[i] = i % LaneBytes < Q
            ? std::uint8_t(i / LaneBytes * Q + i % LaneBytes)
            : std::uint8_t(input_bytes); // A zero byte supplied by the masked load.
    return indices;
}();

template<unsigned Q, unsigned LaneBytes>
inline constexpr auto encode_indices = [] {
    std::array<std::uint8_t, 64> indices{};
    for (unsigned i = 0; i < 64 / LaneBytes * Q; ++i)
        indices[i] = std::uint8_t(i / Q * LaneBytes + i % Q);
    return indices;
}();

// Each 64-byte output window can draw from at most two consecutive source
// registers. The last window is 32 bytes and draws only from register four.
template<unsigned Window>
inline constexpr auto region32_7_indices = [] {
    static_assert(Window < 4);
    std::array<std::uint8_t, 64> indices{};
    for (unsigned i = 0; i < (Window == 3 ? 32u : 64u); ++i) {
        const unsigned packed_byte = Window * 64 + i;
        indices[i] = std::uint8_t(packed_byte / 7 * 8 + packed_byte % 7 - Window * 64);
    }
    return indices;
}();

} // namespace body_detail

// N = 64 / LaneBytes consecutive little-endian AoS values, each occupying Q
// bytes. The returned unsigned lanes have their upper bytes zeroed. Reads are
// confined to [p, p + N*Q), without alignment or readable-suffix requirements.
// Q == 0 accesses no storage. Direct widening needs AVX-512F/BW; irregular
// byte widths additionally need VBMI, rather than silently using scalar work.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline __m512i decode_body(const std::uint8_t* p) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes);
    if constexpr (Q == 0) {
        return _mm512_setzero_si512();
    } else if constexpr (Q == LaneBytes) {
        return _mm512_loadu_si512(p);
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        return _mm512_cvtepu8_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
    } else if constexpr (Q == 1 && LaneBytes == 4) {
        return _mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 1 && LaneBytes == 8) {
        return _mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 2 && LaneBytes == 4) {
        return _mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
    } else if constexpr (Q == 2 && LaneBytes == 8) {
        return _mm512_cvtepu16_epi64(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 4 && LaneBytes == 8) {
        return _mm512_cvtepu32_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
    } else {
#if defined(__AVX512VBMI__)
        constexpr auto mask = (__mmask64{1} << (64 / LaneBytes * Q)) - 1;
        const auto bytes = _mm512_maskz_loadu_epi8(mask, p);
        const auto indices = _mm512_loadu_si512(body_detail::decode_indices<Q, LaneBytes>.data());
        return _mm512_permutexvar_epi8(indices, bytes);
#else
        static_assert(Q == LaneBytes, "Irregular AVX-512 body expansion requires AVX-512VBMI");
#endif
    }
}

// Store each lane's low Q bytes, ignoring higher input bytes. The actual write
// extent is exactly N*Q bytes; the body neither reads nor touches a suffix.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline void encode_body(std::uint8_t* p, __m512i values) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes);
    if constexpr (Q == 0) {
        return;
    } else if constexpr (Q == LaneBytes) {
        _mm512_storeu_si512(p, values);
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_cvtepi16_epi8(values));
    } else if constexpr (Q == 1 && LaneBytes == 4) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm512_cvtepi32_epi8(values));
    } else if constexpr (Q == 1 && LaneBytes == 8) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm512_cvtepi64_epi8(values));
    } else if constexpr (Q == 2 && LaneBytes == 4) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_cvtepi32_epi16(values));
    } else if constexpr (Q == 2 && LaneBytes == 8) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm512_cvtepi64_epi16(values));
    } else if constexpr (Q == 4 && LaneBytes == 8) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_cvtepi64_epi32(values));
    } else {
#if defined(__AVX512VBMI__)
        constexpr auto mask = (__mmask64{1} << (64 / LaneBytes * Q)) - 1;
        const auto indices = _mm512_loadu_si512(body_detail::encode_indices<Q, LaneBytes>.data());
        _mm512_mask_storeu_epi8(p, mask, _mm512_permutexvar_epi8(indices, values));
#else
        static_assert(Q == LaneBytes, "Irregular AVX-512 body packing requires AVX-512VBMI");
#endif
    }
}

// Exact eight-value local prefix, or a complete native grain. No masked-off
// address authorizes a logical value; the returned high lanes are zero.
template<unsigned Q, unsigned LaneBytes, unsigned Count>
[[gnu::always_inline]] inline __m512i decode_body_prefix(const std::uint8_t* p) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes && Count <= 64 / LaneBytes);
    static_assert(Count == 0 || Count == 8 || Count == 64 / LaneBytes);
    if constexpr (Count == 0 || Q == 0) {
        return _mm512_setzero_si512();
    } else if constexpr (Count == 64 / LaneBytes) {
        return decode_body<Q, LaneBytes>(p);
    } else if constexpr (Q == LaneBytes) {
        if constexpr (Count * Q == 8)
            return _mm512_zextsi128_si512(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
        else if constexpr (Count * Q == 16)
            return _mm512_zextsi128_si512(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
        else
            return _mm512_zextsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        const auto low = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        return _mm512_cvtepu8_epi16(_mm256_zextsi128_si256(low));
    } else if constexpr (Q == 1 && LaneBytes == 4) {
        return _mm512_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 2 && LaneBytes == 4) {
        const auto low = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        return _mm512_cvtepu16_epi32(_mm256_zextsi128_si256(low));
    } else {
#if defined(__AVX512VBMI__)
        static_assert(Q == 3 && LaneBytes == 4);
        constexpr auto mask = (__mmask64{1} << (Count * Q)) - 1;
        const auto bytes = _mm512_maskz_loadu_epi8(mask, p);
        return _mm512_permutexvar_epi8(
            _mm512_loadu_si512(body_detail::decode_indices<Q, LaneBytes>.data()), bytes);
#else
        static_assert(Q == LaneBytes, "Irregular AVX-512 body expansion requires AVX-512VBMI");
#endif
    }
}

template<unsigned Q, unsigned LaneBytes, unsigned Count>
[[gnu::always_inline]] inline void encode_body_prefix(std::uint8_t* p, __m512i values) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes && Count <= 64 / LaneBytes);
    static_assert(Count == 0 || Count == 8 || Count == 64 / LaneBytes);
    if constexpr (Count == 0 || Q == 0) {
        return;
    } else if constexpr (Count == 64 / LaneBytes) {
        encode_body<Q, LaneBytes>(p, values);
    } else if constexpr (Q == LaneBytes) {
        if constexpr (Count * Q == 8)
            _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm512_castsi512_si128(values));
        else if constexpr (Count * Q == 16)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm512_castsi512_si128(values));
        else
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_castsi512_si256(values));
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p),
            _mm256_castsi256_si128(_mm512_cvtepi16_epi8(values)));
    } else if constexpr (Q == 1 && LaneBytes == 4) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm512_cvtepi32_epi8(values));
    } else if constexpr (Q == 2 && LaneBytes == 4) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p),
            _mm256_castsi256_si128(_mm512_cvtepi32_epi16(values)));
    } else {
#if defined(__AVX512VBMI__)
        static_assert(Q == 3 && LaneBytes == 4);
        constexpr auto mask = (__mmask64{1} << (Count * Q)) - 1;
        const auto compact = _mm512_permutexvar_epi8(
            _mm512_loadu_si512(body_detail::encode_indices<Q, LaneBytes>.data()), values);
        _mm512_mask_storeu_epi8(p, mask, compact);
#else
        static_assert(Q == LaneBytes, "Irregular AVX-512 body packing requires AVX-512VBMI");
#endif
    }
}

#if defined(__AVX512VBMI__)
// Optional dense region: four consecutive eight-value Q=7/LaneBytes=8 native
// fragments become exactly 224 bytes. The caller admits the entire contiguous
// region: unlike encode_body<7,8>, these stores cross eight-value boundaries.
// It is ineligible across stride gaps or any residual bytes between bodies.
[[gnu::always_inline]] inline void encode_body_region32_7(
    std::uint8_t* p, __m512i a, __m512i b, __m512i c, __m512i d) noexcept {
    const auto first = _mm512_permutex2var_epi8(
        a, _mm512_loadu_si512(body_detail::region32_7_indices<0>.data()), b);
    const auto second = _mm512_permutex2var_epi8(
        b, _mm512_loadu_si512(body_detail::region32_7_indices<1>.data()), c);
    const auto third = _mm512_permutex2var_epi8(
        c, _mm512_loadu_si512(body_detail::region32_7_indices<2>.data()), d);
    const auto last = _mm512_permutexvar_epi8(
        _mm512_loadu_si512(body_detail::region32_7_indices<3>.data()), d);
    _mm512_storeu_si512(p, first);
    _mm512_storeu_si512(p + 64, second);
    _mm512_storeu_si512(p + 128, third);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + 192), _mm512_castsi512_si256(last));
}
#endif

} // namespace ikea_predecessor::seriespack::detail::avx512
#endif
