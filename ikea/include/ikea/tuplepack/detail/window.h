#pragma once
#include <ikea/tuplepack/selection.h>
#include <bit>
#include <algorithm>
#include <optional>
#include <array>
#include <utility>
#include <type_traits>

namespace ikea::tuplepack::detail {
template <unsigned Rows> inline constexpr std::uint64_t all_rows = ~std::uint64_t(0) >> (64 - Rows);

/// The active mask names original rows first+r. Empty windows may begin at end;
/// inactive tail coordinates need no storage and never form payload pointers.
template <unsigned Rows>
inline bool valid_window(std::size_t size, std::size_t first, std::uint64_t active) noexcept {
    return !(active & ~all_rows<Rows>) && first <= size &&
           unsigned(std::bit_width(active)) <= size - first;
}
/// One output/input packet per Rows original rows, with a short final packet.
/// Reject surplus packets rather than silently consuming empty windows.
template <unsigned Rows>
inline std::optional<std::size_t> range_rows(std::size_t size, std::size_t first,
                                             std::size_t packets) noexcept {
    if (first > size)
        return {};
    const auto available = size - first;
    if (packets > available / Rows + (available % Rows != 0))
        return {};
    return packets > available / Rows ? available : packets * Rows;
}
template <unsigned Rows>
inline std::uint64_t selected_window(selection selected, std::size_t first,
                                     std::size_t count) noexcept {
    return selected.mask(first, unsigned(std::min(std::size_t(Rows), count)));
}
/// Row observers retain only demanded before-state. A native batch observer can
/// instead provide before_batch/after_batch and keep its packet-shaped projection.
/// Every requested old observation precedes every child store in this window.
template <unsigned Rows, class Maintenance, class Mutate>
[[gnu::always_inline]] inline void maintain(std::size_t first, std::uint64_t active,
                                            Maintenance& maintenance, Mutate&& mutate) {
    if (!active)
        return;
    if constexpr (requires { maintenance.before_batch(first, active); }) {
        if constexpr (requires { std::remove_cvref_t<Maintenance>::rows; })
            static_assert(std::remove_cvref_t<Maintenance>::rows == Rows,
                          "Batch observation and mutation must share their original-row shape");
        auto before = maintenance.before_batch(first, active);
        mutate();
        maintenance.after_batch(first, active, std::move(before));
    } else if constexpr (Rows == 1) {
        auto before = maintenance.before(first);
        mutate();
        maintenance.after(first, std::move(before));
    } else {
        using State = decltype(maintenance.before(first));
        std::array<std::optional<State>, Rows> before;
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                before[r].emplace(maintenance.before(first + r));
        mutate();
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                maintenance.after(first + r, std::move(*before[r]));
    }
}
} // namespace ikea::tuplepack::detail
