#pragma once
#include <ikea2/seriespack/detail/native/avx512/read.h>
#include <ikea2/seriespack/detail/native/avx2/write.h>

namespace ikea2::seriespack::zmm {
/// Native subregions are register projections, with no array handoff. P counts
/// logical rows; the wider execution group does not change their coordinates.
template <unsigned P, unsigned K, unsigned N>
[[gnu::always_inline]] inline x86::values<K> part16(values<K, N> source) {
    constexpr unsigned L = sizeof(uint_for<K>);
    static_assert(P % 16 == 0 && P + 16 <= N);
    x86::values<K> out;
    if constexpr (L == 1)
        out.v[0] = _mm512_extracti32x4_epi32(source.v[P / 64], P % 64 / 16);
    else
        detail::each<x86::values<K>::parts>([&](auto j) {
            constexpr unsigned byte = P * L + j * 32;
            out.v[j] = _mm512_extracti64x4_epi64(source.v[byte / 64], byte % 64 / 32);
        });
    return out;
}

template <unsigned K, unsigned N, class U>
[[gnu::always_inline]] inline values<K, N> load_values(const U* input) {
    if constexpr (sizeof(U) == sizeof(uint_for<K>)) {
        values<K, N> out;
        if constexpr (N * sizeof(U) == 16)
            out.v[0] =
                _mm512_zextsi128_si512(_mm_loadu_si128(reinterpret_cast<const __m128i*>(input)));
        else if constexpr (N * sizeof(U) == 32)
            out.v[0] =
                _mm512_zextsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(input)));
        else
            detail::each<values<K, N>::parts>(
                [&](auto p) { out.v[p] = _mm512_loadu_si512(input + p * (64 / sizeof(U))); });
        return out;
    } else
        return packets<K, N>([&](unsigned p) {
            const auto value = x86::load_values(input + p);
            if constexpr (sizeof(U) > sizeof(uint_for<K>))
                return x86::narrow<K>(value);
            else
                return x86::widen<K>(value);
        });
}

template <unsigned K, unsigned N>
[[gnu::always_inline]] inline values<K, N> choose(std::uint64_t active, values<K, N> yes,
                                                  values<K, N> no) {
    constexpr unsigned L = sizeof(uint_for<K>);
    const auto mask = active_mask<K, N>(active);
    detail::each<values<K, N>::parts>([&](auto p) {
        if constexpr (L == 1)
            yes.v[p] = _mm512_mask_blend_epi8(mask.bits[p], no.v[p], yes.v[p]);
        if constexpr (L == 2)
            yes.v[p] = _mm512_mask_blend_epi16(mask.bits[p], no.v[p], yes.v[p]);
        if constexpr (L == 4)
            yes.v[p] = _mm512_mask_blend_epi32(mask.bits[p], no.v[p], yes.v[p]);
        if constexpr (L == 8)
            yes.v[p] = _mm512_mask_blend_epi64(mask.bits[p], no.v[p], yes.v[p]);
    });
    return yes;
}
} // namespace ikea2::seriespack::zmm
