#pragma once
#include <ikea/seriespack/detail/mutation/physical.h>

namespace ikea::seriespack::composition {
#if defined(__aarch64__) || defined(__AVX2__)
/// Reverse the same semantic joins used by reading. An execution backend owns
/// native projection and physical stores; an effect backend can walk the same
/// destination before executing any writes.
template <class Ops, class S, field_kind Field, class Value>
[[gnu::always_inline]] inline void assign(Ops& ops, const leaf<S, Field>& destination,
                                          std::size_t first, Value value) {
    ops.write(destination, first, value);
}
template <class Ops, unsigned K, class B, class T, class Value>
[[gnu::always_inline]] inline void assign(Ops& ops, const payload_expression<K, B, T>& destination,
                                          std::size_t first, Value value) {
    if constexpr (K >= 8)
        assign(ops, destination.body, first, ops.template project<K / 8 * 8, K % 8>(value));
    if constexpr (K % 8)
        assign(ops, destination.tail, first, ops.template project<K % 8, 0>(value));
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Value>
[[gnu::always_inline]] inline void assign(Ops& ops,
                                          const value_expression<K, H, P, H0, H1>& destination,
                                          std::size_t first, Value value) {
    if constexpr (K > H)
        assign(ops, destination.payload, first, ops.template project<K - H, 0>(value));
    if constexpr (H >= 8)
        assign(ops, destination.head0, first, ops.template project<8, K - 8>(value));
    if constexpr (H == 16)
        assign(ops, destination.head1, first, ops.template project<8, K - 16>(value));
}

template <field_kind Field>
inline constexpr unsigned write_field = 1u << static_cast<unsigned>(Field);
struct native_write_ops {
    template <unsigned To, unsigned Shift, unsigned From>
    [[gnu::always_inline]] auto project(native::values<From> value) const {
        return native::project<To, Shift>(value);
    }
    template <class S, field_kind Field, unsigned K>
    [[gnu::always_inline]] void write(const leaf<S, Field>& destination, std::size_t first,
                                      native::values<K> value) const {
        using F = typename S::format_type;
        static_assert(K == leaf<S, Field>::bit_width);
        constexpr unsigned shift = Field == field_kind::body    ? F::tail
                                   : Field == field_kind::head0 ? F::width - 8
                                   : Field == field_kind::head1 ? F::width - 16
                                                                : 0;
        native::write16<F, write_field<Field>>(destination.source, first,
                                               native::place<F::width, shift>(value));
    }
};
struct scalar_write_ops {
    template <unsigned To, unsigned Shift, unsigned From>
    auto project(scalar_value<From> value) const {
        auto x = value.value >> Shift;
        if constexpr (To < 64)
            x &= (std::uint64_t{1} << To) - 1;
        return scalar_value<To>{x};
    }
    template <class S, field_kind Field, unsigned K>
    void write(const leaf<S, Field>& destination, std::size_t row, scalar_value<K> value) const {
        using F = typename S::format_type;
        static_assert(K == leaf<S, Field>::bit_width);
        const auto tile = row / F::tile_rows, lane = row % F::tile_rows;
        if constexpr (Field == field_kind::head0 || Field == field_kind::head1) {
            constexpr unsigned p = Field == field_kind::head0 ? 1 : 2;
            const auto& plane = destination.source.stream(p);
            plane.bytes[tile * plane.stride + lane] = value.value;
        } else {
            const auto& plane = destination.source.stream(0);
            auto* bytes = plane.bytes.data() + tile * plane.stride;
            if constexpr (Field == field_kind::body)
                ikea::seriespack::detail::store<F::body>(
                    bytes + ikea::seriespack::detail::body_offset<F>(lane), value.value);
            else
                ikea::seriespack::detail::put_tail<F>(bytes, lane, value.value);
        }
    }
};
/// No native data in this walk. Owners may count/preflight effects and acquire
/// all effect capacity before the matching native assignment begins. Version
/// preservation (for example Orbital page COW) belongs to the owner.
struct write_token {};
template <class Coverage, bool Point = false> struct coverage_ops {
    Coverage& coverage;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
    void write(const leaf<S, Field>& destination, std::size_t first, write_token) const {
        using F = typename S::format_type;
        auto emit = [&](byte_write write) { coverage.before(destination.source, write); };
        if constexpr (Point)
            visit_writes_point<F, write_field<Field>>(destination.source, first, emit);
        else
            visit_writes16<F, write_field<Field>>(destination.source, first, emit);
    }
};

template <class Expression, class Coverage>
void visit_assignment16(const Expression& destination, std::size_t first, Coverage& coverage) {
    coverage_ops<Coverage> ops{coverage};
    assign(ops, destination, first, write_token{});
}
template <class Expression, class Coverage>
void visit_assignment_point(const Expression& destination, std::size_t row, Coverage& coverage) {
    coverage_ops<Coverage, true> ops{coverage};
    assign(ops, destination, row, write_token{});
}

template <class Expression, class Summary, class Coverage>
void replace_point_unchecked(const Expression& destination, std::size_t row, std::uint64_t value,
                             Summary& summary, Coverage& coverage) {
    if constexpr (Summary::needs_before) {
        scalar_ops reader;
        summary.observe_scalar(read(reader, destination, row, true).value, value);
    }
    visit_assignment_point(destination, row, coverage);
    scalar_write_ops writer;
    assign(writer, destination, row, scalar_value<Expression::bit_width>{value});
}

/// Trusted, non-suspending composed update. Actual writable leaves, logical
/// extent, non-overlapping semantic destinations, input domain, isolation and
/// effect resources are admitted by the enclosing operation. Shared bytes in
/// distinct striped bit fields are permitted; overlapping semantic fields are
/// not. Every 16 input slots is readable; only selected slots need fit the domain.
template <class Expression, class U, class Summary>
[[gnu::always_inline]] inline auto replacement_values16(const Expression& destination,
                                                        std::size_t first, const U* input,
                                                        std::uint16_t selected, Summary& summary) {
    __builtin_assume(first % 16 == 0);
    constexpr unsigned K = Expression::bit_width;
    auto after = [&] {
        const auto value = native::load_values(input);
        if constexpr (sizeof(U) >= sizeof(uint_for<K>))
            return native::narrow<K>(value);
        else
            return native::widen<K>(value);
    }();
    native_ops reader;
    if constexpr (Summary::needs_before) {
        const auto before = read(reader, destination, first, selected);
        if (selected != 0xffff)
            after = native::choose(selected, after, before);
        summary.observe(before, after, selected);
    } else if (selected != 0xffff)
        after = native::choose(selected, after, read(reader, destination, first, selected));
    return after;
}

template <class Expression, class U, class Summary, class Coverage>
[[gnu::always_inline]] inline void
replace16_unchecked(const Expression& destination, std::size_t first, const U* input,
                    std::uint16_t selected, Summary& summary, Coverage& coverage) {
    if (!selected)
        return;
    const auto after =
        composition::replacement_values16(destination, first, input, selected, summary);
    // All effects are exposed before any destination is changed, including
    // fragments in other owners. Publication remains outside this local call.
    visit_assignment16(destination, first, coverage);
    native_write_ops writer;
    assign(writer, destination, first, after);
}
#endif
} // namespace ikea::seriespack::composition
