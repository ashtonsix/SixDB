#pragma once

// Private materializing regions. The byte carrier names original positions;
// its extent is independent of the caller's eventual integer carrier.
#include <ikea/seriespack/detail/physical.h>
#if defined(__aarch64__)
#include <ikea/seriespack/native_neon.h>
#elif defined(__AVX2__)
#include <ikea/seriespack/native_avx2.h>
#endif

namespace ikea::seriespack::detail::range_regions {
#if defined(__aarch64__) || defined(__AVX2__)
#if defined(__aarch64__)
using bytes16 = uint8x16_t;
#else
using bytes16 = __m128i;
#endif

template<unsigned Count>
[[gnu::always_inline]] inline void store_bytes(std::uint8_t* out, bytes16 value) {
    static_assert(Count == 8 || Count == 16);
#if defined(__aarch64__)
    neon::body_detail::store_bytes<Count>(out, value);
#else
    if constexpr (Count == 8) _mm_storel_epi64(reinterpret_cast<__m128i*>(out), value);
    else _mm_storeu_si128(reinterpret_cast<__m128i*>(out), value);
#endif
}

template<unsigned W, unsigned Count, unsigned Group>
[[gnu::always_inline]] inline bytes16 striped_group(const std::uint8_t* tile, unsigned lane) {
    static_assert(Count == 8 || Count == 16);
    constexpr unsigned R = W % 8;
#if defined(__aarch64__)
    constexpr auto plan = seriespack::neon::native_detail::residual_order<R, Group, false>::plan;
    constexpr auto last = plan.fields[plan.count - 1];
    auto low = seriespack::neon::native_detail::unpack_fields<W, Count, Group, 0, plan.count>(tile, lane);
    if constexpr (last.source + last.length != 8) low = vandq_u8(low, vdupq_n_u8((1u << R) - 1));
    return low;
#else
    auto result = _mm_setzero_si128();
    static_for<R / std::gcd(R, 8u)>([&](auto stripe) {
        constexpr auto field = residual_field<R, Group, stripe>::value;
        if constexpr (field.value_mask != 0) {
            const auto* p = tile + stripe_offset<W>(stripe) + lane;
            auto value = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(p))
                                    : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
            if constexpr (field.shift > 0) value = _mm_srli_epi16(value, field.shift);
            if constexpr (field.shift < 0) value = _mm_slli_epi16(value, -field.shift);
            value = _mm_and_si128(value, _mm_set1_epi8(static_cast<char>(field.value_mask)));
            result = _mm_or_si128(result, value);
        }
    });
    return result;
#endif
}

// lane+Count<=32 and the complete requested logical region are admitted by
// the enclosing range traversal. In particular, a16-byte region at lane16
// never borrows the next stripe or a padding suffix.
template<unsigned W, unsigned Count = 16>
[[gnu::always_inline]] inline bytes16 striped(const std::uint8_t* tile, unsigned first) {
    constexpr unsigned R = W % 8;
    const auto group = first / 32, lane = first % 32;
    if constexpr (R == 1 || R == 2 || R == 4) {
        const auto* p = tile + stripe_offset<W>(0) + lane;
#if defined(__aarch64__)
        const auto raw = neon::body_detail::load_bytes<Count>(p);
        return vandq_u8(vshlq_u8(raw, vdupq_n_s8(-int(group * R))), vdupq_n_u8((1u << R) - 1));
#else
        const auto raw = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(p))
                                     : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        return _mm_and_si128(_mm_srl_epi16(raw, _mm_cvtsi32_si128(group * R)),
                             _mm_set1_epi8((1u << R) - 1));
#endif
    } else if constexpr (R == 3) {
        static constexpr std::uint64_t controls = [] {
            std::uint64_t result = 0;
            for (unsigned g = 0; g != 8; ++g) {
                const auto low = residual_position<3>(g, 0), high = residual_position<3>(g, 2);
                const auto descriptor = (low / 8) * 32 | low % 8 | (low / 8 != high / 8 ? 8u : 0u);
                result |= std::uint64_t(descriptor) << (8 * g);
            }
            return result;
        }();
        const auto control = unsigned(controls >> (8 * group));
        const auto* p = tile + stripe_offset<W>((control >> 5) & 3) + lane;
#if defined(__aarch64__)
        const auto raw = neon::body_detail::load_bytes<Count>(p);
        if ((control & 8) == 0)
            return vandq_u8(vshlq_u8(raw, vdupq_n_s8(-int(control & 7))), vdupq_n_u8(7));
        const auto middle = neon::body_detail::load_bytes<Count>(tile + stripe_offset<W>(1) + lane);
        return vorrq_u8(vshrq_n_u8(raw, 6),
            vandq_u8(vshlq_u8(middle, vdupq_n_s8(-int(4 + (group >> 2)))), vdupq_n_u8(4)));
#else
        const auto raw = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(p))
                                    : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        if ((control & 8) == 0)
            return _mm_and_si128(_mm_srl_epi16(raw, _mm_cvtsi32_si128(control & 7)), _mm_set1_epi8(7));
        const auto* mid_p = tile + stripe_offset<W>(1) + lane;
        const auto middle = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(mid_p))
                                       : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(mid_p));
        return _mm_or_si128(_mm_and_si128(_mm_srli_epi16(raw, 6), _mm_set1_epi8(3)),
            _mm_and_si128(_mm_srl_epi16(middle, _mm_cvtsi32_si128(4 + (group >> 2))), _mm_set1_epi8(4)));
