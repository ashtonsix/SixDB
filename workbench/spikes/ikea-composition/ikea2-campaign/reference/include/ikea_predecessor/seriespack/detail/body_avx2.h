#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
namespace ikea_predecessor::seriespack::detail::avx2 {

namespace body_detail {

template<unsigned Q, unsigned LaneBytes>
inline constexpr auto decode_indices = [] {
    constexpr unsigned half_bytes = 16 / LaneBytes * Q;
    constexpr unsigned window_bytes = half_bytes <= 8 ? 8 : 16;
    std::array<std::uint8_t, 32> indices{};
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned offset = i < 16 ? 0 : window_bytes - half_bytes;
        indices[i] = i % LaneBytes < Q
            ? std::uint8_t(offset + (i % 16) / LaneBytes * Q + i % LaneBytes)
            : 0x80;
    }
    return indices;
}();

template<unsigned Q, unsigned LaneBytes>
inline constexpr auto encode_indices = [] {
    constexpr unsigned half_bytes = 16 / LaneBytes * Q;
    std::array<std::uint8_t, 32> indices{};
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned packed = i % 16;
        indices[i] = packed < half_bytes
            ? std::uint8_t(packed / Q * LaneBytes + packed % Q)
            : 0x80;
    }
    return indices;
}();

template<unsigned Bytes>
[[gnu::always_inline]] inline __m128i load_window(const std::uint8_t* p) noexcept {
    static_assert(Bytes == 8 || Bytes == 16);
    if constexpr (Bytes == 8)
        return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
    else
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
}

template<unsigned Bytes>
[[gnu::always_inline]] inline void store_window(std::uint8_t* p, __m128i v) noexcept {
    static_assert(Bytes == 8 || Bytes == 16);
    if constexpr (Bytes == 8)
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p), v);
    else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v);
}

} // namespace body_detail

// N = 32 / LaneBytes consecutive little-endian AoS values, each occupying Q
// bytes. The returned unsigned lanes have their upper bytes zeroed. The only
// readable extent is [p, p + N*Q); alignment and a readable suffix are not
// required. Q == 0 does not access p. These are trusted, by-value native bodies.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline __m256i decode_body(const std::uint8_t* p) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes);
    if constexpr (Q == 0) {
        return _mm256_setzero_si256();
    } else if constexpr (Q == LaneBytes) {
        return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        return _mm256_cvtepu8_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 1 && LaneBytes == 4) {
        return _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 1 && LaneBytes == 8) {
        std::uint32_t bytes;
        std::memcpy(&bytes, p, sizeof bytes);
        return _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(static_cast<int>(bytes)));
    } else if constexpr (Q == 2 && LaneBytes == 4) {
        return _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 2 && LaneBytes == 8) {
        return _mm256_cvtepu16_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 4 && LaneBytes == 8) {
        return _mm256_cvtepu32_epi64(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    } else {
        constexpr unsigned half_bytes = 16 / LaneBytes * Q;
        constexpr unsigned window_bytes = half_bytes <= 8 ? 8 : 16;
        // Both windows are wholly inside the exact input. Each half's shuffle
        // selects its own values even where the windows overlap.
        const auto first = body_detail::load_window<window_bytes>(p);
        const auto last = body_detail::load_window<window_bytes>(p + 2 * half_bytes - window_bytes);
        const auto joined = _mm256_inserti128_si256(_mm256_castsi128_si256(first), last, 1);
        const auto indices = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
            body_detail::decode_indices<Q, LaneBytes>.data()));
        return _mm256_shuffle_epi8(joined, indices);
    }
}

// Store each lane's low Q bytes in the same AoS order. Higher input bytes are
// ignored, not saturated. The actual write extent is exactly N*Q bytes, with no
// destination reads. Overlapping stores agree on every shared byte.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline void encode_body(std::uint8_t* p, __m256i values) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes);
    if constexpr (Q == 0) {
        return;
    } else if constexpr (Q == LaneBytes) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), values);
    } else {
        constexpr unsigned half_bytes = 16 / LaneBytes * Q;
        const auto indices = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
            body_detail::encode_indices<Q, LaneBytes>.data()));
        const auto compact = _mm256_shuffle_epi8(values, indices);
        const auto first = _mm256_castsi256_si128(compact);
        const auto last = _mm256_extracti128_si256(compact, 1);
        if constexpr (half_bytes == 2) {
            const auto joined = _mm_unpacklo_epi16(first, last);
            const auto bytes = static_cast<std::uint32_t>(_mm_cvtsi128_si32(joined));
            std::memcpy(p, &bytes, sizeof bytes);
        } else if constexpr (half_bytes == 4) {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm_unpacklo_epi32(first, last));
        } else if constexpr (half_bytes == 8) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm_unpacklo_epi64(first, last));
        } else {
            constexpr unsigned window_bytes = half_bytes <= 8 ? 8 : 16;
            const auto left = _mm_or_si128(first, _mm_slli_si128(last, half_bytes));
            const auto right = _mm_or_si128(
                _mm_srli_si128(first, 2 * half_bytes - window_bytes),
                _mm_slli_si128(last, window_bytes - half_bytes));
            body_detail::store_window<window_bytes>(p, left);
            body_detail::store_window<window_bytes>(p + 2 * half_bytes - window_bytes, right);
        }
    }
}

// A local packet can supply eight values even when the native register holds
// more. Only that prefix is admitted; high result lanes are defined as zero.
// Full native grains and an empty prefix use the same spelling.
template<unsigned Q, unsigned LaneBytes, unsigned Count>
[[gnu::always_inline]] inline __m256i decode_body_prefix(const std::uint8_t* p) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes && Count <= 32 / LaneBytes);
    static_assert(Count == 0 || Count == 8 || Count == 32 / LaneBytes);
    if constexpr (Count == 0 || Q == 0) {
        return _mm256_setzero_si256();
    } else if constexpr (Count == 32 / LaneBytes) {
        return decode_body<Q, LaneBytes>(p);
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        return _mm256_cvtepu8_epi16(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else if constexpr (Q == 1) {
        return _mm256_zextsi128_si256(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
    } else {
        static_assert(Q == 2 && LaneBytes == 2);
        return _mm256_zextsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
    }
}

template<unsigned Q, unsigned LaneBytes, unsigned Count>
[[gnu::always_inline]] inline void encode_body_prefix(std::uint8_t* p, __m256i values) noexcept {
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    static_assert(Q <= LaneBytes && Count <= 32 / LaneBytes);
    static_assert(Count == 0 || Count == 8 || Count == 32 / LaneBytes);
    if constexpr (Count == 0 || Q == 0) {
        return;
    } else if constexpr (Count == 32 / LaneBytes) {
        encode_body<Q, LaneBytes>(p, values);
    } else if constexpr (Q == 1 && LaneBytes == 2) {
        const auto indices = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
            body_detail::encode_indices<1, 2>.data()));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p),
            _mm_shuffle_epi8(_mm256_castsi256_si128(values), indices));
    } else if constexpr (Q == 1) {
        _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm256_castsi256_si128(values));
    } else {
        static_assert(Q == 2 && LaneBytes == 2);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), _mm256_castsi256_si128(values));
    }
}

} // namespace ikea_predecessor::seriespack::detail::avx2
#endif
