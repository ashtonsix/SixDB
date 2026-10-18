#pragma once
#include "bits.h"
#include <ikea/detail/filter/compare.h>
#include <array>
// Authored whole-bucket lowering for two admitted Local E=30 tiles. Both
// 32-byte loads (including the overlapping second one) stay within the owner's
// initialized 64-byte bucket. This deliberately has a stronger read-window
// contract than an individual SeriesPack leaf; no neighbouring bucket is read.
// Match sixteen E=30 stored quotients in a 64-byte primary bucket. Two
// eight-row SeriesPack tiles occupy bytes 0..59; empty flags occupy 60..61,
// byte 62 is reserved zero, and byte 63 counts overflow. Loads may inspect the
// initialized bucket. The caller applies slot liveness after physical matching.
namespace ikea::hashtable::detail {
template <unsigned R> struct plane_queries {
    static_assert(R > 0 && R < 8);
    inline static constexpr auto patterns = [] {
        std::array<word, 1u << R> out{};
        for (unsigned q = 0; q < out.size(); ++q)
            for (unsigned bit = 0; bit < R; ++bit)
                if (q & (1u << bit))
                    out[q] |= word{255} << (bit * 8);
        return out;
    }();
};
#if defined(__aarch64__)
// One bit per four-bit field, in primary row order. Four overlapping loads
// stay within bytes 0..57 of the admitted 64-byte owner. Unlike a standalone
// body leaf this lowering may read initialized residual bytes, but ignores them.
inline word body30_neon_nibbles(const byte* p, word q) {
    alignas(16) static constexpr std::array<byte, 16> order{0, 1, 2, 255, 3, 4,  5,  255,
                                                            6, 7, 8, 255, 9, 10, 11, 255};
    const auto indices = vld1q_u8(order.data());
    const auto query = vdupq_n_u32(unsigned(q >> 6));
    const auto a = vceqq_u32(vreinterpretq_u32_u8(vqtbl1q_u8(vld1q_u8(p), indices)), query);
    const auto b = vceqq_u32(vreinterpretq_u32_u8(vqtbl1q_u8(vld1q_u8(p + 12), indices)), query);
    const auto c = vceqq_u32(vreinterpretq_u32_u8(vqtbl1q_u8(vld1q_u8(p + 30), indices)), query);
    const auto d = vceqq_u32(vreinterpretq_u32_u8(vqtbl1q_u8(vld1q_u8(p + 42), indices)), query);
    const auto ab = vuzp1q_u16(vreinterpretq_u16_u32(a), vreinterpretq_u16_u32(b));
    const auto cd = vuzp1q_u16(vreinterpretq_u16_u32(c), vreinterpretq_u16_u32(d));
    const auto lanes = vuzp1q_u8(vreinterpretq_u8_u16(ab), vreinterpretq_u8_u16(cd));
    const auto nibbles = vshrn_n_u16(vreinterpretq_u16_u8(lanes), 4);
    return vget_lane_u64(vreinterpret_u64_u8(nibbles), 0) & 0x1111111111111111ULL;
}
#endif
// Six admitted planes, with two additional initialized owner bytes. Truncate
// before the second shift: this removes every contribution from those bytes.
// On AArch64 the two AND-with-shift instructions avoid a scalar reduction tree.
inline unsigned six_plane_mask(word raw, word query) {
    const word equal = raw ^ ~query, three = equal & (equal >> 16);
    const unsigned alternating = unsigned(three) & (unsigned(three) >> 16);
    return alternating & (alternating >> 8);
}
#if defined(__AVX512VBMI__) && defined(__GFNI__)
struct parts30 {
    __m512i packed;
    unsigned body;
};
template <bool ZeroPadding = false> inline parts30 prepare30(const byte* p, word q) {
    alignas(64) static constexpr auto order = [] {
        std::array<byte, 64> a{};
        for (unsigned r = 0; r < 16; ++r) {
            for (unsigned j = 0; j < 3; ++j)
                a[4 * r + j] = (r / 8) * 30 + (r % 8) * 3 + j;
            if constexpr (ZeroPadding)
                a[4 * r + 3] = 62;
        }
        return a;
    }();
    const auto packed = _mm512_loadu_si512(p);
    const auto body = [&] {
        if constexpr (ZeroPadding)
            return _mm512_permutexvar_epi8(_mm512_load_si512(order.data()), packed);
        else
            return _mm512_maskz_permutexvar_epi8(0x7777777777777777ULL,
                                                 _mm512_load_si512(order.data()), packed);
    }();
    return {packed, unsigned(_mm512_cmpeq_epi32_mask(body, _mm512_set1_epi32(unsigned(q >> 6))))};
}
template <bool ZeroPadding = false> inline unsigned residual30(__m512i packed, word q) {
    alignas(64) static constexpr auto order = [] {
        std::array<byte, 64> a{};
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned j = 0; j < 8; ++j)
                a[t * 8 + j] = ZeroPadding && j < 2 ? 62 : t * 30 + 31 - j;
        return a;
    }();
    const auto columns =
        _mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_load_si512(order.data()), packed));
    const auto tails =
        _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL), columns, 0);
    const auto bounded = [&] {
        if constexpr (ZeroPadding)
            return tails;
        else
            return _mm_and_si128(tails, _mm_set1_epi8(63));
    }();
    return _mm_cmpeq_epi8_mask(bounded, _mm_set1_epi8(q & 63));
}
#endif
inline unsigned body30_dword(const byte* p, word q) {
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
    const auto order = _mm256_setr_epi32(0, 1, 2, 0, 3, 4, 5, 0);
    const auto shuffle = _mm256_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1, 0,
                                          1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
    const auto query = _mm256_set1_epi32(unsigned(q >> 6));
    unsigned result = 0;
    each<2>([&](auto tile) {
        const auto packed = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + tile * 30));
        const auto rows = _mm256_shuffle_epi8(_mm256_permutevar8x32_epi32(packed, order), shuffle);
        result |= unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(rows, query))))
                  << (tile * 8);
    });
    return result;
