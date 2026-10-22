#pragma once
#include <ikea/intervals/detail/simd.h>
#include <ikea/intervals/detail/bytes.h>
#include <ikea/intervals/types.h>
#include <algorithm>
#include <cassert>

namespace ikea::streampack::detail::compact::detail {
/// Numeric column rewrite with an admitted initialized tail. Before receives
/// the actual issued contiguous store footprint, including preserving lanes.
/// This body owns no allocation, rank policy, publication or effect identity.
template <class T, class Before>
[[gnu::always_inline]] void add_fields(std::byte *fields, unsigned first, unsigned limit,
                                       unsigned padded, int delta, Before &&before) {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
    if (first == limit || !delta)
        return;
#if defined(__aarch64__) || defined(__AVX2__)
    if constexpr (sizeof(T) < 4) {
#if defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
#else
        constexpr unsigned lanes = 16;
#endif
        const unsigned start = first / lanes * lanes, stop = (limit + lanes - 1) / lanes * lanes;
        assert(stop <= padded);
        before(start * sizeof(T), (stop - start) * sizeof(T));
        for (unsigned i = start; i < stop; i += lanes) {
            const auto lo = first > i ? first - i : 0u, hi = std::min(lanes, limit - i);
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
            const auto active = low_bits(hi) & ~low_bits(lo);
            if constexpr (sizeof(T) == 1) {
                auto *p = reinterpret_cast<__m128i *>(fields + i);
                const auto x = _mm_loadu_si128(p);
                _mm_storeu_si128(p, _mm_mask_add_epi8(x, active, x, _mm_set1_epi8(delta)));
            } else {
                auto *p = reinterpret_cast<__m256i *>(fields + i * 2);
                const auto x = _mm256_loadu_si256(p);
                _mm256_storeu_si256(p,
                                    _mm256_mask_add_epi16(x, active, x, _mm256_set1_epi16(delta)));
            }
#elif defined(__AVX2__)
            if constexpr (sizeof(T) == 1) {
                const auto index =
                    _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
                const auto active = _mm_and_si128(_mm_cmpgt_epi8(index, _mm_set1_epi8(int(lo) - 1)),
                                                  _mm_cmpgt_epi8(_mm_set1_epi8(hi), index));
                auto *p = reinterpret_cast<__m128i *>(fields + i);
                _mm_storeu_si128(p, _mm_add_epi8(_mm_loadu_si128(p),
                                                 _mm_and_si128(active, _mm_set1_epi8(delta))));
            } else {
                const auto index =
                    _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
                const auto active =
                    _mm256_and_si256(_mm256_cmpgt_epi16(index, _mm256_set1_epi16(int(lo) - 1)),
                                     _mm256_cmpgt_epi16(_mm256_set1_epi16(hi), index));
                auto *p = reinterpret_cast<__m256i *>(fields + i * 2);
                _mm256_storeu_si256(
                    p, _mm256_add_epi16(_mm256_loadu_si256(p),
                                        _mm256_and_si256(active, _mm256_set1_epi16(delta))));
            }
#else
            if constexpr (sizeof(T) == 1) {
                const uint8x16_t index{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
                const auto active =
                    vandq_u8(vcgeq_u8(index, vdupq_n_u8(lo)), vcltq_u8(index, vdupq_n_u8(hi)));
                auto *p = reinterpret_cast<std::uint8_t *>(fields + i);
                vst1q_u8(p, vaddq_u8(vld1q_u8(p), vandq_u8(active, vdupq_n_u8(delta))));
            } else {
                const uint16x8_t index{0, 1, 2, 3, 4, 5, 6, 7};
                const auto active =
                    vandq_u16(vcgeq_u16(index, vdupq_n_u16(lo)), vcltq_u16(index, vdupq_n_u16(hi)));
                auto *p = reinterpret_cast<std::uint16_t *>(fields + i * 2);
                vst1q_u16(p, vaddq_u16(vld1q_u16(p), vandq_u16(active, vdupq_n_u16(delta))));
            }
#endif
        }
    } else {
        constexpr auto lanes = native::lanes;
        const unsigned start = first / lanes * lanes;
        const unsigned stop = (limit + lanes - 1) / lanes * lanes;
        assert(stop <= padded);
        before(start * sizeof(T), (stop - start) * sizeof(T));
        [[maybe_unused]] const auto lower = native::splat(first);
        const auto upper = native::splat(limit);
        auto position = native::add(native::indices(), native::splat(start));
        for (unsigned i = start; i < stop; i += lanes) {
            auto value = native::load<T>(fields + i * sizeof(T));
#if defined(__AVX512F__) && defined(__AVX512BW__)
            const auto active =
                _mm512_cmpge_epi32_mask(position, lower) & _mm512_cmplt_epi32_mask(position, upper);
            value = _mm512_mask_add_epi32(value, active, value, native::splat(unsigned(delta)));
            _mm512_storeu_si512(fields + i * 4, value);
#elif defined(__AVX2__)
            const auto active =
                _mm256_and_si256(_mm256_cmpgt_epi32(position, _mm256_set1_epi32(int(first) - 1)),
                                 _mm256_cmpgt_epi32(upper, position));
            value =
                _mm256_add_epi32(value, _mm256_and_si256(active, native::splat(unsigned(delta))));
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(fields + i * 4), value);
#else
            const auto active = vandq_u32(vcgeq_u32(position, lower), vcltq_u32(position, upper));
            value = vaddq_u32(value, vandq_u32(active, native::splat(unsigned(delta))));
            vst1q_u32(reinterpret_cast<std::uint32_t *>(fields + i * 4), value);
#endif
            position = native::add(position, native::splat(lanes));
        }
    }
#else
    (void)padded;
    before(first * sizeof(T), (limit - first) * sizeof(T));
    for (unsigned i = first; i < limit; ++i)
        store(fields + i * sizeof(T), T(load<T>(fields + i * sizeof(T)) + delta));
#endif
}
} // namespace ikea::streampack::detail::compact::detail