#endif
    } else if constexpr (R == 6) {
        const auto edge = group & 2;
        const auto* p = tile + stripe_offset<W>(edge) + lane;
#if defined(__aarch64__)
        const auto raw = neon::body_detail::load_bytes<Count>(p);
        if (((group + 1) & 2) == 0) return vandq_u8(raw, vdupq_n_u8(63));
        const auto middle = neon::body_detail::load_bytes<Count>(tile + stripe_offset<W>(1) + lane);
        return vorrq_u8(vandq_u8(vshrq_n_u8(raw, 2), vdupq_n_u8(48)),
            vandq_u8(vshlq_u8(middle, vdupq_n_s8(-int(edge * 2))), vdupq_n_u8(15)));
#else
        const auto raw = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(p))
                                    : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        if (((group + 1) & 2) == 0) return _mm_and_si128(raw, _mm_set1_epi8(63));
        const auto* mid_p = tile + stripe_offset<W>(1) + lane;
        const auto middle = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(mid_p))
                                       : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(mid_p));
        return _mm_or_si128(_mm_and_si128(_mm_srli_epi16(raw, 2), _mm_set1_epi8(48)),
            _mm_and_si128(_mm_srl_epi16(middle, _mm_cvtsi32_si128(edge * 2)), _mm_set1_epi8(15)));
#endif
    } else if constexpr (R == 5 || R == 7) {
        // These maps have one ordinary field or two adjoining fields. Select
        // the fragment class once, independently of the final output lanes.
        // This is the vector form of get_arithmetic's original-index map.
        const unsigned start = group * R, stripe = start / 8, shift = start % 8;
        const auto* p = tile + stripe_offset<W>(stripe) + lane;
#if defined(__aarch64__)
        const auto raw = neon::body_detail::load_bytes<Count>(p);
        if (shift <= 8 - R)
            return vandq_u8(vshlq_u8(raw, vdupq_n_s8(-int(shift))), vdupq_n_u8((1u << R) - 1));
        const auto next = neon::body_detail::load_bytes<Count>(tile + stripe_offset<W>(stripe + 1) + lane);
        const auto low_mask = vdupq_n_u8((1u << (shift + R - 8)) - 1);
        return vbslq_u8(low_mask, next, vshrq_n_u8(raw, 8 - R));
#else
        const auto raw = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(p))
                                    : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        if (shift <= 8 - R)
            return _mm_and_si128(_mm_srl_epi16(raw, _mm_cvtsi32_si128(shift)), _mm_set1_epi8((1u << R) - 1));
        const auto* next_p = tile + stripe_offset<W>(stripe + 1) + lane;
        const auto next = Count == 16 ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(next_p))
                                     : _mm_loadl_epi64(reinterpret_cast<const __m128i*>(next_p));
        const unsigned low = (1u << (shift + R - 8)) - 1;
        // Word shifts must still remove the neighboring byte's bits.
        return _mm_or_si128(_mm_and_si128(_mm_srli_epi16(raw, 8 - R), _mm_set1_epi8(((1u << R) - 1) & ~low)),
                            _mm_and_si128(next, _mm_set1_epi8(low)));
#endif
    } else {
        return dispatch_group<payload_layout<W, geometry::striped>::tile_values / 32>(group,
            [&](auto selected) { return striped_group<W, Count, selected>(tile, lane); });
    }
}

// Two contiguous Local8 residual-only tiles, represented as16 original byte
// lanes. The caller proves no stride gap between these particular tiles.
template<unsigned W>
[[gnu::always_inline]] inline bytes16 local_pair(const std::uint8_t* tiles) {
    static_assert(W >= 1 && W <= 7);
#if defined(__aarch64__)
    return seriespack::neon::read_local_pair<W>(tiles);
#else
    if constexpr (W == 1) {
        const auto raw = _mm_cvtsi32_si128(static_cast<int>(load_le<2>(tiles)));
        const auto groups = _mm_setr_epi8(0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1);
        const auto bits = _mm_set1_epi64x(static_cast<long long>(0x8040201008040201ULL));
        const auto values = _mm_shuffle_epi8(raw, groups);
        return _mm_and_si128(_mm_cmpeq_epi8(_mm_and_si128(values, bits), bits), _mm_set1_epi8(1));
    } else {
        // Exact windows supply the two W-byte plane packets in separate
        // 64-bit lanes. Both transposes then share one native operation.
        constexpr unsigned Bytes = 2 * W;
        const auto packed = [&] [[gnu::always_inline]] {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
            return _mm_maskz_loadu_epi8((1u << Bytes) - 1, tiles);
#else
            if constexpr (Bytes <= 8) return _mm_cvtsi64_si128(static_cast<long long>(load_le<Bytes>(tiles)));
            else return _mm_or_si128(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(tiles)),
                _mm_slli_si128(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(tiles + Bytes - 8)), Bytes - 8));
#endif
        }();
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned i = 0; i != 16; ++i)
                result[i] = i % 8 < W ? i / 8 * W + i % 8 : 0x80;
            return result;
        }();
        const auto matrix = _mm_shuffle_epi8(packed, _mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.data())));
        return seriespack::avx2::native_detail::transpose(matrix);
    }
#endif
}
#endif
} // namespace ikea::seriespack::detail::range_regions
