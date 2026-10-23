#pragma once
#include <ikea/tuplepack/plan.h>
#include <ikea/tuplepack/view.h>
#include <ikea/tuplepack/detail/native/packet.h>

namespace ikea::tuplepack::detail {
// Shared register-valued body for ordinary predicates and authored readers.
// Requires admitted active rows. Empty activity forms no source address.
template <author::read_access Access = author::read_access::active_units,
          unsigned N, unsigned Rows, class Byte>
[[gnu::always_inline]] inline native::packet_for<N>
read_native(const read_plan<N, Rows>& plan, const basic_view<Byte>& source,
            std::size_t first, std::uint64_t active) {
    if (!active)
        return native::packet_for<N>{};
    if constexpr (Rows == 1)
        return native::read_body(plan.controls(), ikea::to_u8(source.row_unchecked(first)));
    else {
        const auto base = ikea::to_u8(source.row_unchecked(first));
        const auto stride = source.stride();
        return native::read_body<Rows, Access>(plan.controls(),
            [&](unsigned r) { return base + r * stride; }, active, stride);
    }
}
} // namespace ikea::tuplepack::detail
