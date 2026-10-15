#pragma once
#include <cstdint>

namespace ikea {
/// Unsigned comparisons; operands retain their full width (no truncation of rhs).
enum class comparison { equal, less, less_equal, greater, greater_equal };

template <comparison Op> constexpr bool compare(std::uint64_t value, std::uint64_t rhs) noexcept {
    if constexpr (Op == comparison::equal)
        return value == rhs;
    if constexpr (Op == comparison::less)
        return value < rhs;
    if constexpr (Op == comparison::less_equal)
        return value <= rhs;
    if constexpr (Op == comparison::greater)
        return value > rhs;
    if constexpr (Op == comparison::greater_equal)
        return value >= rhs;
}
} // namespace ikea
