#pragma once
#include <ikea/boundlist/author/read.h>

namespace ikea::boundlist::sidecar {
template <class Byte>
[[nodiscard]] std::expected<location, error> locate(const basic_view<Byte> &source, unsigned rank) {
    if (rank >= source.size())
        return std::unexpected(error::rank);
    if (!source.live_unchecked(rank))
        return std::unexpected(error::retired);
    return author::dispatch(source, [&](auto a) { return a.locate(rank); });
}
template <class Byte>
[[nodiscard]] std::optional<position_owner> reverse(const basic_view<Byte> &source,
                                                    std::uint32_t point) {
    if (point >= source.extent())
        return {};
    return author::dispatch(source, [&](auto a) { return a.reverse(point); });
}
/// Exact positive physical reservations overlapping [low,high). Retired ranks
/// participate unless only_live is requested. A live zero reservation is not a
/// positive physical interval; logical occupied extrema use boundlist::overlap.
namespace author {
template <class Access>
[[gnu::always_inline]] rank_window overlap(Access a, std::uint32_t low, std::uint32_t high,
                                           bool only_live = false) {
    const auto n = a.size(), end = a.source().extent();
    if (!n || low >= high || low >= end)
        return {n, n};
    high = std::min(high, end);
    auto first = a.template boundary<edge::after>(low);
    const auto stop = a.template boundary<edge::at_or_after>(high);
    auto limit = std::min(n, stop + 1);
    if (only_live) {
        first = a.next_eligible(first, limit);
        while (first < limit && !a.slot_size(first))
            first = a.next_eligible(first + 1, limit);
        if (first >= limit)
            return {n, n};
        auto last = a.previous_eligible(limit);
        while (last != n && !a.slot_size(last))
            last = a.previous_eligible(last);
        if (last == n || last < first)
            return {n, n};
        limit = last + 1;
    }
    return first < limit ? rank_window{first, limit} : rank_window{n, n};
}
} // namespace author
template <class Byte>
rank_window overlap(const basic_view<Byte> &source, std::uint32_t low, std::uint32_t high,
                    bool only_live = false) {
    return author::dispatch(source,
                            [&](auto a) { return author::overlap(a, low, high, only_live); });
}
template <class Byte, class Sink>
[[nodiscard]] std::expected<void, error> range(const basic_view<Byte> &source, unsigned first,
                                               unsigned count, Sink &&sink) {
    if (first > source.size() || count > source.size() - first)
        return std::unexpected(error::rank);
    author::dispatch(source, [&](auto a) { a.range(first, count, sink); });
    return {};
}
} // namespace ikea::boundlist::sidecar
