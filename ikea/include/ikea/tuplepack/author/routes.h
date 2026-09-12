#pragma once
#include <ikea/tuplepack/author/native.h>

namespace ikea::tuplepack::composition {
/// Restricted wiring algebra: output bit -> source bit [0,512), or -1 for zero.
/// It describes selection/shift/mask/disjoint OR, not arithmetic or Engine IR.
using bit_routes = std::array<std::int16_t, 512>;
bit_routes zero_routes() noexcept;
bit_routes identity_routes() noexcept;
[[nodiscard]] std::expected<bit_routes, error> compose(const bit_routes& outer,
                                                       const bit_routes& inner);
[[nodiscard]] std::expected<bit_routes, error> unite(const bit_routes&, const bit_routes&);
/// Physical byte packet -> mapped code packet; independent of an ISA reader's
/// compacted load controls. This can be composed with caller-authored assembly.
[[nodiscard]] std::expected<bit_routes, error> decoding_routes(const layout&,
                                                               std::span<const byte> map);
struct route_term {
    ikea::tuplepack::detail::shuffle operation;
    bool left = false;
};
/// Caller-owned compiled terms. Normalization needs 0 (identity), 1 (permutation)
/// or 3 (byte rotation) slots; a fully general legal route needs at most 16.
/// A failed preparation leaves output storage unchanged. The returned plan
/// borrows stable terms; no plan/control pointers belong in a wire description.
struct route_plan {
    std::span<const route_term> terms;
    bool normalized = false;
};
[[nodiscard]] std::expected<route_plan, error> prepare_routes(const bit_routes&,
                                                              std::span<route_term> storage);

#if defined(__aarch64__) || defined(__AVX2__)
namespace detail {
// These two bodies exploit the stronger normalized proof: all byte source
// routes are known, and a shift doesn't require another general permutation.
[[gnu::always_inline]] native::packet permute_full(native::packet,
                                                   const ikea::tuplepack::detail::shuffle&);
template <bool Left>
[[gnu::always_inline]] native::packet shift_mask(native::packet,
                                                 const ikea::tuplepack::detail::shuffle&);
} // namespace detail
} // namespace ikea::tuplepack::composition
#include <ikea/tuplepack/detail/native/routes.h>
namespace ikea::tuplepack::composition {
[[gnu::always_inline]] inline native::packet apply_routes(native::packet input, route_plan plan) {
    if (plan.normalized) {
        if (plan.terms.empty())
            return input;
        auto value = detail::permute_full(input, plan.terms[0].operation);
        if (plan.terms.size() == 1)
            return value;
        return native::bit_or(detail::shift_mask<true>(value, plan.terms[1].operation),
                              detail::shift_mask<false>(value, plan.terms[2].operation));
    }
    auto result = native::native_detail::join(
        native::native_detail::zero16(), native::native_detail::zero16(),
        native::native_detail::zero16(), native::native_detail::zero16());
    for (const auto& term : plan.terms)
        result =
            native::bit_or(result, term.left ? native::transform<true>(input, term.operation)
                                             : native::transform<false>(input, term.operation));
    return result;
}
#endif
} // namespace ikea::tuplepack::composition
