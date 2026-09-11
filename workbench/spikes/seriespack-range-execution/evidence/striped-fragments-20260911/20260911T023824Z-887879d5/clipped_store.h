#pragma once

// Target-local sink for a nonempty contiguous interval of a native value.
// Output is anchored at the first active object, never at a clipped read origin.
namespace clipped_store {
#if defined(__aarch64__) || defined(__AVX2__)
template<class Ops, unsigned L, unsigned N, class V>
[[gnu::always_inline]] inline void put(std::uint8_t* out, V value, unsigned first, unsigned count) {
    if (count == N) {
        Ops::template store<L, L, N>(out, value);
        return;
    }
#if defined(__aarch64__)
    const uint8x16_t iota{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    auto bytes = vqtbl1q_u8(value, vaddq_u8(iota, vdupq_n_u8(first * L)));
    unsigned remaining = count * L;
    if (remaining >= 8) {
        vst1_u8(out, vget_low_u8(bytes));
        remaining -= 8;
        if (remaining == 0) return;
        out += 8;
        bytes = vextq_u8(bytes, bytes, 8);
    }
    if (remaining >= 4) {
        vst1q_lane_u32(reinterpret_cast<std::uint32_t*>(out), vreinterpretq_u32_u8(bytes), 0);
        remaining -= 4;
        if (remaining == 0) return;
        out += 4;
        bytes = vextq_u8(bytes, bytes, 4);
    }
    if (remaining >= 2) {
        vst1q_lane_u16(reinterpret_cast<std::uint16_t*>(out), vreinterpretq_u16_u8(bytes), 0);
        remaining -= 2;
        if (remaining == 0) return;
        out += 2;
        bytes = vextq_u8(bytes, bytes, 2);
    }
    vst1q_lane_u8(out, bytes, 0);
#else
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    if constexpr (Ops::register_bytes == 64) {
        if constexpr (L == 4) {
            const unsigned mask = ((1u << count) - 1) << first;
            _mm512_mask_compressstoreu_epi32(out, mask, value);
        } else if constexpr (L == 8) {
            const unsigned mask = ((1u << count) - 1) << first;
            _mm512_mask_compressstoreu_epi64(out, mask, value);
        } else {
            const auto iota = _mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
            const auto shifted = _mm_shuffle_epi8(_mm512_castsi512_si128(value),
                _mm_add_epi8(iota, _mm_set1_epi8(first * L)));
            _mm512_mask_storeu_epi8(out, (1u << (count * L)) - 1,
                _mm512_castsi128_si512(shifted));
        }
    } else
#endif
    if constexpr (L >= 4) {
        const auto iota = _mm256_setr_epi32(0,1,2,3,4,5,6,7);
        const auto shifted = _mm256_permutevar8x32_epi32(value,
            _mm256_add_epi32(iota, _mm256_set1_epi32(first * (L / 4))));
        if constexpr (L == 4) {
            const auto mask = _mm256_cmpgt_epi32(_mm256_set1_epi32(count), iota);
            _mm256_maskstore_epi32(reinterpret_cast<int*>(out), mask, shifted);
        } else {
            const auto lanes = _mm256_setr_epi64x(0,1,2,3);
            const auto mask = _mm256_cmpgt_epi64(_mm256_set1_epi64x(count), lanes);
            _mm256_maskstore_epi64(reinterpret_cast<long long*>(out), mask, shifted);
        }
    } else {
        const auto iota = _mm_setr_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
        auto bytes = _mm_shuffle_epi8(_mm256_castsi256_si128(value),
            _mm_add_epi8(iota, _mm_set1_epi8(first * L)));
        unsigned remaining = count * L;
        if (remaining >= 8) {
            _mm_storel_epi64(reinterpret_cast<__m128i*>(out), bytes);
            remaining -= 8;
            if (remaining == 0) return;
            out += 8;
            bytes = _mm_srli_si128(bytes, 8);
        }
        auto low = static_cast<std::uint32_t>(_mm_cvtsi128_si32(bytes));
        if (remaining >= 4) {
            std::memcpy(out, &low, 4);
            remaining -= 4;
            if (remaining == 0) return;
            out += 4;
            low = static_cast<std::uint32_t>(_mm_cvtsi128_si32(_mm_srli_si128(bytes, 4)));
        }
        if (remaining >= 2) {
            const auto half = static_cast<std::uint16_t>(low);
            std::memcpy(out, &half, 2);
            remaining -= 2;
            if (remaining == 0) return;
            out += 2;
            low >>= 16;
        }
        *out = static_cast<std::uint8_t>(low);
    }
#endif
}
#endif
}
