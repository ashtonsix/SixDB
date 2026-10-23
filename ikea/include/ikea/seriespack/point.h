#pragma once
#include <ikea/seriespack/detail/point.h>

namespace ikea::seriespack {
/// Scalar point building block over an admitted physical view. The caller proves
/// i < v.size(), keeps the view's placement and bytes live, and synchronizes
/// access to every occupied byte read. Coordinates name original logical rows.
template <class F, class B>
[[gnu::always_inline]] inline std::uint64_t get_unchecked(const basic_view<F, B>& v, std::size_t i) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    std::uint64_t x = 0;
    if constexpr (F::payload)
        x = detail::point_payload<F>(ikea::to_u8(v.stream(0).bytes).data() + tile * v.stream(0).stride, lane);
    if constexpr (F::heads) {
        std::uint64_t high = ikea::to_u8(v.stream(1).bytes)[tile * v.stream(1).stride + lane];
        if constexpr (F::heads == 16)
            high = (high << 8) | ikea::to_u8(v.stream(2).bytes)[tile * v.stream(2).stride + lane];
        x |= high << F::payload;
    }
    return x;
}
/// Checked scalar point read. An out-of-range row reads no source bytes.
/// Placement, readable extent, owner lifetime and synchronization remain borrowed
/// obligations of the admitted view; no allocation or suspension occurs.
template <class F, class B>
std::expected<std::uint64_t, error> get(const basic_view<F, B>& v, std::size_t i) {
    if (i >= v.size())
        return std::unexpected(error::range);
    return get_unchecked(v, i);
}
} // namespace ikea::seriespack
