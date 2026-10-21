#pragma once
#include <ikea/streampack/author/read.h>
#include <ikea/boundlist/read.h>

namespace ikea::streampack {
template <class Byte>
[[nodiscard]] std::expected<bytes, error> value(const basic_view<Byte>& source, unsigned rank) {
    if (rank >= source.size())
        return std::unexpected(error::rank);
    if (!source.live_unchecked(rank))
        return std::unexpected(error::retired);
    return author::dispatch(source, [&](auto a) { return a.value(rank); });
}
template <class Byte>
[[nodiscard]] std::expected<unsigned, error> length(const basic_view<Byte>& source, unsigned rank) {
    if (rank >= source.size())
        return std::unexpected(error::rank);
    if (!source.live_unchecked(rank))
        return std::unexpected(error::retired);
    return author::dispatch(source, [&](auto a) { return a.length(rank); });
}
template <class Byte>
[[nodiscard]] std::optional<byte_owner> reverse(const basic_view<Byte>& source,
                                                std::uint32_t offset) {
    if (offset >= source.extent())
        return {};
    return author::dispatch(source, [&](auto a) { return a.reverse(offset); });
}
/// First/last positive reservations overlapping [low,high). Physical by default;
/// only_live excludes retirement, while holes inside the window remain holes.
template <class Byte>
boundlist::rank_window overlap(const basic_view<Byte>& source, std::uint32_t low,
                               std::uint32_t high, bool only_live = false) {
    return author::dispatch(source, [&](auto a) {
        if (only_live) {
            const boundlist::selected_source selected{a, a.live_ranks()};
            return boundlist::overlap(selected, low, high);
        }
        return boundlist::overlap(a, low, high);
    });
}
template <class Byte, class Sink>
[[nodiscard]] std::expected<void, error> range(const basic_view<Byte>& source, unsigned first,
                                               unsigned count, Sink&& sink) {
    if (first > source.size() || count > source.size() - first)
        return std::unexpected(error::rank);
    author::dispatch(source, [&](auto a) { a.range(first, count, sink); });
    return {};
}
template <class Byte, class Sink>
[[nodiscard]] std::expected<void, error> select(const basic_view<Byte>& source,
                                                std::span<const std::uint16_t> ranks, Sink&& sink) {
    for (auto rank : ranks)
        if (rank >= source.size())
            return std::unexpected(error::rank);
    author::dispatch(source, [&](auto a) { a.select(ranks, sink); });
    return {};
}
/// Paid dense selection: starts are caller-owned scratch and expire when geometry
/// changes. Preparation/output do not escape this call; arbitrary order/duplicates
/// remain observable. Sparse select() requires no scratch or full-block decode.
template <class Byte, class Sink>
[[nodiscard]] std::expected<void, error> select(const basic_view<Byte>& source,
                                                std::span<const std::uint16_t> ranks,
                                                std::span<std::uint32_t> scratch, Sink&& sink) {
    const auto n = source.size();
    if (scratch.size() < ((n + 15) / 16) * 16)
        return std::unexpected(error::capacity);
    for (auto rank : ranks)
        if (rank >= n)
            return std::unexpected(error::rank);
    for (const auto p : source.windows().planes())
        if (detail::overlaps(scratch.data(), scratch.size_bytes(), p.data(), p.size()))
            return std::unexpected(error::alias);
    if (detail::overlaps(scratch.data(), scratch.size_bytes(), ranks.data(), ranks.size_bytes()))
        return std::unexpected(error::alias);
    author::dispatch(source, [&](auto a) {
        for (unsigned g = 0; g < (n + 15) / 16; ++g)
            a.starts16(g, scratch.data() + g * 16);
        for (auto rank : ranks)
            if (a.live(rank))
                sink(rank, source.windows().payload.subspan(scratch[rank], a.length(rank)));
    });
    return {};
}
} // namespace ikea::streampack
