#pragma once
// Experimental namespace/ownership boundary. Reuse maintained non-owning bodies;
// the candidate intentionally exports no layout, tombstones, retire or live_ranks.
#include <ikea/boundlist/geometry.h>

namespace ikea::intervals {
using boundlist::edge;
using boundlist::error;
using boundlist::interval;
using boundlist::rank_window;
using boundlist::maximum_count;
using boundlist::direct_source;
using boundlist::delta_source;
using boundlist::relative_end_source;
using boundlist::rank_selection;
using boundlist::selected_source;
using boundlist::from_cuts;
using boundlist::from_ends;
using boundlist::lower_bound;
using boundlist::upper_bound;
using boundlist::locate;
using boundlist::reverse;
using boundlist::overlap;
using boundlist::range;
namespace detail = boundlist::detail;

/// Resolve representation once around complete owner work. The body may fuse
/// its own length/liveness/payload stages with ready geometry; no universal
/// intermediate packet or flattened descriptor is required.
template <class Source, class Body>
[[gnu::always_inline]] decltype(auto) with_geometry(const Source &source, Body &&body) {
    return detail::geometry_body(source,std::forward<Body>(body));
}

/// Start-only geometry projection for consumers with an independent used length.
/// Avoids reconstructing an interval's size just to address its payload.
template <class Source>
[[gnu::always_inline]] auto start(const Source &source, unsigned rank, rank_selection filter = rank_selection::all())
    -> std::optional<std::remove_cvref_t<decltype(source.at(0))>> {
    using Point = std::remove_cvref_t<decltype(source.at(0))>;
    if (rank >= source.size() || !filter.contains(rank) || !detail::selected(source, rank)) return {};
    return detail::geometry_body(source, [&](const auto &g) __attribute__((always_inline)) -> Point {
        if constexpr (requires { g.start(rank); }) return g.start(rank);
        else return rank ? g.at(rank - 1) : g.origin();
    });
}

/// Admitted typed composition: the owner already proved rank and selection.
/// This preserves start+U kernels without a second checked/optional projection.
template <class Source>
[[gnu::always_inline]] auto start_unchecked(const Source &source, unsigned rank) {
    if constexpr (requires { source.start(rank); }) return source.start(rank);
    else return rank ? source.at(rank - 1) : source.origin();
}

/// Operation-local borrowed selection. Invert a tombstone bitmap to select live
/// ranks. Geometry never owns, updates, moves or reassigns the filter's ranks.
template <class Source, class Q>
unsigned reverse(const Source &source, const Q &point, rank_selection filter) {
    return boundlist::reverse(selected_source{source, filter}, point);
}
template <class Source>
auto locate(const Source &source, unsigned rank, rank_selection filter) {
    return boundlist::locate(selected_source{source, filter}, rank);
}
template <class Source, class Low, class High>
auto overlap(const Source &source, const Low &low, const High &high, rank_selection filter) {
    return boundlist::overlap(selected_source{source, filter}, low, high);
}
template <class Source, class Sink>
auto range(const Source &source, unsigned first, unsigned count, Sink &&sink, rank_selection filter) {
    const auto n = source.size();
    if (first > n || count > n - first)
        return std::expected<void, error>{std::unexpected(error::rank)};
    if (filter.next(first, first + count) == first + count)
        return std::expected<void, error>{};
    return boundlist::range(selected_source{source, filter}, first, count, std::forward<Sink>(sink));
}
} // namespace ikea::intervals

namespace ikea::intervals {
/// Readiness is independent of scheduling. The named original source, backing,
/// filter and interpretation must remain coherent until finish(). A local window
/// is a useful next-read hint, not the complete protection/retention footprint.
template <class Source, class Pending> struct phase {
    const Source *source;
    Pending pending;
    bool has_local_read() const noexcept {return pending.has_local_read();}
    auto local_window() const noexcept {return pending.local_window();}
    unsigned finish() const {return detail::next(*source,pending.finish(),source->size());}
};
template <class Source, class Q> auto prepare_lower_bound(const Source &source,const Q &point) {
    return phase{&source,detail::prepare<edge::at_or_after>(source,point)};
}
template <class Source, class Q> auto prepare_upper_bound(const Source &source,const Q &point) {
    return phase{&source,detail::prepare<edge::after>(source,point)};
}
template <class Source,class Q> void prepare_lower_bound(const Source &&,const Q &)=delete;
template <class Source,class Q> void prepare_upper_bound(const Source &&,const Q &)=delete;
} // namespace ikea::intervals
