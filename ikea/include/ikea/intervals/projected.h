#pragma once
#include <ikea/intervals/geometry.h>

namespace ikea::intervals {
/// Immutable Frame run ends in [1,256], stored as end-1. Comparisons translate
/// the query once so native search sees the original contiguous byte plane,
/// at every count: short planes count exactly without count-dependent loops.
/// No control/directory/tombstone bytes. The owner admits nondecreasing ends.
struct run_end_source {
    const std::uint8_t *ends_minus_one;
    unsigned count;
    unsigned size() const noexcept { return count; }
    unsigned origin() const noexcept { return 0; }
    unsigned at(unsigned rank) const noexcept {
        return unsigned(detail::read_element(ends_minus_one, rank)) + 1;
    }
    unsigned extent() const noexcept { return count ? at(count - 1) : 0; }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q &point) const {
        if constexpr (std::integral<Q>) {
            const detail::prepared_query<unsigned, Q> query(point);
            if (query.relation) return query.relation < 0 ? 0u : count;
            if (query.value == 0) return 0;
            if (query.value > 256) return count;
            const auto threshold = std::uint8_t(query.value - 1);
            return detail::hybrid_array<E, true>(ends_minus_one,count,threshold);
        } else {
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                const auto mid = lo + (hi - lo) / 2;
                if (detail::meets<E>(at(mid), point)) hi = mid;
                else lo = mid + 1;
            }
            return lo;
        }
    }
};

/// Arithmetic geometry, with no stored boundary plane. The owner proves all
/// coordinates fit Point and step is nonnegative; no multiplication may wrap.
template <std::unsigned_integral Point = std::uint32_t> struct fixed_source {
    unsigned count;
    Point first, step;
    unsigned size() const noexcept { return count; }
    Point origin() const noexcept { return first; }
    Point start(unsigned rank) const noexcept { return first + Point(rank) * step; }
    Point slot_size(unsigned) const noexcept { return step; }
    Point at(unsigned rank) const noexcept { return first + Point(rank + 1) * step; }
    Point extent() const noexcept { return first + Point(count) * step; }
    template <std::integral Q> unsigned owner_rank(Q point) const noexcept {
        return unsigned((Point(point) - first) / step); // Called only for a valid owner.
    }
};
} // namespace ikea::intervals
