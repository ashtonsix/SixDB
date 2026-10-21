#pragma once
#include <ikea/boundlist/author/read.h>

namespace ikea::boundlist::native {
#if defined(__aarch64__) || defined(__AVX2__)
inline constexpr unsigned chunks = 16 / lanes;
/// Sixteen offsets and reservations, flattened into native registers at chain hops.
/// Ranks outside the accompanying active mask are unassigned or explicitly excluded.
struct packet {
    using vector = native::vector;
    static constexpr unsigned parts = 2 * chunks;
    std::array<vector, parts> v;
    vector offsets(unsigned chunk) const { return v[chunk]; }
    vector sizes(unsigned chunk) const { return v[chunks + chunk]; }
};
struct group {
    packet values;
    std::uint16_t active;
};
namespace detail {
// Keep the unrolled callable boundary fused too: outlining it materializes all
// packet vectors even when read16 itself is inlined into a native chain stage.
template <unsigned I, class Body> [[gnu::always_inline]] inline void chunk(Body &body) {
    [[clang::always_inline]] body(std::integral_constant<unsigned, I>{});
}
template <class Body, std::size_t... I>
[[gnu::always_inline]] inline void chunks_into(Body &body, std::index_sequence<I...>) {
    (chunk<I>(body), ...);
}
} // namespace detail
template <class Body> [[gnu::always_inline]] inline void each_chunk(Body &&body) {
    detail::chunks_into(body, std::make_index_sequence<chunks>{});
}
/// One admitted group; can be a partial final group. Loads access initialized
/// whole groups, never the payload or a terminal checkpoint. Native results feed
/// a fused consumer directly; no mandatory decoded stack array.
template <class Access, class Sink>
[[gnu::always_inline]] inline std::uint16_t decode16(Access a, unsigned group_index, Sink &&sink) {
    const auto &l = a.format();
    const auto first = group_index * 16;
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    unsigned carry = a.start(first);
    each_chunk([&](auto i) {
        constexpr unsigned lane = i * lanes;
        vector starts, reserved;
        if (l.fixed()) {
            reserved = splat(l.fixed_width());
            starts = multiply(add(indices(), splat(first + lane)), l.fixed_width());
        } else if constexpr (Access::family == geometry::sizes) {
            reserved = load<W>(a.source().windows().lengths.data() + a.width_offset(first + lane));
            const auto sums = prefix(reserved);
            starts = add(sub(sums, reserved), splat(carry));
            carry += last(sums);
        } else if constexpr (Access::family == geometry::relative_ends) {
            const auto ends =
                load<W>(a.source().windows().lengths.data() + a.width_offset(first + lane));
            const auto previous_local = previous(ends, lane ? carry : 0);
            const auto base = boundlist::detail::load<O>(a.geometry_data() + group_index * sizeof(O));
            starts = add(previous_local, splat(base));
            reserved = sub(ends, previous_local);
            carry = last(ends);
        } else {
            const auto ends = load<O>(a.geometry_data() + (first + lane) * sizeof(O));
            starts = previous(ends, carry);
            reserved = sub(ends, starts);
            carry = last(ends);
        }
        [[clang::always_inline]] sink(i, starts, reserved);
    });
    const unsigned count = std::min(16u, a.size() - first);
    unsigned active = (1u << count) - 1;
    if constexpr (requires { a.selection_mask(first, count); })
        active &= a.selection_mask(first, count);
    return std::uint16_t(active);
}
template <class Access> [[gnu::always_inline]] inline group read16(Access a, unsigned group_index) {
    group out;
    out.active = decode16(a, group_index,
                        [&](auto i, vector starts, vector reserved) __attribute__((always_inline)) {
                            out.values.v[i] = starts;
                            out.values.v[chunks + i] = reserved;
                        });
    return out;
}

#endif
} // namespace ikea::boundlist::native
