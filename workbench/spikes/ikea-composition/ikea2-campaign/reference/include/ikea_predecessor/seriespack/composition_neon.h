#pragma once

#if defined(__aarch64__)

#include <ikea_predecessor/seriespack/composition.h>
#include <ikea_predecessor/seriespack/native_neon.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ikea_predecessor::seriespack::neon {

/// Physical tile ordinal; Begin supplies the fragment offset within that tile.
struct tile_position { std::size_t tile; };

/// Immediate NEON fragment with L-byte unsigned lanes and all-ones/zero masks.
/// Admit each actual child's tile independently; masks do not suppress reads.
/// Only selected logical rows are valid: active_all requires a complete fragment.
template<class Format, unsigned Begin>
struct composition_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned W = payload_width(layout);
    static constexpr unsigned L = sizeof(typename Format::scalar_type);
    static constexpr auto G = layout.storage;
    using traits = fragment_traits<W, G, L>;
    static constexpr unsigned lanes = traits::lanes;
    static constexpr std::size_t tile_values = Format::payload::tile_values;
    static_assert(Begin % lanes == 0 && Begin + lanes <= tile_values);
    using value_type = uint8x16_t;
    using mask_type = uint8x16_t;

    static constexpr std::size_t original_index(tile_position rows, unsigned lane) {
        return rows.tile * tile_values + Begin + lane;
    }

    [[gnu::always_inline]] static inline mask_type active_all() {
        if constexpr (lanes * L == 16) return vdupq_n_u8(255);
        else return vcombine_u8(vdup_n_u8(255), vdup_n_u8(0));
    }

    /// Convert low positional bits to native lanes; performs no admission checks.
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        bits &= (std::uint64_t{1} << lanes) - 1;
        if constexpr (L == 1) {
            constexpr std::uint8_t weights[16] = {1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
            const auto replicated = vcombine_u8(vdup_n_u8(bits), vdup_n_u8(bits >> 8));
            return vtstq_u8(replicated, vld1q_u8(weights));
        } else if constexpr (L == 2) {
            constexpr std::uint16_t weights[8] = {1,2,4,8,16,32,64,128};
            return vreinterpretq_u8_u16(vtstq_u16(vdupq_n_u16(bits), vld1q_u16(weights)));
        } else if constexpr (L == 4) {
            constexpr std::uint32_t weights[4] = {1,2,4,8};
            return vreinterpretq_u8_u32(vtstq_u32(vdupq_n_u32(bits), vld1q_u32(weights)));
        } else {
            constexpr std::uint64_t weights[2] = {1,2};
            return vreinterpretq_u8_u64(vtstq_u64(vdupq_n_u64(bits), vld1q_u64(weights)));
        }
    }

    template<class Ref>
    [[gnu::always_inline]] static inline const std::uint8_t* payload(const Ref& ref,
                                                                    tile_position rows) {
        static_assert(Ref::format_type::layout == layout,
                      "A different child geometry needs a separately admitted native region");
        if constexpr (W == 0) return nullptr;
        const auto& p = ref.source.placement().payload;
        return reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride);
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_body(const composition::body_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return neon::read_body<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return neon::read_tail<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, unsigned Plane, class Source>
    [[gnu::always_inline]] value_type read_head(const composition::head_ref<F, Plane, Source>& ref,
                                               tile_position rows, mask_type) const {
        static_assert(F::layout == layout && Plane < layout.head_bits / 8);
        const auto& p = ref.source.placement().heads[Plane];
        return detail::neon::decode_body_prefix<1, L, lanes>(
            reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride + Begin));
    }

    template<unsigned LowBits>
    [[gnu::always_inline]] value_type join(value_type high, value_type low) const {
        return neon::join<L, LowBits>(high, low);
    }

    [[gnu::always_inline]] value_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                   mask_type active) const {
        if constexpr (layout.width < 64)
            if (cutoff >= (std::uint64_t{1} << layout.width)) return active;
        value_type predicate;
        if constexpr (L == 1) predicate = vcltq_u8(values, vdupq_n_u8(cutoff));
        if constexpr (L == 2) predicate = vreinterpretq_u8_u16(vcltq_u16(
            vreinterpretq_u16_u8(values), vdupq_n_u16(cutoff)));
        if constexpr (L == 4) predicate = vreinterpretq_u8_u32(vcltq_u32(
            vreinterpretq_u32_u8(values), vdupq_n_u32(cutoff)));
        if constexpr (L == 8) predicate = vreinterpretq_u8_u64(vcltq_u64(
            vreinterpretq_u64_u8(values), vdupq_n_u64(cutoff)));
        auto result = vandq_u8(predicate, active);
        // Clang 21 otherwise narrows the boolean lanes at the domain branch
        // and re-expands them for sum. This emits no instruction and preserves
        // the native mask; known-all active inputs can still simplify above.
        asm("" : "+w"(result));
        return result;
    }

    [[gnu::always_inline]] value_type unsigned_equal(value_type values, std::uint64_t constant,
                                                    mask_type active) const {
        if constexpr (layout.width < 64)
            if (constant >= (std::uint64_t{1} << layout.width)) return vdupq_n_u8(0);
        value_type predicate;
        if constexpr (L == 1) predicate = vceqq_u8(values, vdupq_n_u8(constant));
        if constexpr (L == 2) predicate = vreinterpretq_u8_u16(vceqq_u16(
            vreinterpretq_u16_u8(values), vdupq_n_u16(constant)));
        if constexpr (L == 4) predicate = vreinterpretq_u8_u32(vceqq_u32(
            vreinterpretq_u32_u8(values), vdupq_n_u32(constant)));
        if constexpr (L == 8) predicate = vreinterpretq_u8_u64(vceqq_u64(
            vreinterpretq_u64_u8(values), vdupq_n_u64(constant)));
        auto result = vandq_u8(predicate, active);
        asm("" : "+w"(result));
        return result;
    }

    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum) const {
        const auto kept = vandq_u8(values, selected);
        if constexpr (L == 1) return vaddlvq_u8(kept);
        if constexpr (L == 2) return vaddlvq_u16(vreinterpretq_u16_u8(kept));
        if constexpr (L == 4) return vaddlvq_u32(vreinterpretq_u32_u8(kept));
        if constexpr (L == 8) return vaddvq_u64(vreinterpretq_u64_u8(kept));
    }
};

} // namespace ikea_predecessor::seriespack::neon

#endif
