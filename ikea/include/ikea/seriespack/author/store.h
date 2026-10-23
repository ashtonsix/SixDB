#pragma once
#include <ikea/seriespack/point.h>

namespace ikea::seriespack::author {
/// Raw scalar leaf store: caller proves i < v.size() and x fits F::width.
/// Occupied bytes are writable; shared packed bytes read for preservation are
/// initialized and isolated from concurrent writers. Other fields are preserved.
/// This emits no write effects or summaries and does not initialize tile slack.
/// Use seriespack::bind_writer for operation-level stores with source-qualified effects.
template <class F>
inline void store_unchecked(const basic_view<F, ikea::byte>& v, std::size_t i, std::uint64_t x) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    if constexpr (F::payload)
        ikea::seriespack::detail::put_payload<F>(ikea::to_u8(v.stream(0).bytes).data() + tile * v.stream(0).stride, lane, x);
    if constexpr (F::heads) {
        ikea::to_u8(v.stream(1).bytes)[tile * v.stream(1).stride + lane] =
            static_cast<std::uint8_t>(x >> (F::width - 8));
        if constexpr (F::heads == 16)
            ikea::to_u8(v.stream(2).bytes)[tile * v.stream(2).stride + lane] =
                static_cast<std::uint8_t>(x >> F::payload);
    }
}
/// Checked raw scalar leaf store. Range/value failure reads and writes no data.
/// It has the same borrowing, initialized-byte and isolation requirements as
/// store_unchecked; successful stores emit no write effects or summary contributions.
template <class F>
std::expected<void, error> store(const basic_view<F, ikea::byte>& v, std::size_t i, std::uint64_t x) {
    if (i >= v.size())
        return std::unexpected(error::range);
    if constexpr (F::width < 64)
        if (x >> F::width)
            return std::unexpected(error::value);
    store_unchecked(v, i, x);
    return {};
}
} // namespace ikea::seriespack::author
