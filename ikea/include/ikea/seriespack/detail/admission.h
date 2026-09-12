#pragma once
#include <ikea/detail/overlap.h>
#include <ikea/seriespack/status.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace ikea::seriespack::detail {
using ikea::detail::occupied_run;
using ikea::detail::overlaps;
struct stream_extent {
    const void* data;
    std::size_t bytes, stride;
};
std::expected<void, error> admit_view(std::size_t count, std::size_t rows,
                                      std::array<std::size_t, 3> used,
                                      std::array<stream_extent, 3> streams, bool striped);
} // namespace ikea::seriespack::detail
