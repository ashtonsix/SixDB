#pragma once
#include "boundlist.h"
#include <ikea/seriespack/author/filter.h>

namespace boundlist_spike {
// One extraction/composition candidate. The expression borrows actual named
// SeriesPack sources; it does not own bytes, domain policy or allocation.
template <class Expression> struct packed_view {
    using point_type = std::uint64_t;
    static constexpr unsigned group_size = 16;
    Expression expression;
    unsigned count;
    population eligible;
    unsigned groups() const { return (count + 15) / 16; }
    [[gnu::always_inline]] std::uint64_t end(unsigned rank) const {
        ikea::seriespack::composition::scalar_ops ops;
        return ikea::seriespack::composition::read(ops, expression, rank, true).value;
    }
    std::uint64_t group_end(unsigned group) const { return end(std::min(count, (group + 1) * 16) - 1); }
    template <edge E> [[gnu::always_inline]] unsigned in_group(unsigned group, std::uint64_t query) const {
        constexpr auto Op = E == edge::after ? ikea::comparison::greater : ikea::comparison::greater_equal;
        const auto first = group * 16, take = std::min(16u, count - first);
        const auto mask = ikea::seriespack::composition::filter_unchecked<Op>(
            expression, first, take, query, eligible.mask(first, take));
        return mask ? first + std::countr_zero(mask) : count;
    }
    template <edge E> [[gnu::always_inline]] unsigned scan_group(std::uint64_t query) const {
        auto raw = *this; raw.eligible = {};
        for (unsigned g = 0; g < groups(); ++g) if (raw.template in_group<E>(g, query) != count) return g;
        return groups();
    }
};
} // namespace boundlist_spike
