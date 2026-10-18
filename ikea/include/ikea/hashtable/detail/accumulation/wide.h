#pragma once
#include "flat.h"
namespace ikea::hashtable::detail::accumulation {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned T> using wide_image = std::array<__m512i, T>;
template <unsigned E, unsigned T> [[gnu::always_inline]] wide_image<T> read_wide(const byte* p) {
    static_assert(E >= 31 && E <= 64 && T >= 1 && T <= 2 && T * (E + 1) <= 95);
    const auto lo = _mm512_loadu_si512(p);
    if constexpr (E == 64 && T == 1)
        return {lo};
    else {
        constexpr unsigned B = E / 8, R = E % 8;
        wide_image<T> result;
        const auto hi = [&] {
            if constexpr (T * E > 64)
                return _mm512_zextsi256_si512(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 64)));
            else
                return _mm512_setzero_si512();
        }();
        const auto tails = [&] {
            if constexpr (!R)
                return _mm512_setzero_si512();
            else {
                alignas(64) static constexpr auto order = [] {
                    std::array<byte, 64> a{};
                    for (unsigned t = 0; t < T; ++t)
                        for (unsigned j = 0; j < 8; ++j)
                            a[t * 8 + j] = t * E + 8 * B + 7 - j;
                    return a;
                }();
                const auto columns =
                    _mm512_permutex2var_epi8(lo, _mm512_load_si512(order.data()), hi);
                return _mm512_and_si512(_mm512_gf2p8affine_epi64_epi8(
                                            _mm512_set1_epi64(0x8040201008040201ULL), columns, 0),
                                        _mm512_set1_epi8((1u << R) - 1));
            }
        }();
        ht::each<T>([&](auto tile) __attribute__((always_inline)) {
            constexpr unsigned Tile = decltype(tile)::value;
            alignas(64) static constexpr auto order = [] {
                std::array<byte, 64> a{};
                for (unsigned r = 0; r < 8; ++r)
                    for (unsigned j = 0; j < B; ++j)
                        a[r * 8 + j] = Tile * E + r * B + j;
                return a;
            }();
            constexpr word body_mask = [] {
                word m = 0;
                for (unsigned r = 0; r < 8; ++r)
                    for (unsigned j = 0; j < B; ++j)
                        m |= word{1} << (r * 8 + j);
                return m;
            }();
            const auto body = _mm512_maskz_mov_epi8(
                body_mask, _mm512_permutex2var_epi8(lo, _mm512_load_si512(order.data()), hi));
            const auto residual =
                _mm512_cvtepu8_epi64(_mm_srli_si128(_mm512_castsi512_si128(tails), Tile * 8));
            result[Tile] = _mm512_or_si512(_mm512_slli_epi64(body, R), residual);
        });
        return result;
    }
}
struct wide_orders {
    std::array<byte, 64> body{}, tail{}, flags{};
    word bodies = 0, tails = 0, flag_bits = 0, changed = 0;
};
template <unsigned E, unsigned T, unsigned Part> consteval wide_orders wide_encode_orders() {
    wide_orders a;
    constexpr unsigned B = E / 8, R = E % 8;
    auto mark = [&](unsigned pos, byte index, auto& order, word& mask) {
        if (pos / 64 == Part) {
            order[pos % 64] = index;
            mask |= word{1} << (pos % 64);
        }
    };
    for (unsigned t = 0; t < T; ++t) {
        for (unsigned row = 0; row < 8; ++row)
            for (unsigned j = 0; j < B; ++j)
                mark(t * E + row * B + j, byte((t * 8 + row) * 8 + j), a.body, a.bodies);
        for (unsigned j = 0; j < R; ++j)
            mark(t * E + 8 * B + j, byte(t * 8 + j), a.tail, a.tails);
        mark(T * E + t, byte(t), a.flags, a.flag_bits);
    }
    a.changed = a.bodies | a.tails | a.flag_bits;
    return a;
}
template <unsigned E, unsigned T>
[[gnu::always_inline]] void write_wide(byte* p, const wide_image<T>& keys, word live) {
    static_assert(E >= 31 && E <= 64 && T >= 1 && T <= 2 && T * (E + 1) <= 95);
    if constexpr (E == 64 && T == 1) {
        _mm512_storeu_si512(p, keys[0]);
        p[64] = byte(~live);
    } else {
        constexpr unsigned R = E % 8;
        const auto first = _mm512_srli_epi64(keys[0], R);
        const auto second = [&] {
            if constexpr (T == 2)
                return _mm512_srli_epi64(keys[1], R);
            else
                return _mm512_setzero_si512();
        }();
        const auto columns = [&] {
            if constexpr (!R)
                return _mm512_setzero_si512();
            else {
                auto rows = _mm512_cvtepi64_epi8(keys[0]);
                if constexpr (T == 2)
                    rows = _mm_unpacklo_epi64(rows, _mm512_cvtepi64_epi8(keys[1]));
                const auto reverse = _mm512_broadcast_i32x4(
                    _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
                return _mm512_gf2p8affine_epi64_epi8(
                    _mm512_set1_epi64(0x8040201008040201ULL),
                    _mm512_shuffle_epi8(_mm512_zextsi128_si512(rows), reverse), 0);
            }
        }();
        ht::each<2>([&](auto part) __attribute__((always_inline)) {
            constexpr unsigned Part = decltype(part)::value;
            alignas(64) static constexpr auto map = wide_encode_orders<E, T, Part>();
            if constexpr (map.changed) {
                const auto bodies = _mm512_maskz_mov_epi8(
                    map.bodies,
                    _mm512_permutex2var_epi8(first, _mm512_load_si512(map.body.data()), second));
                const auto tails = _mm512_maskz_permutexvar_epi8(
                    map.tails, _mm512_load_si512(map.tail.data()), columns);
                const auto encoded = _mm512_mask_permutexvar_epi8(
                    _mm512_or_si512(bodies, tails), map.flag_bits,
                    _mm512_load_si512(map.flags.data()), _mm512_set1_epi64(~live));
                const auto old = [&] {
                    if constexpr (Part == 0)
                        return _mm512_loadu_si512(p);
                    else
                        return _mm512_zextsi256_si512(
                            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 64)));
                }();
                const auto next = _mm512_mask_mov_epi8(old, map.changed, encoded);
                if constexpr (Part == 0)
                    _mm512_storeu_si512(p, next);
                else
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + 64),
                                        _mm512_castsi512_si256(next));
            }
        });
    }
}
#endif
} // namespace ikea::hashtable::detail::accumulation
