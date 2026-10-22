#pragma once
#include <ikea/seriespack/detail/point.h>

namespace ikea::seriespack {
/// Scalar point building block over an admitted physical view. The caller proves
/// i < v.size(), keeps the view's placement and bytes live, and synchronizes
/// access to every occupied byte read. Coordinates name original logical rows.
template <class F, class B>
[[gnu::always_inline]] inline std::uint64_t get_unchecked(const view<F, B>& v, std::size_t i) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    std::uint64_t x = 0;
    if constexpr (F::payload)
        x = detail::point_payload<F>(v.stream(0).bytes.data() + tile * v.stream(0).stride, lane);
    if constexpr (F::heads) {
        std::uint64_t high = v.stream(1).bytes[tile * v.stream(1).stride + lane];
        if constexpr (F::heads == 16)
            high = (high << 8) | v.stream(2).bytes[tile * v.stream(2).stride + lane];
        x |= high << F::payload;
    }
    return x;
}
/// Checked scalar point read. An out-of-range row reads no source bytes.
/// Placement, readable extent, owner lifetime and synchronization remain borrowed
/// obligations of the admitted view; no allocation or suspension occurs.
template <class F, class B>
std::expected<std::uint64_t, error> get(const view<F, B>& v, std::size_t i) {
    if (i >= v.size())
        return std::unexpected(error::range);
    return get_unchecked(v, i);
}
/// Raw scalar leaf store: caller proves i < v.size() and x fits F::width.
/// Occupied bytes are writable; shared packed bytes read for preservation are
/// initialized and isolated from concurrent writers. Other fields are preserved.
/// This emits no write effects or summaries and does not initialize tile slack.
/// Use bind_mutation for operation-level stores with source-qualified effects.
template <class F>
inline void set_unchecked(const view<F, std::uint8_t>& v, std::size_t i, std::uint64_t x) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    if constexpr (F::payload)
        detail::put_payload<F>(v.stream(0).bytes.data() + tile * v.stream(0).stride, lane, x);
    if constexpr (F::heads) {
        v.stream(1).bytes[tile * v.stream(1).stride + lane] =
            static_cast<std::uint8_t>(x >> (F::width - 8));
        if constexpr (F::heads == 16)
            v.stream(2).bytes[tile * v.stream(2).stride + lane] =
                static_cast<std::uint8_t>(x >> F::payload);
    }
}
/// Checked raw scalar leaf store. Range/value failure reads and writes no data.
/// It has the same borrowing, initialized-byte and isolation requirements as
/// set_unchecked; successful stores emit no write effects or summary contributions.
template <class F>
std::expected<void, error> set(const view<F, std::uint8_t>& v, std::size_t i, std::uint64_t x) {
    if (i >= v.size())
        return std::unexpected(error::range);
    if constexpr (F::width < 64)
        if (x >> F::width)
            return std::unexpected(error::value);
    set_unchecked(v, i, x);
    return {};
}
} // namespace ikea::seriespack
