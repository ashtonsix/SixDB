#pragma once
#include <ikea/tuplepack/detail/native/memory.h>
#include <ikea/tuplepack/detail/window.h>
#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native::native_detail {
/// Repeat each original-row bit across its physical/code-byte slots.
template <unsigned Rows, unsigned Bytes>
[[gnu::always_inline]] inline std::uint64_t byte_mask(std::uint64_t active) {
    static_assert(Rows * Bytes <= 64);
    if constexpr (Bytes == 1)
        return active;
#if defined(__BMI2__)
    else {
        constexpr auto positions = [] {
            std::uint64_t bits = 0;
            for (unsigned r = 0; r < Rows; ++r)
                bits |= std::uint64_t(1) << (r * Bytes);
            return bits;
        }();
        return _pdep_u64(active, positions) * (~std::uint64_t(0) >> (64 - Bytes));
    }
#else
    else {
        std::uint64_t result = 0;
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                result |= (~std::uint64_t(0) >> (64 - Bytes)) << (r * Bytes);
        return result;
    }
#endif
}
template <unsigned Rows, unsigned Part>
[[gnu::always_inline]] inline vector16 mask_part(std::uint64_t active) {
    constexpr unsigned B = 64 / Rows, first = Part * 16 / B;
    if constexpr (B >= 16) {
        const byte value = active & (std::uint64_t(1) << first) ? 255 : 0;
#if defined(__aarch64__)
        return vdupq_n_u8(value);
#else
        return _mm_set1_epi8(value);
#endif
    } else {
        constexpr auto bits = [] {
            std::array<byte, 16> result{};
            for (unsigned i = 0; i < 16; ++i)
                result[i] = byte(1u << ((i / B) % 8));
            return result;
        }();
        const auto selected = active >> first;
#if defined(__aarch64__)
        auto value = B == 1 ? vcombine_u8(vdup_n_u8(selected), vdup_n_u8(selected >> 8))
                            : vdupq_n_u8(selected);
        return vcgtq_u8(vandq_u8(value, vld1q_u8(bits.data())), vdupq_n_u8(0));
#else
        auto value = B == 1
                         ? _mm_unpacklo_epi64(_mm_set1_epi8(selected), _mm_set1_epi8(selected >> 8))
                         : _mm_set1_epi8(selected);
        return _mm_xor_si128(
            _mm_cmpeq_epi8(_mm_and_si128(value, _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                                                    bits.data()))),
                           _mm_setzero_si128()),
            _mm_set1_epi8(-1));
#endif
    }
}
} // namespace ikea::tuplepack::native::native_detail
namespace ikea::tuplepack::native {
/// A byte mask for row-major code slots: every byte of an active row is 0xff.
/// Row bits remain separate from the payload's values and evidence meaning.
template <unsigned Rows> [[gnu::always_inline]] inline packet row_mask(std::uint64_t active) {
    static_assert(Rows && Rows <= 64 && std::has_single_bit(Rows));
#if defined(__AVX512VBMI__)
    return _mm512_movm_epi8(native_detail::byte_mask<Rows, 64 / Rows>(active));
#else
    using namespace native_detail;
    return join(mask_part<Rows, 0>(active), mask_part<Rows, 1>(active), mask_part<Rows, 2>(active),
                mask_part<Rows, 3>(active));
#endif
}
/// Expand original-row bits into this plan's grouped map slots. Trailing
/// capacity is zero; explicit holes still belong to their original row.
[[gnu::always_inline]] inline packet row_mask(const packet_layout<64> &order,
                                              std::uint64_t active) {
    using namespace native_detail;
    const auto part = [&](unsigned first) __attribute__((always_inline)) {
#if defined(__aarch64__)
        const auto owner = vld1q_u8(order.row_indices().data() + first);
        const auto selected =
            vqtbl1q_u8(vreinterpretq_u8_u64(vdupq_n_u64(active)), vshrq_n_u8(owner, 3));
        const auto bit =
            vshlq_u8(vdupq_n_u8(1), vreinterpretq_s8_u8(vandq_u8(owner, vdupq_n_u8(7))));
        return vtstq_u8(selected, bit);
#else
        const auto owner =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(order.row_indices().data() + first));
        const auto index = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(owner, 3), _mm_set1_epi8(31)),
                                        _mm_and_si128(owner, _mm_set1_epi8(char(0x80))));
        const auto selected = _mm_shuffle_epi8(_mm_set1_epi64x(active), index);
        const auto bit = _mm_shuffle_epi8(_mm_set1_epi64x(0x8040201008040201ull),
                                          _mm_and_si128(owner, _mm_set1_epi8(7)));
        return _mm_xor_si128(_mm_cmpeq_epi8(_mm_and_si128(selected, bit), _mm_setzero_si128()),
                             _mm_set1_epi8(-1));
#endif
    };
    return join(part(0), part(16), part(32), part(48));
}
} // namespace ikea::tuplepack::native
#endif
