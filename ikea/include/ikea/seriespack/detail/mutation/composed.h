#pragma once
#include <ikea/seriespack/detail/physical_source.h>
#include <ikea/seriespack/detail/mutation/assignment.h>

namespace ikea::seriespack::author {
#if defined(__aarch64__) || defined(__AVX2__)
namespace range_detail {
template <class S, field_kind Field> auto striped_tail(const leaf<S, Field>& value) {
    if constexpr (Field == field_kind::tail && S::layout_type::storage == geometry::striped)
        return &value;
    else
        return nullptr;
}
template <unsigned K, unsigned H, class P, class H0, class H1>
auto striped_tail(const value_expression<K, H, P, H0, H1>& value);
template <unsigned K, class B, class T>
auto striped_tail(const payload_expression<K, B, T>& value) {
    if constexpr (K % 8)
        return striped_tail(value.tail);
    else
        return nullptr;
}
template <unsigned K, unsigned H, class P, class H0, class H1>
auto striped_tail(const value_expression<K, H, P, H0, H1>& value) {
    if constexpr (K > H)
        return striped_tail(value.payload);
    else
        return nullptr;
}

template <class F, unsigned Group, class Target> struct batch_writer : native_write_ops {
    native::striped_tail_batch<F>& batch;
    const Target& target;
    template <class S, field_kind Field, unsigned K>
    [[gnu::always_inline]] void write(const leaf<S, Field>& destination, std::size_t row,
                                      native::values<K> value) const {
        if constexpr (Field == field_kind::tail && std::is_same_v<S, Target>) {
            if (&destination.source == &target) {
                batch.template add<Group>(value);
                return;
            }
        }
        native_write_ops{}.write(destination, row, value);
    }
};
template <class Coverage, class Target> struct other_coverage {
    Coverage& coverage;
    const Target& target;
    template <unsigned, unsigned> write_token project(write_token) const {
        return {};
    }
    template <class S, field_kind Field>
        requires(Field == field_kind::tail && std::is_same_v<S, Target>)
    [[gnu::always_inline]] void write(const leaf<S, Field>& destination, std::size_t row,
                                      write_token) const {
        static_assert(ikea::effects<Coverage, S>);
        // A batched tail is covered once by the enclosing tile. Keep this
        // identity test visible before paying for an unmatched footprint call.
        if (&destination.source != &target)
            write_other_tail(destination, row);
    }
    template <class S, field_kind Field>
        requires(!(Field == field_kind::tail && std::is_same_v<S, Target>))
    void write(const leaf<S, Field>& destination, std::size_t row, write_token) const {
        static_assert(ikea::effects<Coverage, S>);
        visit_writes16<typename S::layout_type, write_field<Field>>(
            destination.source, row,
            [&](ikea::detail::write_span bytes) { coverage.before(destination.source, bytes.plane, bytes.offset, bytes.size); });
    }

  private:
    void write_other_tail(const leaf<Target, field_kind::tail>& destination,
                          std::size_t row) const {
        visit_writes16<typename Target::layout_type, write_field<field_kind::tail>>(
            destination.source, row,
            [&](ikea::detail::write_span bytes) { coverage.before(destination.source, bytes.plane, bytes.offset, bytes.size); });
    }
};
} // namespace range_detail

/// A bound bulk invocation owns traversal. For a complete substituted striped
/// residual tile, each packed half-stripe is assembled in native registers and
/// stored once. Other leaves receive those same logical replacements directly.
/// Contributions may be visited out of row order; maintenance must permit that.
template <class Expression, class U, class Prefilter, class Summary, class Coverage>
void replace_regions_unchecked(const Expression& destination, std::size_t first, std::size_t count,
                               const U* input, Prefilter&& mask, Summary& summary,
                               Coverage& coverage) {
    __builtin_assume(first % 16 == 0 && count % 16 == 0);
    std::size_t offset = 0;
    const auto tail = range_detail::striped_tail(destination);
    if constexpr (std::is_pointer_v<decltype(tail)>) {
        using F = typename std::remove_pointer_t<decltype(tail)>::layout_type;
        while (offset < count && (first + offset) % F::tile_rows) {
            author::replace16_unchecked(destination, first + offset, input + offset,
                                             mask(first + offset), summary, coverage);
            offset += 16;
        }
        for (; count - offset >= F::tile_rows; offset += F::tile_rows) {
            const auto origin = first + offset;
            __builtin_assume(origin % F::tile_rows == 0);
            std::array<std::uint16_t, F::tile_rows / 16> masks;
            bool complete = true;
            ikea::seriespack::detail::each<F::tile_rows / 16>([&](auto r) {
                masks[r] = mask(origin + r * 16);
                complete &= masks[r] != 0;
            });
            if (!complete) {
                ikea::seriespack::detail::each<F::tile_rows / 16>([&](auto r) {
                    author::replace16_unchecked(destination, origin + r * 16,
                                                     input + offset + r * 16, masks[r], summary,
                                                     coverage);
                });
                continue;
            }
            const auto& plane = tail->source.stream(0);
            const auto tile_offset = origin / F::tile_rows * plane.stride;
            // Record all owners' actual write union before touching this tile.
            ikea::seriespack::detail::each<native::striped_tail_batch<F>::stripes>([&](auto s) {
                coverage.before(
                    tail->source,
                    0, tile_offset + ikea::seriespack::detail::stripe_offset<F>(s), 32);
            });
            using Target = std::remove_cvref_t<decltype(tail->source)>;
            range_detail::other_coverage<Coverage, Target> effects{coverage, tail->source};
            ikea::seriespack::detail::each<F::tile_rows / 16>(
                [&](auto r) { assign(effects, destination, origin + r * 16, write_token{}); });
            ikea::seriespack::detail::each<2>([&](auto half) {
                native::striped_tail_batch<F> batch;
                ikea::seriespack::detail::each<F::tile_rows / 32>([&](auto group) {
                    constexpr auto r = group * 32 + half * 16;
                    const auto value = author::replacement_values16(
                        destination, origin + r, input + offset + r, masks[group * 2 + half],
                        summary);
                    range_detail::batch_writer<F, group, Target> writer{{}, batch, tail->source};
                    assign(writer, destination, origin + r, value);
                });
                batch.store(ikea::to_u8(plane.bytes).data() + tile_offset, half);
            });
        }
    }
    for (; offset < count; offset += 16)
        author::replace16_unchecked(destination, first + offset, input + offset,
                                         mask(first + offset), summary, coverage);
}

template <class Expression, class U, class Prefilter, class Summary, class Coverage>
void replace_unchecked(const Expression& destination, std::size_t first, std::size_t count,
                       const U* input, Prefilter&& mask, Summary& summary, Coverage& coverage) {
    std::size_t offset = 0;
    while (offset < count && (first + offset) % 16) {
        const auto row = first + offset;
        if (mask(row - row % 16) & (1u << (row % 16)))
            author::replace_point_unchecked(destination, row, input[offset], summary,
                                                 coverage);
        ++offset;
    }
    const auto regions = (count - offset) / 16 * 16;
    if (regions)
        author::replace_regions_unchecked(destination, first + offset, regions, input + offset,
                                               mask, summary, coverage);
    offset += regions;
    while (offset < count) {
        const auto row = first + offset;
        if (mask(row - row % 16) & (1u << (row % 16)))
            author::replace_point_unchecked(destination, row, input[offset], summary,
                                                 coverage);
        ++offset;
    }
}
#endif
} // namespace ikea::seriespack::author
