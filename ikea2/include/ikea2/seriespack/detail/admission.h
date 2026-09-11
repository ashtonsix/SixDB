#pragma once
#include <ikea2/seriespack/status.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace ikea2::seriespack::detail {
struct occupied_run {
    std::uintptr_t base;
    std::size_t stride, bytes, count;
};
/// Cold overlap proof for repeating occupied fields, including interleaved gaps.
bool overlaps(occupied_run a, occupied_run b) noexcept;
struct stream_extent {
    const void* data;
    std::size_t bytes, stride;
};
std::expected<void, error> admit_view(std::size_t count, std::size_t rows,
                                      std::array<std::size_t, 3> used,
                                      std::array<stream_extent, 3> streams, bool striped);
} // namespace ikea2::seriespack::detail
