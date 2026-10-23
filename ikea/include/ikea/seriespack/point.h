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
/// Exact bytes get_unchecked(v, i) reads, as (address, size) spans in plane
/// order: payload body, residual fragments, then head bytes. Nothing is read.
/// A caller can prefetch them, or hand them to a scheduler, before the point
/// read itself; emitting a span is neither a lease nor proof of readiness.
template <class F, class B, class Emit>
[[gnu::always_inline]] inline void visit_point_reads(const basic_view<F, B>& v, std::size_t i,
                                                     Emit&& emit) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    if constexpr (F::payload) {
        const auto* base = v.stream(0).bytes.data() + tile * v.stream(0).stride;
        if constexpr (F::body)
            emit(static_cast<const ikea::byte*>(base + detail::body_offset<F>(lane)),
                 std::size_t{F::body});
        if constexpr (F::tail) {
            if constexpr (F::storage == geometry::local)
                emit(static_cast<const ikea::byte*>(base + 8 * F::body), std::size_t{F::tail});
            else
                detail::tail_fragments<F::tail>(unsigned(lane / 32), [&](unsigned stripe, unsigned, unsigned) {
                    emit(static_cast<const ikea::byte*>(base + detail::stripe_offset<F>(stripe) + lane % 32),
                         std::size_t{1});
                });
        }
    }
    detail::each<F::heads / 8>([&](auto p) {
        const auto& head = v.stream(p + 1);
        emit(static_cast<const ikea::byte*>(head.bytes.data() + tile * head.stride + lane), std::size_t{1});
    });
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
