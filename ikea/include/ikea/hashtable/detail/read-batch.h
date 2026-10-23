#pragma once
#include "hash.h"
#include <bit>

namespace ikea::hashtable::detail {
#if defined(__AVX512BW__)
inline constexpr unsigned swiss_read_groups = 4;
#elif defined(__AVX2__)
inline constexpr unsigned swiss_read_groups = 2;
#else
inline constexpr unsigned swiss_read_groups = 1;
#endif

// The next logical groups are triangular, not physically adjacent. Express
// each address from the packet origin so their load addresses are independent.
inline std::size_t swiss_read_group(std::size_t first, std::size_t step, unsigned ordinal,
                                    std::size_t mask) {
    return (first + ordinal * step + ordinal * (ordinal + 1) / 2) & mask;
}

template <unsigned Groups>
inline std::size_t swiss_read_slot(std::size_t first, std::size_t step, unsigned lane,
                                   std::size_t mask) {
    static_assert(Groups == 2 || Groups == 4);
    const auto group = [&] {
        if constexpr (Groups == 2)
            return lane & 16 ? swiss_read_group(first, step, 1, mask) : first;
        else {
            const auto ordinal = (lane >> 4) & 3;
            // Triangular offsets 0,1,3,6 as four immediate nibbles. No address
            // array or second multiply is needed on the candidate path.
            const auto triangle = (0x6310u >> (ordinal * 4)) & 15;
            return (first + ordinal * step + triangle) & mask;
        }
    }();
    return group * 16 + (lane & 15);
}

struct swiss_read_masks {
    word candidates, empties;
};

// Caller supplies at least Groups logical groups. Every load stays within the
// assigned controls, including wrap and custom unaligned backing.
template <unsigned Groups>
[[gnu::always_inline]] swiss_read_masks compare_swiss_read(const std::uint8_t* controls, std::size_t first,
                                                           std::size_t step, std::size_t mask,
                                                           std::uint8_t tag) {
    static_assert(Groups == 2 || Groups == 4);
    swiss_read_masks out{};
#if defined(__AVX512BW__)
    if constexpr (Groups == 4) {
        const auto second = swiss_read_group(first, step, 1, mask);
        const auto third = swiss_read_group(first, step, 2, mask);
        const auto fourth = swiss_read_group(first, step, 3, mask);
        auto bytes = _mm512_castsi128_si512(
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + first * 16)));
        bytes = _mm512_inserti32x4(
            bytes, _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + second * 16)), 1);
        bytes = _mm512_inserti32x4(
            bytes, _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + third * 16)), 2);
        bytes = _mm512_inserti32x4(
            bytes, _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + fourth * 16)), 3);
        out.candidates = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8(tag));
        out.empties = _mm512_cmpeq_epi8_mask(bytes, _mm512_setzero_si512());
    } else
#endif
#if defined(__AVX2__)
    {
        for (unsigned i = 0; i < Groups; i += 2) {
            const auto a = swiss_read_group(first, step, i, mask);
            const auto b = swiss_read_group(first, step, i + 1, mask);
            const auto bytes = [&] {
                if (b == a + 1)
                    return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(controls + a * 16));
                return _mm256_inserti128_si256(
                    _mm256_castsi128_si256(
                        _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + a * 16))),
                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(controls + b * 16)), 1);
            }();
            out.candidates |=
                word(
                    unsigned(_mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, _mm256_set1_epi8(tag)))))
                << (i * 16);
            out.empties |= word(unsigned(_mm256_movemask_epi8(
                               _mm256_cmpeq_epi8(bytes, _mm256_setzero_si256()))))
                           << (i * 16);
        }
    }
#else
    {
        // Portable oracle/check route; ordinary NEON/SSE2 reads remain scalar.
        for (unsigned i = 0; i < Groups; ++i) {
            const auto* p = controls + swiss_read_group(first, step, i, mask) * 16;
            out.candidates |= word(equal_controls(p, tag)) << (i * 16);
            out.empties |= word(equal_controls(p, 0)) << (i * 16);
        }
    }
#endif
    // Include every candidate in the first group containing an empty, even
    // after its first empty lane. Tombstones do not terminate a logical group.
    if constexpr (Groups == 2) {
        out.candidates &= out.empties & 0xffff ? word{0xffff} : word{0xffffffff};
    }
#if defined(__BMI2__) && defined(__x86_64__)
    else {
        // Zero empties gives 80; BZHI then leaves all 64 candidate bits intact.
        const unsigned end = (unsigned(std::countr_zero(out.empties)) | 15u) + 1;
        out.candidates = _bzhi_u64(out.candidates, end);
    }
#else
    else if (out.empties) {
        out.candidates &= ~word{0} >> ((63u - std::countr_zero(out.empties)) & ~15u);
    }
#endif
    return out;
}
} // namespace ikea::hashtable::detail
