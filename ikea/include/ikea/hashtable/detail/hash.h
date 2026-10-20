#pragma once
#include "representation.h"
namespace ikea::hashtable {
// This is one mathematical 128-bit product. On AArch64 it is MUL + UMULH.
// Low W bits are a permutation. Bucket+quotient therefore recover the key.
template <unsigned W, unsigned K> struct permutation {
    static constexpr word multiplier = 0x9e3779b185ebca87ULL;
    static constexpr word inverse = [] {
        word a = 1;
        for (unsigned i = 0; i < 6; ++i)
            a *= 2 - multiplier * a;
        return a;
    }();
    word seed;
    struct result {
        word bucket, quotient, swiss;
        // Derived only after routing to overflow; primary reads/writes do not
        // pay the reserved-code remapping cost.
        byte fingerprint() const {
            const byte x = byte(swiss >> 56);
            return byte(x + (x < 2 ? 8 : 0));
        }
    };
    [[gnu::always_inline]] result operator()(word key) const {
        // Trusted W-bit inputs and the construction-masked seed. Avoid a
        // redundant narrow-domain mask (Clang otherwise emitted a partial-
        // register BZHI count in the actual independent consumer).
        __builtin_assume((key & ~mask<W>) == 0);
        __builtin_assume((seed & ~mask<W>) == 0);
        const __uint128_t p = __uint128_t(key ^ seed) * multiplier;
        const word lo = word(p), h = lo ^ word(p >> 64);
        const auto bucket = [&] {
            if constexpr (K == 64)
                return word{0};
            else
                return (lo >> K) & mask<W - K>;
        }();
        return {bucket, low_bits<K>(lo), h};
    }
    word recover(word bucket, word quotient) const {
        const word y = K == 64 ? quotient : (bucket << K) | quotient;
        return ((y * inverse) ^ seed) & mask<W>;
    }
};
inline unsigned equal_controls(const byte* p, byte rhs) {
#if defined(__aarch64__)
    const auto lanes = vceqq_u8(vld1q_u8(p), vdupq_n_u8(rhs));
    return ikea::detail::filter::byte_bits(lanes);
#elif defined(__SSE2__)
    return unsigned(_mm_movemask_epi8(
        _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)), _mm_set1_epi8(rhs))));
#else
    unsigned m = 0;
    for (unsigned i = 0; i < 16; ++i)
        m |= unsigned(p[i] == rhs) << i;
    return m;
#endif
}
} // namespace ikea::hashtable
