#pragma once
#include "bits.h"
#include <ikea/detail/filter/compare.h>
#include <array>
// Owner-specific B64 load contract, not a widened SeriesPack leaf contract.
// The caller protects every loaded byte and filters zero-query liveness.
namespace ikea::hashtable::detail {
#if defined(__aarch64__)
template <unsigned E>
inline std::array<uint8x16_t, 2> small_body_comparisons_neon(const std::uint8_t* p, word query) {
    static_assert(E > 8 && E < 16 && 4 * E + 5 <= 64);
    const auto needle = vdupq_n_u8(std::uint8_t(query >> (E % 8)));
    return {vceqq_u8(vcombine_u8(vld1_u8(p), vld1_u8(p + E)), needle),
            vceqq_u8(vcombine_u8(vld1_u8(p + 2 * E), vld1_u8(p + 3 * E)), needle)};
}
inline unsigned first_small_body_neon(const std::array<uint8x16_t, 2>& equality) {
    const uint8x16_t lo{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const uint8x16_t hi{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    return vminvq_u8(vminq_u8(vornq_u8(lo, equality[0]), vornq_u8(hi, equality[1])));
}
inline word small_body_mask_neon(const std::array<uint8x16_t, 2>& equality) {
    const auto weights = vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
    auto reduced = vpaddq_u8(vandq_u8(equality[0], weights), vandq_u8(equality[1], weights));
    reduced = vpaddq_u8(reduced, reduced);
    reduced = vpaddq_u8(reduced, reduced);
    return vgetq_lane_u64(vreinterpretq_u64_u8(reduced), 0) & mask<32>;
}
// Compress all byte-body comparisons together, rather than paying one
// SIMD-to-scalar reduction for each eight-row tile. Each reduction byte sums
// distinct powers of two, so pairwise byte addition cannot carry across rows.
template <unsigned E, unsigned T> inline word small_body_matches_neon(const std::uint8_t* p, word query) {
    static_assert(E > 8 && E < 16 && T >= 3 && T <= 5 && T * E + T + 1 <= 64);
    const auto needle = vdupq_n_u8(std::uint8_t(query >> (E % 8)));
    const auto weights = vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ULL));
    auto pair = [&]<unsigned Tile>() {
        const auto lo = vld1_u8(p + Tile * E);
        const auto hi = [&] {
            if constexpr (Tile + 1 < T)
                return vld1_u8(p + (Tile + 1) * E);
            else
                return vdup_n_u8(0);
        }();
        return vandq_u8(vceqq_u8(vcombine_u8(lo, hi), needle), weights);
    };
    const auto ab = vpaddq_u8(pair.template operator()<0>(), pair.template operator()<2>());
    const auto middle = [&] {
        if constexpr (T == 5)
            return vpaddq_u8(ab, vpaddq_u8(pair.template operator()<4>(), vdupq_n_u8(0)));
        else
            return vpaddq_u8(ab, ab);
    }();
    const auto reduced = vpaddq_u8(middle, middle);
    return vgetq_lane_u64(vreinterpretq_u64_u8(reduced), 0) & mask<T * 8>;
}
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned E, unsigned T> inline word exact_small_bucket(const std::uint8_t* p, word query) {
    constexpr unsigned Q = E / 8, R = E % 8, N = T * 8;
    static_assert(Q >= 1 && Q <= 2 && T * E + T + 1 <= 64 && N * Q <= 64);
    alignas(64) static constexpr auto bodies = [] {
        std::array<std::uint8_t, 64> order{};
        for (unsigned row = 0; row < N; ++row)
            for (unsigned j = 0; j < Q; ++j)
                order[row * Q + j] = std::uint8_t((row / 8) * E + (row % 8) * Q + j);
        return order;
    }();
    const auto packed = _mm512_loadu_si512(p);
    const auto body = _mm512_permutexvar_epi8(_mm512_load_si512(bodies.data()), packed);
    const word body_mask = [&] {
        if constexpr (Q == 1)
            return word(_mm512_mask_cmpeq_epi8_mask(__mmask64(mask<N>), body,
                                                    _mm512_set1_epi8(query >> R)));
        else
            return word(_mm512_mask_cmpeq_epi16_mask(__mmask32(mask<N>), body,
                                                     _mm512_set1_epi16(query >> R)));
    }();
    if constexpr (!R)
        return body_mask;
    else {
        alignas(64) static constexpr auto planes = [] {
            std::array<std::uint8_t, 64> order{};
            for (unsigned tile = 0; tile < T; ++tile)
                for (unsigned bit = 0; bit < R; ++bit)
                    order[tile * 8 + 7 - bit] = std::uint8_t(tile * E + 8 * Q + bit);
            return order;
        }();
        constexpr word active = [] {
            word bits = 0;
            for (unsigned tile = 0; tile < T; ++tile)
                for (unsigned bit = 0; bit < R; ++bit)
                    bits |= word{1} << (tile * 8 + 7 - bit);
            return bits;
        }();
        // VPERMB wraps indices; absent matrix bytes must be masked to zero.
        const auto matrix = _mm512_maskz_permutexvar_epi8(__mmask64(active),
                                                          _mm512_load_si512(planes.data()), packed);
        const auto residual =
            _mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL), matrix, 0);
        return word(_mm512_mask_cmpeq_epi8_mask(__mmask64(body_mask), residual,
                                                _mm512_set1_epi8(query & mask<R>)));
    }
}
#endif
#if defined(__AVX2__) && defined(__BMI2__)
#if defined(__GFNI__)
// Two exact eight-row tiles per AVX2 register. Each 16-byte lane loads its
// own tile origin, so PSHUFB never needs to cross a lane boundary. GFNI
// transposes the admitted residual planes; PEXT packs the live row coordinates.
template <unsigned E, unsigned T> inline word exact_small_bucket_avx2(const std::uint8_t* p, word query) {
    constexpr unsigned R = E % 8;
    static_assert(E > 8 && E < 16 && T >= 3 && T <= 5 && (T - 1) * E + 16 <= 64);
    alignas(32) static constexpr auto planes = [] {
        std::array<std::uint8_t, 32> order{};
        order.fill(0x80);
        for (unsigned lane = 0; lane < 2; ++lane)
            for (unsigned bit = 0; bit < R; ++bit)
                order[lane * 16 + 7 - bit] = std::uint8_t(8 + bit);
        return order;
    }();
    if constexpr (T == 4) {
        const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + E));
        const auto c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 2 * E));
        const auto d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 3 * E));
        const auto body = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_unpacklo_epi64(a, b)),
                                                  _mm_unpacklo_epi64(c, d), 1);
        const auto tails = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_unpackhi_epi64(a, b)),
                                                   _mm_unpackhi_epi64(c, d), 1);
        alignas(32) static constexpr auto tail_order = [] {
            std::array<std::uint8_t, 32> order{};
            order.fill(0x80);
            for (unsigned tile = 0; tile < 4; ++tile)
                for (unsigned bit = 0; bit < R; ++bit)
                    order[tile * 8 + 7 - bit] = std::uint8_t((tile % 2) * 8 + bit);
            return order;
        }();
        const auto matrix = _mm256_shuffle_epi8(
            tails, _mm256_load_si256(reinterpret_cast<const __m256i*>(tail_order.data())));
        const auto residual =
            _mm256_gf2p8affine_epi64_epi8(_mm256_set1_epi64x(0x8040201008040201ULL), matrix, 0);
        return unsigned(_mm256_movemask_epi8(
            _mm256_and_si256(_mm256_cmpeq_epi8(body, _mm256_set1_epi8(query >> R)),
                             _mm256_cmpeq_epi8(residual, _mm256_set1_epi8(query & mask<R>)))));
    }
    const auto shuffle = _mm256_load_si256(reinterpret_cast<const __m256i*>(planes.data()));
    const auto basis = _mm256_set1_epi64x(0x8040201008040201ULL);
    const auto body_query = _mm256_set1_epi8(query >> R),
               tail_query = _mm256_set1_epi8(query & mask<R>);
    auto pair = [&]<unsigned Tile>() -> word {
        const auto lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + Tile * E));
        if constexpr (Tile + 1 < T) {
            const auto packed = _mm256_inserti128_si256(
                _mm256_castsi128_si256(lo),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + (Tile + 1) * E)), 1);
            const auto residual =
                _mm256_gf2p8affine_epi64_epi8(basis, _mm256_shuffle_epi8(packed, shuffle), 0);
            const auto exact = _mm256_and_si256(_mm256_cmpeq_epi8(packed, body_query),
                                                _mm256_cmpeq_epi8(residual, tail_query));
            return _pext_u32(unsigned(_mm256_movemask_epi8(exact)), 0x00ff00ff);
        } else {
            const auto residual = _mm_gf2p8affine_epi64_epi8(
                _mm256_castsi256_si128(basis),
                _mm_shuffle_epi8(lo, _mm256_castsi256_si128(shuffle)), 0);
            const auto exact =
                _mm_and_si128(_mm_cmpeq_epi8(lo, _mm256_castsi256_si128(body_query)),
                              _mm_cmpeq_epi8(residual, _mm256_castsi256_si128(tail_query)));
            return unsigned(_mm_movemask_epi8(exact)) & 255;
        }
    };
    word result = pair.template operator()<0>() | (pair.template operator()<2>() << 16);
    if constexpr (T == 5)
        result |= pair.template operator()<4>() << 32;
    return result;
}
#endif
// First body match, with exact residual verification by the caller. The body
// positions stay sparse in storage; PEXT restores the original row order.
template <unsigned E, unsigned T> inline word small_body_matches(const std::uint8_t* p, word query) {
    static_assert(E > 8 && E < 16 && T * E + T + 1 <= 64);
    constexpr word positions = [] {
        word bits = 0;
        for (unsigned tile = 0; tile < T; ++tile)
            bits |= word{255} << (tile * E);
        return bits;
    }();
    const auto needle = _mm256_set1_epi8(query >> (E % 8));
    const auto lo =
        _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), needle);
    const auto hi =
        _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32)), needle);
    const word sparse =
        unsigned(_mm256_movemask_epi8(lo)) | (word(unsigned(_mm256_movemask_epi8(hi))) << 32);
    return _pext_u64(sparse, positions);
}
#endif
} // namespace ikea::hashtable::detail
