#pragma once
// Independently packed-column control from the earlier encoding experiment.
namespace edit_fused {
template <class T>
void add_suffix(std::byte *bytes, unsigned first, unsigned limit, unsigned padded, int delta) {
    if constexpr (sizeof(T) == 1) {
        for (unsigned i = first / 16 * 16; i < limit; i += 16) {
            assert(i + 16 <= padded);
            const auto lo = first > i ? first - i : 0u, hi = std::min(16u, limit - i);
#if defined(__aarch64__)
            const uint8x16_t index{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
            const auto mask =
                vandq_u8(vcgeq_u8(index, vdupq_n_u8(lo)), vcltq_u8(index, vdupq_n_u8(hi)));
            vst1q_u8(reinterpret_cast<std::uint8_t *>(bytes + i),
                     vaddq_u8(vld1q_u8(reinterpret_cast<const std::uint8_t *>(bytes + i)),
                              vandq_u8(mask, vdupq_n_u8(delta))));
#elif defined(__AVX2__)
            const auto index = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
            const auto mask = _mm_and_si128(_mm_cmpgt_epi8(index, _mm_set1_epi8(int(lo) - 1)),
                                            _mm_cmpgt_epi8(_mm_set1_epi8(hi), index));
            auto *p = reinterpret_cast<__m128i *>(bytes + i);
            _mm_storeu_si128(
                p, _mm_add_epi8(_mm_loadu_si128(p), _mm_and_si128(mask, _mm_set1_epi8(delta))));
#else
            for (unsigned j = lo; j < hi; ++j)
                bytes[i + j] = std::byte(std::to_integer<unsigned>(bytes[i + j]) + delta);
#endif
        }
    } else {
        auto *p = reinterpret_cast<T *>(bytes);
        unsigned i;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        constexpr unsigned wide = 64 / sizeof(T);
        i = first / wide * wide;
        for (; i < limit && i + wide <= padded; i += wide) {
            const auto x = _mm512_loadu_si512(p + i);
            if constexpr (sizeof(T) == 2) {
                const auto pos = _mm512_add_epi16(
                    _mm512_set1_epi16(i),
                    _mm512_set_epi16(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
                                     15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
                const auto mask = _mm512_cmpge_epi16_mask(pos, _mm512_set1_epi16(first)) &
                                  _mm512_cmplt_epi16_mask(pos, _mm512_set1_epi16(limit));
                _mm512_storeu_si512(p + i,
                                    _mm512_mask_add_epi16(x, mask, x, _mm512_set1_epi16(delta)));
            } else {
                const auto pos = _mm512_add_epi32(
                    _mm512_set1_epi32(i),
                    _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
                const auto mask = _mm512_cmpge_epi32_mask(pos, _mm512_set1_epi32(first)) &
                                  _mm512_cmplt_epi32_mask(pos, _mm512_set1_epi32(limit));
                _mm512_storeu_si512(p + i,
                                    _mm512_mask_add_epi32(x, mask, x, _mm512_set1_epi32(delta)));
            }
        }
#elif defined(__AVX2__)
        i = first / (32 / sizeof(T)) * (32 / sizeof(T));
#else
        i = first / (16 / sizeof(T)) * (16 / sizeof(T));
#endif
#if defined(__AVX2__)
        constexpr unsigned lanes = 32 / sizeof(T);
        for (; i < limit && i + lanes <= padded; i += lanes) {
            const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + i));
            if constexpr (sizeof(T) == 2) {
                const auto pos = _mm256_add_epi16(
                    _mm256_set1_epi16(i),
                    _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
                const auto mask =
                    _mm256_and_si256(_mm256_cmpgt_epi16(pos, _mm256_set1_epi16(int(first) - 1)),
                                     _mm256_cmpgt_epi16(_mm256_set1_epi16(limit), pos));
                _mm256_storeu_si256(
                    reinterpret_cast<__m256i *>(p + i),
                    _mm256_add_epi16(x, _mm256_and_si256(mask, _mm256_set1_epi16(delta))));
            } else {
                const auto pos = _mm256_add_epi32(_mm256_set1_epi32(i),
                                                  _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
                const auto mask =
                    _mm256_and_si256(_mm256_cmpgt_epi32(pos, _mm256_set1_epi32(int(first) - 1)),
                                     _mm256_cmpgt_epi32(_mm256_set1_epi32(limit), pos));
                _mm256_storeu_si256(
                    reinterpret_cast<__m256i *>(p + i),
                    _mm256_add_epi32(x, _mm256_and_si256(mask, _mm256_set1_epi32(delta))));
            }
        }
#elif defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
        for (; i < limit && i + lanes <= padded; i += lanes) {
            if constexpr (sizeof(T) == 2) {
                const uint16x8_t indices{0, 1, 2, 3, 4, 5, 6, 7};
                const auto pos = vaddq_u16(vdupq_n_u16(i), indices);
                const auto mask = vandq_u16(vcgeq_u16(pos, vdupq_n_u16(first)),
                                            vcltq_u16(pos, vdupq_n_u16(limit)));
                vst1q_u16(p + i, vaddq_u16(vld1q_u16(p + i), vandq_u16(mask, vdupq_n_u16(delta))));
            } else {
                const uint32x4_t indices{0, 1, 2, 3};
                const auto pos = vaddq_u32(vdupq_n_u32(i), indices);
                const auto mask = vandq_u32(vcgeq_u32(pos, vdupq_n_u32(first)),
                                            vcltq_u32(pos, vdupq_n_u32(limit)));
                vst1q_u32(p + i, vaddq_u32(vld1q_u32(p + i), vandq_u32(mask, vdupq_n_u32(delta))));
            }
        }
#else
        for (i = first; i < limit; ++i)
            store(bytes + i * sizeof(T), T(load<T>(bytes + i * sizeof(T)) + delta));
#endif
        assert(i >= limit);
    }
}
} // namespace edit_fused
