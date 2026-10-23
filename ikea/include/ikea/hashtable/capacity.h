#pragma once
#include "author/table.h"
#include "detail/control.h"
#include <algorithm>
#include <limits>
namespace ikea::hashtable {
/// Separate storage preparation; final live-spill bound, primary geometry fixed.
template <class D>
void reserve_overflow(D& dst, std::size_t bound,
                      typename D::storage_policy::context* memory = nullptr) {
    auto to = author::table_access::get(dst);
    require(bound >= to.spill, "overflow bound below population");
    std::size_t groups = bound ? 1 : 0;
    while (groups && bound > groups * 14) {
        require(groups <= std::numeric_limits<std::size_t>::max() / 32, "overflow extent");
        groups *= 2;
    }
    if (groups <= to.groups && !to.deleted)
        return;
    groups = std::max(groups, to.groups);
    if (!groups)
        return;
    require(groups <= std::numeric_limits<std::size_t>::max() / (16 * D::tuple_bytes),
            "overflow tuple extent");
    typename D::storage_policy::buffer controls(groups * 16, memory),
        slots(groups * 16 * D::tuple_bytes, memory);
    std::memset(ikea::to_u8(controls.data), detail::control_empty, groups * 16);
    for (std::size_t i = 0; i < to.groups * 16; ++i)
        if (detail::occupied_control(ikea::to_u8(to.controls.data)[i])) {
            const auto* tuple = ikea::to_u8(to.slots.data) + i * D::tuple_bytes;
            const auto h = to.mix(load<D::key_bytes>(tuple));
            auto g = h.swiss & (groups - 1);
            std::size_t step = 0;
            while (true) {
                const auto free = equal_controls(ikea::to_u8(controls.data) + g * 16, detail::control_empty);
                if (free) {
                    const auto row = g * 16 + std::countr_zero(free);
                    std::memcpy(ikea::to_u8(slots.data) + row * D::tuple_bytes, tuple, D::tuple_bytes);
                    ikea::to_u8(controls.data)[row] = h.fingerprint();
                    break;
                }
                g = (g + (++step)) & (groups - 1);
            }
        }
    to.controls = std::move(controls);
    to.slots = std::move(slots);
    to.groups = groups;
    to.deleted = 0;
}
} // namespace ikea::hashtable
