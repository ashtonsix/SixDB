#pragma once
#include <cstddef>
#include <cstdint>
namespace ikea::detail {
struct occupied_run {
    std::uintptr_t base;
    std::size_t stride, bytes, count;
};
/// Cold overlap proof. Nonzero stride; the enclosing admission has already
/// checked all address/extent arithmetic. Skips interleaved unoccupied gaps.
bool overlaps(occupied_run a, occupied_run b) noexcept;
} // namespace ikea::detail
