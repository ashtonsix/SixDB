#pragma once

#include <ikea/seriespack/operations.h>

namespace seriespack_measurement {

struct point_region {
    std::uint64_t (*sum)(const ikea::seriespack::const_view&, const std::size_t*, std::size_t) = nullptr;
};

// Bind a whole query region. Invocation supplies the same admitted layout and
// valid original indices; the static getter is inlined inside its index loop.
// A malformed description/policy returns an empty region during setup.
[[nodiscard]] point_region static_points(ikea::seriespack::description layout,
                                        ikea::seriespack::point_reader strategy);

} // namespace seriespack_measurement
