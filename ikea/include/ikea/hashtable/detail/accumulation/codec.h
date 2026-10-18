#pragma once
#include "wide.h"
namespace ikea::hashtable::detail::accumulation {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
// Only representation operations vary with the logical SIMD lane width.
// Routing, occupancy, overflow, reduction and commit ordering do not live here.
template <class D> struct image_codec {
    static constexpr bool wide = D::encoding > 32;
    static constexpr unsigned lanes = wide ? 8 : 16, banks = (D::N + lanes - 1) / lanes;
    static constexpr bool supported =
        !D::bitmap &&
        ((D::B == 64 && D::T >= 2 && D::T <= 5 && D::encoding >= 10 && D::encoding <= 30) ||
         (D::B == 96 && D::T >= 1 && D::T <= 2 && D::encoding >= 31 && D::encoding <= 64));
    using image = std::array<__m512i, banks>;
    [[gnu::always_inline]] static image read(const byte* p) {
        if constexpr (D::B == 64)
            return read_flat<D::encoding, D::T>(p);
        else if constexpr (wide)
            return read_wide<D::encoding, D::T>(p);
        else if constexpr (D::encoding == 32)
            return {_mm512_loadu_si512(p)};
        else {
            const auto rows = read_wide<D::encoding, D::T>(p);
            return {_mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(rows[0])),
                                       _mm512_cvtepi64_epi32(rows[1]), 1)};
        }
    }
    [[gnu::always_inline]] static void write(byte* p, const image& value, word live) {
        if constexpr (D::B == 64)
            write_flat<D::encoding, D::T>(p, value, live);
        else if constexpr (wide)
            write_wide<D::encoding, D::T>(p, value, live);
        else if constexpr (D::encoding == 32) {
            _mm512_storeu_si512(p, value[0]);
            ht::store<2>(p + 64, ~live);
        } else {
            wide_image<D::T> rows{_mm512_cvtepu32_epi64(_mm512_castsi512_si256(value[0])),
                                  _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(value[0], 1))};
            write_wide<D::encoding, D::T>(p, rows, live);
        }
    }
    [[gnu::always_inline]] static __m512i query(word q) {
        if constexpr (wide)
            return _mm512_set1_epi64(q);
        else
            return _mm512_set1_epi32(unsigned(q));
    }
    [[gnu::always_inline]] static word match(__m512i keys, __m512i q) {
        if constexpr (wide)
            return _mm512_cmpeq_epi64_mask(keys, q);
        else
            return _mm512_cmpeq_epi32_mask(keys, q);
    }
    [[gnu::always_inline]] static __m512i insert(__m512i keys, unsigned mask, word q) {
        if constexpr (wide)
            return _mm512_mask_set1_epi64(keys, mask, q);
        else
            return _mm512_mask_set1_epi32(keys, mask, unsigned(q));
    }
};
// Source decoding depends on its representation, independently of destination
// width. Widen 32-bit rows once before the destination loads; native words can
// be consumed directly without a temporary expansion.
template <class S> [[gnu::always_inline]] void prepare_source(const byte* p, word* decoded) {
    if constexpr (S::encoding == 64)
        return;
    else if constexpr (S::encoding == 32) {
        ht::each<S::T>([&](auto tile) __attribute__((always_inline)) {
            const auto rows = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + tile * 32));
            _mm512_storeu_si512(decoded + tile * 8, _mm512_cvtepu32_epi64(rows));
        });
    } else if constexpr (S::B == 64)
        decode_flat_source<S>(p, decoded);
    else {
        const auto source = read_wide<S::encoding, S::T>(p);
        ht::each<S::T>([&](auto tile) __attribute__((always_inline)) {
            _mm512_storeu_si512(decoded + tile * 8, source[tile]);
        });
    }
}
template <class S>
[[gnu::always_inline]] word source_quotient(const byte* p, const word* decoded, unsigned row) {
    if constexpr (S::encoding == 64)
        return ht::point<S::encoding>(p, row, S::N);
    else
        return decoded[row];
}
#endif
} // namespace ikea::hashtable::detail::accumulation
