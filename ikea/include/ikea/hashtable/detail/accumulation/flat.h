#pragma once
#include "packed32.h"
namespace ikea::hashtable::detail::accumulation {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
template <unsigned T> using flat_image = std::array<__m512i, (T + 1) / 2>;
// The complete 64-byte owner admits all reads, including flags/slack which
// are removed by constant masks. Every tile still has the maintained Local law.
template <unsigned E, unsigned T> [[gnu::always_inline]] flat_image<T> read_flat(const byte* p) {
    static_assert(E >= 10 && E <= 30 && T >= 2 && T <= 5 && T * (E + 1) <= 63);
    if constexpr (T == 2 && E >= 24)
        return {read_packed<E>(p)};
    else {
        constexpr unsigned B = E / 8, R = E % 8, N = T * 8;
        flat_image<T> result{};
        const auto packed = _mm512_loadu_si512(p);
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
                    _mm512_permutexvar_epi8(_mm512_load_si512(order.data()), packed);
                return _mm512_and_si512(_mm512_gf2p8affine_epi64_epi8(
                                            _mm512_set1_epi64(0x8040201008040201ULL), columns, 0),
                                        _mm512_set1_epi8((1u << R) - 1));
            }
        }();
        ht::each<(T + 1) / 2>([&](auto part) __attribute__((always_inline)) {
            constexpr unsigned Part = decltype(part)::value;
            alignas(64) static constexpr auto order = [] {
                std::array<byte, 64> a{};
                for (unsigned r = 0; r < 16; ++r)
                    if (Part * 16 + r < N)
                        for (unsigned j = 0; j < B; ++j)
                            a[r * 4 + j] =
                                ((Part * 16 + r) / 8) * E + ((Part * 16 + r) % 8) * B + j;
                return a;
            }();
            constexpr word mask = [] {
                word m = 0;
                for (unsigned r = 0; r < 16; ++r)
                    if (Part * 16 + r < N)
                        for (unsigned j = 0; j < B; ++j)
                            m |= word{1} << (r * 4 + j);
                return m;
            }();
            const auto body =
                _mm512_maskz_permutexvar_epi8(mask, _mm512_load_si512(order.data()), packed);
            const auto tail = _mm512_cvtepu8_epi32(_mm512_extracti32x4_epi32(tails, Part));
            result[Part] = _mm512_or_si512(_mm512_slli_epi32(body, R), tail);
        });
        return result;
    }
}
template <unsigned E, unsigned T>
[[gnu::always_inline]] void write_flat(byte* p, const flat_image<T>& keys, word live) {
    static_assert(E >= 10 && E <= 30 && T >= 2 && T <= 5 && T * (E + 1) <= 63);
    if constexpr (T == 2 && E >= 24)
        write_packed_vector<E>(p, keys[0], unsigned(live));
    else {
        constexpr unsigned B = E / 8, R = E % 8, N = T * 8, Banks = (T + 1) / 2;
        alignas(64) static constexpr auto body_order = [] {
            std::array<byte, 64> a{};
            for (unsigned r = 0; r < N; ++r)
                for (unsigned j = 0; j < B; ++j)
                    a[(r / 8) * E + (r % 8) * B + j] = byte(r * 4 + j);
            return a;
        }();
        constexpr word body_mask = [] {
            word m = 0;
            for (unsigned r = 0; r < N; ++r)
                for (unsigned j = 0; j < B; ++j)
                    m |= word{1} << ((r / 8) * E + (r % 8) * B + j);
            return m;
        }();
        const auto first = _mm512_srli_epi32(keys[0], R);
        auto body = [&] {
            if constexpr (Banks == 1)
                return _mm512_permutexvar_epi8(_mm512_load_si512(body_order.data()), first);
            else
                return _mm512_permutex2var_epi8(first, _mm512_load_si512(body_order.data()),
                                                _mm512_srli_epi32(keys[1], R));
        }();
        if constexpr (Banks == 3) {
            alignas(64) static constexpr auto third_order = [] {
                std::array<byte, 64> a{};
                for (unsigned r = 32; r < N; ++r)
                    for (unsigned j = 0; j < B; ++j)
                        a[(r / 8) * E + (r % 8) * B + j] = byte((r - 32) * 4 + j);
                return a;
            }();
            constexpr word third_mask = [] {
                word m = 0;
                for (unsigned r = 32; r < N; ++r)
                    for (unsigned j = 0; j < B; ++j)
                        m |= word{1} << ((r / 8) * E + (r % 8) * B + j);
                return m;
            }();
            body = _mm512_mask_permutexvar_epi8(body, third_mask,
                                                _mm512_load_si512(third_order.data()),
                                                _mm512_srli_epi32(keys[2], R));
        }
        body = _mm512_maskz_mov_epi8(body_mask, body);
        const auto tails = [&] {
            if constexpr (!R)
                return _mm512_setzero_si512();
            else {
                auto rows = _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(keys[0]));
                if constexpr (Banks >= 2)
                    rows = _mm512_inserti32x4(rows, _mm512_cvtepi32_epi8(keys[1]), 1);
                if constexpr (Banks == 3)
                    rows = _mm512_inserti32x4(rows, _mm512_cvtepi32_epi8(keys[2]), 2);
                const auto reverse = _mm512_broadcast_i32x4(
                    _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
                const auto columns =
                    _mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL),
                                                  _mm512_shuffle_epi8(rows, reverse), 0);
                alignas(64) static constexpr auto order = [] {
                    std::array<byte, 64> a{};
                    for (unsigned t = 0; t < T; ++t)
                        for (unsigned j = 0; j < R; ++j)
                            a[t * E + 8 * B + j] = t * 8 + j;
                    return a;
                }();
                constexpr word mask = [] {
                    word m = 0;
                    for (unsigned t = 0; t < T; ++t)
                        for (unsigned j = 0; j < R; ++j)
                            m |= word{1} << (t * E + 8 * B + j);
                    return m;
                }();
                return _mm512_maskz_permutexvar_epi8(mask, _mm512_load_si512(order.data()),
                                                     columns);
            }
        }();
        alignas(64) static constexpr auto flag_order = [] {
            std::array<byte, 64> a{};
            for (unsigned j = 0; j < T; ++j)
                a[E * T + j] = j;
            return a;
        }();
        constexpr word flag_mask = ((word{1} << T) - 1) << (E * T),
                       changed = (word{1} << (T * (E + 1))) - 1;
        const auto encoded = _mm512_mask_permutexvar_epi8(_mm512_or_si512(body, tails), flag_mask,
                                                          _mm512_load_si512(flag_order.data()),
                                                          _mm512_set1_epi64(~live));
        _mm512_storeu_si512(p, _mm512_mask_mov_epi8(_mm512_loadu_si512(p), changed, encoded));
    }
}
template <class S> [[gnu::always_inline]] void decode_flat_source(const byte* p, word* out) {
    if constexpr (!S::bitmap && S::B == 64 && S::T >= 2 && S::T <= 5 && S::encoding <= 30) {
        const auto keys = read_flat<S::encoding, S::T>(p);
        ht::each<(S::T + 1) / 2>([&](auto part) __attribute__((always_inline)) {
            constexpr unsigned Part = decltype(part)::value;
            _mm512_storeu_si512(out + Part * 16,
                                _mm512_cvtepu32_epi64(_mm512_castsi512_si256(keys[Part])));
            if constexpr (Part * 16 + 16 <= S::N)
                _mm512_storeu_si512(
                    out + Part * 16 + 8,
                    _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(keys[Part], 1)));
        });
    } else
        for (unsigned row = 0; row < S::N; ++row) {
            if constexpr (S::bitmap)
                out[row] = row;
            else
                out[row] = ht::point<S::encoding>(p, row, S::N);
        }
}
#endif
} // namespace ikea::hashtable::detail::accumulation
