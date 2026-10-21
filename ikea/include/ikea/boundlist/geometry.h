#pragma once
#include <ikea/boundlist/search.h>
#include <cassert>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace ikea::boundlist {
/// One interval per exclusive end. The owner admits ordered ends >= origin and
/// coherent borrowed backing. The geometry requires no storage format,
/// arithmetic distance, payload metadata or selection policy.
template <class Ends, class Point> struct geometry_source {
    Ends ends;
    Point first;
    unsigned size() const noexcept { return ends.size(); }
    const Point &origin() const noexcept { return first; }
    decltype(auto) at(unsigned rank) const { return ends.at(rank); }
    Point extent() const { return size() ? Point(at(size() - 1)) : first; }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q &point) const {
        return detail::boundary<E>(ends, point);
    }
    template <edge E, class Q> auto prepare(const Q &point) const {
        return detail::prepare<E>(ends, point);
    }
};
template <class Ends>
auto from_ends(Ends ends, std::remove_cvref_t<decltype(ends.at(0))> origin) {
    return geometry_source{std::move(ends), std::move(origin)};
}
/// The first cut is explicit in this input, but not an extra interval rank.
/// At least one readable cut is required, including for an empty geometry.
template <class T, std::size_t Extent> auto from_cuts(std::span<T, Extent> cuts) {
    assert(!cuts.empty() && cuts.size() - 1 <= maximum_count);
    using Point = std::remove_cv_t<T>;
    return from_ends(direct_source<Point>{cuts.data() + 1, unsigned(cuts.size() - 1)},
                     detail::read_element(cuts.data(), 0));
}
namespace detail {
template <class Source, class Body>
[[gnu::always_inline]] decltype(auto) geometry_body(const Source &source, Body &&body) {
    if constexpr (requires { source.with_geometry(std::forward<Body>(body)); })
        return source.with_geometry(std::forward<Body>(body));
    else
        return body(source);
}
template <class Source> [[gnu::always_inline]] decltype(auto) geometry_extent(const Source &source) {
    if constexpr (requires { source.extent(); })
        return source.extent();
    else
        return source.size() ? source.at(source.size() - 1) : source.origin();
}
template <class Source> [[gnu::always_inline]] bool selected(const Source &source, unsigned rank) {
    if constexpr (requires { source.is_selected(rank); })
        return source.is_selected(rank);
    else
        return next(source, rank, rank + 1) == rank;
}
template <class Source> [[gnu::always_inline]] bool positive(const Source &source, unsigned rank) {
    if constexpr (requires { source.slot_size(rank); })
        return source.slot_size(rank) != 0;
    else
        return meets<edge::after>(source.at(rank), rank ? source.at(rank - 1) : source.origin());
}
template <class Source, class Q>
[[gnu::always_inline]] unsigned geometry_reverse(const Source &source, const Q &point) {
    const auto n = source.size();
    if (!n || meets<edge::after>(source.origin(), point) ||
        meets<edge::at_or_after>(point, geometry_extent(source)))
        return n;
    unsigned rank;
    if constexpr (std::integral<Q> && requires { source.owner_rank(unsigned{}); })
        rank = source.owner_rank(point);
    else
        rank = boundary<edge::after>(source, point);
    return rank != n && selected(source, rank) ? rank : n;
}
template <class Source, class Low, class High>
[[gnu::always_inline]] rank_window geometry_overlap(const Source &source, const Low &low,
                                                    const High &high) {
    const auto n = source.size();
    if (!n || !meets<edge::after>(high, low))
        return {n, n};
    const auto origin = source.origin();
    const auto end = geometry_extent(source);
    if (meets<edge::at_or_after>(origin, high) || meets<edge::at_or_after>(low, end))
        return {n, n};
    unsigned first, stop;
    if constexpr (std::same_as<std::remove_cvref_t<Low>, std::remove_cvref_t<decltype(origin)>> &&
                  std::same_as<std::remove_cvref_t<High>, std::remove_cvref_t<decltype(end)>>) {
        first = boundary<edge::after>(source, std::max(low, origin));
        stop = boundary<edge::at_or_after>(source, std::min(high, end));
    } else {
        first = meets<edge::after>(origin, low) ? boundary<edge::after>(source, origin)
                                                : boundary<edge::after>(source, low);
        stop = meets<edge::after>(high, end) ? boundary<edge::at_or_after>(source, end)
                                             : boundary<edge::at_or_after>(source, high);
    }
    auto limit = stop == n ? n : stop + 1;
    if constexpr (requires { source.next_eligible(0u, n); }) {
        first = next(source, first, limit);
        while (first < limit && !positive(source, first))
            first = next(source, first + 1, limit);
        if (first >= limit)
            return {n, n};
        auto last = previous(source, limit);
        while (last != n && !positive(source, last))
            last = previous(source, last);
        if (last == n || last < first)
            return {n, n};
        limit = last + 1;
    }
    return first < limit ? rank_window{first, limit} : rank_window{n, n};
}
} // namespace detail
/// Checked interval bounds. Selection is supplied explicitly by a wrapper.
template <class Source>
[[gnu::always_inline]] auto locate(const Source &source, unsigned rank)
    -> std::optional<interval<std::remove_cvref_t<decltype(source.at(0))>>> {
    using Point = std::remove_cvref_t<decltype(source.at(0))>;
    if (rank >= source.size() || !detail::selected(source, rank))
        return {};
    return detail::geometry_body(source, [&](const auto &g) __attribute__((always_inline)) -> std::optional<interval<Point>> {
        if constexpr (requires { g.start(rank); g.slot_size(rank); }) {
            const auto start = g.start(rank);
            return interval<Point>{Point(start), Point(start + g.slot_size(rank))};
        } else
            return interval<Point>{rank ? g.at(rank - 1) : g.origin(), g.at(rank)};
    });
}
/// Containing rank, or size() outside the geometry/explicit selection. Excluding
/// an owner never redirects the point to a later interval.
template <class Source, class Q>
[[gnu::always_inline]] unsigned reverse(const Source &source, const Q &point) {
    return detail::geometry_body(source, [&](const auto &g) __attribute__((always_inline)) {
        return detail::geometry_reverse(g, point);
    });
}
template <class Source, class Low, class High>
[[gnu::always_inline]] rank_window overlap(const Source &source, const Low &low, const High &high) {
    return detail::geometry_body(source, [&](const auto &g) __attribute__((always_inline)) {
        return detail::geometry_overlap(g, low, high);
    });
}
template <class Source, class Sink>
[[nodiscard]] std::expected<void, error> range(const Source &source, unsigned first,
                                               unsigned count, Sink &&sink) {
    if (first > source.size() || count > source.size() - first)
        return std::unexpected(error::rank);
    detail::geometry_body(source, [&](const auto &g) __attribute__((always_inline)) {
        auto emit = [&](unsigned rank, auto bounds) __attribute__((always_inline)) {
            if (detail::selected(g, rank))
                sink(rank, bounds);
        };
        if constexpr (requires { g.visit_intervals(first, count, emit); })
            g.visit_intervals(first, count, emit);
        else if (count) {
            auto start = first ? g.at(first - 1) : g.origin();
            for (unsigned r = first; r < first + count; ++r) {
                auto end = g.at(r);
                emit(r, interval{start, end});
                start = std::move(end);
            }
        }
    });
    return {};
}
} // namespace ikea::boundlist
