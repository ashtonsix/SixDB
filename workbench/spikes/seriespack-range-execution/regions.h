#pragma once

#include <ikea/seriespack/operations.h>

namespace seriespack_measurement {

// Workbench diagnostic, not a SeriesPack API. Both callbacks materialize the
// sixteen original positions [origin, origin+16) as unsigned 64-bit values, in order.
//
// Binding admits the actual descriptor and compiled execution target. Each
// invocation owes the same descriptor, origin%16==0, origin+16<=source.size(),
// sixteen writable output elements disjoint from the encoded source, and the
// ordinary lifetime/placement/canonical-source obligations of const_view.
// read retains runtime base and stride (including non-dense placement).
// raw_dense additionally admits a dense payload starting at original index 0;
// it has the predecessor's pointer/index/output ABI and no view metadata.
// Neither callback validates, allocates, or retains source/output references.
struct materialized_region {
    void (*read)(const ikea::seriespack::const_view&, std::size_t origin,
                 std::uint64_t* output) = nullptr;
    void (*raw_dense)(const std::uint8_t*, std::size_t origin,
                      std::uint64_t* output) = nullptr;
};

// Supported diagnostic scope: headless Local 1..7/56 and selected striped
// widths 1..7,10,12,14,15,20. Unsupported descriptions/targets return empty
// callbacks at setup; automatic is not a compiled target selection.
[[nodiscard]] materialized_region static_materialized16(
    ikea::seriespack::description, ikea::seriespack::execution_target);

} // namespace seriespack_measurement