#else
    unsigned result = 0;
    for (unsigned row = 0; row < 16; ++row)
        result |= unsigned(load<3>(p + (row / 8) * 30 + (row % 8) * 3) == (q >> 6)) << row;
    return result;
#endif
}
inline unsigned body30(const byte* p, word q) {
#if defined(__AVX512VBMI__)
    alignas(64) static constexpr auto order = [] {
        std::array<byte, 64> a{};
        for (unsigned r = 0; r < 16; ++r)
            for (unsigned j = 0; j < 3; ++j)
                a[4 * r + j] = (r / 8) * 30 + (r % 8) * 3 + j;
        return a;
    }();
    const auto packed = _mm512_loadu_si512(p);
    const auto rows = _mm512_maskz_permutexvar_epi8(0x7777777777777777ULL,
                                                    _mm512_load_si512(order.data()), packed);
    return _mm512_cmpeq_epi32_mask(rows, _mm512_set1_epi32(unsigned(q >> 6)));
#elif defined(__AVX2__) && defined(__BMI2__)
    alignas(32) static constexpr auto order = [] {
        std::array<byte, 32> a{};
        for (unsigned i = 0; i < 32; ++i)
            a[i] = i % 3;
        return a;
    }();
    const auto query =
        _mm256_shuffle_epi8(_mm256_set1_epi32(unsigned(q >> 6)),
                            _mm256_load_si256(reinterpret_cast<const __m256i*>(order.data())));
    unsigned result = 0;
    each<2>([&](auto tile) {
        const auto equal = _mm256_cmpeq_epi8(
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + tile * 30)), query);
        const auto bits = unsigned(_mm256_movemask_epi8(equal));
        result |= _pext_u32(bits & (bits >> 1) & (bits >> 2), 0x249249) << (tile * 8);
    });
    return result;
#elif defined(__AVX2__)
    return body30_dword(p, q);
#elif defined(__aarch64__)
    unsigned result = 0;
    const auto a = vdup_n_u8(q >> 6), b = vdup_n_u8(q >> 14), c = vdup_n_u8(q >> 22);
    each<2>([&](auto tile) {
        const auto body = vld3_u8(p + tile * 30);
        const auto equal = vand_u8(vand_u8(vceq_u8(body.val[0], a), vceq_u8(body.val[1], b)),
                                   vceq_u8(body.val[2], c));
        result |= unsigned(ikea::detail::filter::byte_bits(vcombine_u8(equal, vdup_n_u8(0))))
                  << (tile * 8);
    });
    return result;
#else
    unsigned result = 0;
    for (unsigned row = 0; row < 16; ++row)
        result |= unsigned(load<3>(p + (row / 8) * 30 + (row % 8) * 3) == (q >> 6)) << row;
    return result;
#endif
}
template <bool Gfni, bool ZeroPadding = false> inline unsigned exact30(const byte* p, word q) {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    if constexpr (Gfni) {
        // ZeroPadding additionally requires the owner's unused byte 62 to be
        // zero. Its two complete 30-byte tiles and flags occupy bytes 0..61;
        // the overflow count is byte 63. No mutation may reuse byte 62.
        const auto parts = prepare30<ZeroPadding>(p, q);
        return parts.body & residual30<ZeroPadding>(parts.packed, q);
    }
#endif
    const auto body = body30(p, q);
    unsigned residual = 0;
#if defined(__GFNI__) && defined(__SSSE3__)
    if constexpr (Gfni) {
        const auto lo = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p + 24));
        const auto hi = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p + 54));
        const auto columns =
            _mm_shuffle_epi8(_mm_unpacklo_epi64(lo, hi),
                             _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
        const auto rows =
            _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL), columns, 0);
        const auto masked = _mm_and_si128(rows, _mm_set1_epi8(63));
        residual = unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(masked, _mm_set1_epi8(q & 63))));
    } else
#endif
    {
        const auto query = plane_queries<6>::patterns[q & 63];
        each<2>([&](auto tile) {
            residual |= six_plane_mask(load<8>(p + tile * 30 + 24), query) << (tile * 8);
        });
    }
    return body & residual;
}
} // namespace ikea::hashtable::detail
