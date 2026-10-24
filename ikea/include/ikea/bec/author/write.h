#pragma once
#include <ikea/bec/author/native.h>
#include <ikea/bec/detail/write.h>

namespace ikea::bec::detail {
struct encoding_access {
    template <class Encode>
    [[gnu::always_inline]] static bec256::encoding make(Encode &&encode) {
        bec256::encoding result;
        result.bytes_ = encode(result.data_.data());
        return result;
    }
};
template <class Coverage> requires ikea::effects<Coverage, view> struct admitted_sink {
    view &target;
    std::size_t offset;
    Coverage &effects;
    [[gnu::always_inline]] unsigned operator()(const std::uint64_t *values,
                                               const std::uint64_t *widths) const noexcept {
        const auto n = encoded_bytes(widths);
        if (n) {
            effects.before(target, 0, offset, n);
            emit_exact(values, widths,
                       reinterpret_cast<std::uint8_t *>(target.storage.data()) + offset, n);
        }
        return n;
    }
    unsigned singleton(std::uint8_t value) const noexcept {
        effects.before(target, 0, offset, 1);
        target.storage[offset] = byte{value};
        return 1;
    }
};
} // namespace ikea::bec::detail

namespace ikea::bec256::native {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
/// Immediate checked replacement from native bits, with the same capacity,
/// population and effect contract as bec256::encode. No plain-bitset buffer.
[[nodiscard, gnu::always_inline]] inline std::expected<unsigned, error>
encode(block value, unsigned population, view &target, std::size_t offset,
       journal &effects) noexcept {
#if defined(IKEA_BEC256_AVX512)
    return detail::avx512::encode_to(value, population,
                                     detail::checked_sink{target, offset, effects});
#else
    return detail::neon::encode_to(value, population,
                                   detail::checked_sink{target, offset, effects});
#endif
}
/// Native encode-or-decline with bec256::encode_if_promising's heuristic and
/// checked mutation contract. Keeps the input in registers and shares byte
/// populations/widths between the shortcut and accepted encoder body.
[[nodiscard, gnu::always_inline]] inline std::expected<std::optional<unsigned>, error>
encode_if_promising(block value, unsigned population, unsigned enum_bit_cutoff, view &target,
                    std::size_t offset, journal &effects) noexcept {
#if defined(IKEA_BEC256_AVX512)
    return detail::avx512::encode_to(
        value, population, detail::selective_sink{{target, offset, effects}, enum_bit_cutoff});
#else
    return detail::neon::encode_to(
        value, population, detail::selective_sink{{target, offset, effects}, enum_bit_cutoff});
#endif
}
/// Trusted immediate replacement with caller-proved population and capacity for
/// the resulting body (47 bytes suffice for any value) and one coverage event.
/// A tighter proof, such as one byte for a singleton, is sufficient. Emits only the exact
/// body span; zero/full blocks write nothing. Input is already in registers.
/// Named target and command/effect storage remain live and disjoint. Coverage
/// is no-fail/no-suspend and may only use pre-admitted resources.
template <class Coverage> requires ikea::effects<Coverage, view>
[[nodiscard, gnu::always_inline]] inline unsigned
encode_exact_unchecked(block value, unsigned population, view &target, std::size_t offset,
                       Coverage &effects) noexcept {
    if (population == 0 || population == 256)
        return 0;
#if defined(IKEA_BEC256_AVX512)
    return detail::avx512::encode_to(value, population,
                                     detail::admitted_sink<Coverage>{target, offset, effects});
#else
    return detail::neon::encode_to(value, population,
                                   detail::admitted_sink<Coverage>{target, offset, effects});
#endif
}
/// One native 512-bit input, two independently placed exact bodies. The owner
/// proves both populations and body capacities, disjoint output bodies, and
/// room for two coverage events before entry. Neither half can then reject or
/// suspend. Publication of their interpretation metadata remains caller-owned.
template <class Coverage> requires ikea::effects<Coverage, view>
[[nodiscard, gnu::always_inline]] inline lengths
encode_pair_exact_unchecked(pair value, unsigned population_a, view &a, std::size_t offset_a,
                            unsigned population_b, view &b, std::size_t offset_b,
                            Coverage &effects) noexcept {
    return {encode_exact_unchecked(part<0>(value), population_a, a, offset_a, effects),
            encode_exact_unchecked(part<1>(value), population_b, b, offset_b, effects)};
}
/// Encodes register values directly into a transient candidate, without a plain
/// bitset buffer. The caller proves population matches value. This candidate's
/// checked write has the same exact-store/effect contract as encoding::make.
[[gnu::always_inline]] inline encoding encode_unchecked(block value, unsigned population) {
    return detail::encoding_access::make([&](byte *output) {
        return population == 0 || population == 256 ? 0
                                                    : encode_unchecked(value, population, output);
    });
}
#endif
} // namespace ikea::bec256::native
