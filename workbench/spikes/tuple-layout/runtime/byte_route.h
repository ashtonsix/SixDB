#pragma once
#include "composition.h"

namespace tuple_composition_probe {
// A useful normal form of the restricted bit-route algebra: each output byte
// is a permutation plus rotation of one source byte. Selected from the routes,
// never from fixture/layout identity. General mappings retain the old lowering.
struct byte_route {
    shuffle permutation, positive, negative;
    bool supported = false, identity = false, rotating = false;
};
inline byte_route recognize_bytes(const bit_routes& bits) {
    byte_route p; p.identity = true;
    for (unsigned i = 0; i < 64; ++i) {
        if (bits[i * 8] < 0) return {};
        const int source = bits[i * 8] / 8, rotation = bits[i * 8] % 8;
        for (unsigned b = 0; b < 8; ++b)
            if (bits[i * 8 + b] != source * 8 + (int(b) + rotation) % 8) return {};
        p.permutation.index[i] = source; p.permutation.mask[i] = 255;
        p.positive.index[i] = p.negative.index[i] = i;
        p.negative.shift[i] = -rotation; p.negative.mask[i] = 255u >> rotation;
        p.positive.shift[i] = rotation ? 8 - rotation : 0;
        p.positive.mask[i] = rotation ? byte(255u << (8 - rotation)) : 0;
        p.identity &= source == int(i) && rotation == 0;
        p.rotating |= rotation != 0;
    }
    finish_controls(p.permutation, false); finish_controls(p.positive, true); finish_controls(p.negative, false);
    p.supported = true;
    return p;
}
[[gnu::always_inline]] inline native_packet permute_full(native_packet source, const shuffle& p) {
#if defined(__aarch64__)
    const uint8x16x4_t table{{source.a, source.b, source.c, source.d}};
    return {vqtbl4q_u8(table, vld1q_u8(p.index.data())), vqtbl4q_u8(table, vld1q_u8(p.index.data() + 16)),
            vqtbl4q_u8(table, vld1q_u8(p.index.data() + 32)), vqtbl4q_u8(table, vld1q_u8(p.index.data() + 48))};
#elif defined(__AVX512VBMI__)
    return _mm512_permutexvar_epi8(_mm512_loadu_si512(p.index.data()), source);
#else
    const auto a = _mm256_broadcastsi128_si256(native_detail::split<0>(source));
    const auto b = _mm256_broadcastsi128_si256(native_detail::split<1>(source));
    const auto c = _mm256_broadcastsi128_si256(native_detail::split<2>(source));
    const auto d = _mm256_broadcastsi128_si256(native_detail::split<3>(source));
    auto part = [&](unsigned offset) {
        auto index = [&](unsigned i) { return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.avx2_index[i].data() + offset)); };
        return _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(a, index(0)), _mm256_shuffle_epi8(b, index(1))),
                              _mm256_or_si256(_mm256_shuffle_epi8(c, index(2)), _mm256_shuffle_epi8(d, index(3))));
    };
    return {part(0), part(32)};
#endif
}
template <bool Left> [[gnu::always_inline]] inline native_packet shift_mask(native_packet source, const shuffle& p) {
#if defined(__aarch64__)
    auto part = [&](uint8x16_t v, unsigned i) {
        return vandq_u8(vshlq_u8(v, vld1q_s8(p.shift.data() + i)), vld1q_u8(p.mask.data() + i));
    };
    return {part(source.a, 0), part(source.b, 16), part(source.c, 32), part(source.d, 48)};
#elif defined(__AVX512VBMI__)
    return _mm512_and_si512(_mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.bit_index.data()), source), _mm512_loadu_si512(p.mask.data()));
#else
    auto part = [&](auto source, unsigned i) {
        const auto low = _mm256_set1_epi16(255), high = _mm256_set1_epi16(short(0xff00));
        const auto ef = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.even_factor.data() + i));
        const auto of = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.odd_factor.data() + i));
        auto even = _mm256_mullo_epi16(_mm256_and_si256(source, low), ef);
        __m256i odd;
        if constexpr (Left) {
            even = _mm256_and_si256(even, low);
            odd = _mm256_mullo_epi16(_mm256_and_si256(source, high), of);
        } else {
            even = _mm256_srli_epi16(even, 8);
            odd = _mm256_mullo_epi16(_mm256_srli_epi16(source, 8), of);
        }
        return _mm256_and_si256(_mm256_or_si256(even, _mm256_and_si256(odd, high)),
                               _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.mask.data() + i * 2)));
    };
    return {part(source.a, 0), part(source.b, 16)};
#endif
}
[[gnu::always_inline]] inline native_packet apply_bytes(native_packet source, const byte_route& p) {
    if (p.identity) return source;
    auto permuted = permute_full(source, p.permutation);
    if (!p.rotating) return permuted;
    return either(shift_mask<true>(permuted, p.positive), shift_mask<false>(permuted, p.negative));
}
} // namespace tuple_composition_probe
