// Lane and runs sweeps, and dispatch to the per-layout tables.
#include "sweeps.h"

#include <cstdlib>

namespace bodies {
namespace detail {
const packed_entry *packed_table_local8();
const packed_entry *packed_table_plane64();
const packed_entry *packed_table_plane256();
const packed_leaf *packed_leaves_local8();
const packed_leaf *packed_leaves_plane64();
const packed_leaf *packed_leaves_plane256();

template <class T> using lane_sweeps = packed_sweeps<lanes<T>, block_rows * sizeof(T)>;
constexpr packed_entry lane_entries[4] = {lane_sweeps<u8>::entry, lane_sweeps<u16>::entry, lane_sweeps<u32>::entry,
                                          lane_sweeps<u64>::entry};
constexpr packed_leaf lane_leaves[4] = {lane_sweeps<u8>::leaf, lane_sweeps<u16>::leaf, lane_sweeps<u32>::leaf,
                                        lane_sweeps<u64>::leaf};
constexpr runs_entry run_entries[3] = {runs_sweeps<u8>::entry, runs_sweeps<u16>::entry, runs_sweeps<u32>::entry};
constexpr runs_leaf run_leaves[3] = {runs_sweeps<u8>::leaf, runs_sweeps<u16>::leaf, runs_sweeps<u32>::leaf};
unsigned lane_index(unsigned k) { return k <= 8 ? 0 : k <= 16 ? 1 : k <= 32 ? 2 : 3; }
unsigned run_index(unsigned lane) { return lane == 1 ? 0 : lane == 2 ? 1 : 2; }
}  // namespace detail

const packed_entry &packed_catalogue(layout l, unsigned k) {
    switch (l) {
    case layout::local8: return detail::packed_table_local8()[k];
    case layout::plane64: return detail::packed_table_plane64()[k];
    case layout::plane256: return detail::packed_table_plane256()[k];
    case layout::lane: return detail::lane_entries[detail::lane_index(k)];
    }
    std::abort();
}
const packed_leaf &packed_leaves(layout l, unsigned k) {
    switch (l) {
    case layout::local8: return detail::packed_leaves_local8()[k];
    case layout::plane64: return detail::packed_leaves_plane64()[k];
    case layout::plane256: return detail::packed_leaves_plane256()[k];
    case layout::lane: return detail::lane_leaves[detail::lane_index(k)];
    }
    std::abort();
}
const runs_entry &runs_catalogue(unsigned lane) { return detail::run_entries[detail::run_index(lane)]; }
const runs_leaf &runs_leaves(unsigned lane) { return detail::run_leaves[detail::run_index(lane)]; }
}  // namespace bodies
