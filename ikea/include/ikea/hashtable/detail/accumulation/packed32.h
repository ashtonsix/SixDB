#pragma once
#include "resolve.h"
#include <array>
namespace ikea::hashtable::detail::accumulation {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned E> [[gnu::always_inline]] __m512i read_packed(const std::uint8_t* p) {
    static_assert(E >= 24 && E <= 30);
    constexpr auto R = E % 8;
    alignas(64) static constexpr auto body_order = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned r = 0; r < 16; ++r)
            for (unsigned j = 0; j < 3; ++j)
                a[r * 4 + j] = (r / 8) * E + (r % 8) * 3 + j;
        return a;
    }();
    alignas(64) static constexpr auto tail_order = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned j = 0; j < 8; ++j)
                a[t * 8 + j] = t * E + 31 - j;
        return a;
    }();
    const auto packed = _mm512_loadu_si512(p);
    const auto body = _mm512_maskz_permutexvar_epi8(0x7777777777777777ULL,
                                                    _mm512_load_si512(body_order.data()), packed);
    const auto columns = _mm512_castsi512_si128(
        _mm512_permutexvar_epi8(_mm512_load_si512(tail_order.data()), packed));
    const auto tail = _mm_and_si128(
        _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL), columns, 0),
        _mm_set1_epi8((1u << R) - 1));
    return _mm512_or_si512(_mm512_slli_epi32(body, R), _mm512_cvtepu8_epi32(tail));
}
#endif
#if defined(__AVX512VBMI__) && defined(__GFNI__)
// Commit two complete tiles once, retaining padding and overflow metadata.
// Keys and flags remain in the admitted 64-byte key cache line.
template <unsigned E>
[[gnu::always_inline]] void write_packed_vector(std::uint8_t* p, __m512i rows, unsigned live) {
    static_assert(E >= 24 && E <= 30);
    constexpr auto R = E % 8;
    alignas(64) static constexpr auto body_order = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned r = 0; r < 8; ++r)
                for (unsigned j = 0; j < 3; ++j)
                    a[t * E + r * 3 + j] = (t * 8 + r) * 4 + j;
        return a;
    }();
    alignas(64) static constexpr auto tail_order = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned t = 0; t < 2; ++t)
            for (unsigned j = 0; j < R; ++j)
                a[t * E + 24 + j] = t * 8 + j;
        return a;
    }();
    constexpr word body_mask = 0xffffffULL | (0xffffffULL << E);
    constexpr word tail_mask = ((word{1} << R) - 1) << 24 | ((word{1} << R) - 1) << (E + 24);
    const auto body = _mm512_maskz_permutexvar_epi8(body_mask, _mm512_load_si512(body_order.data()),
                                                    _mm512_srli_epi32(rows, R));
    const auto reversed =
        _mm_shuffle_epi8(_mm512_cvtepi32_epi8(rows),
                         _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
    const auto columns =
        _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL), reversed, 0);
    const auto tail = _mm512_maskz_permutexvar_epi8(tail_mask, _mm512_load_si512(tail_order.data()),
                                                    _mm512_broadcast_i32x4(columns));
    const auto encoded =
        _mm512_mask_set1_epi16(_mm512_or_si512(body, tail), 1u << E, int(std::uint16_t(~live)));
    constexpr word changed_mask = (word{1} << (2 * E + 2)) - 1;
    const auto final = _mm512_mask_mov_epi8(_mm512_loadu_si512(p), changed_mask, encoded);
    _mm512_storeu_si512(p, final);
}
template <unsigned E>
[[gnu::always_inline]] void write_packed(std::uint8_t* p, const std::uint32_t* q, unsigned live) {
    write_packed_vector<E>(p, _mm512_loadu_si512(q), live);
}
#endif
} // namespace ikea::hashtable::detail::accumulation
