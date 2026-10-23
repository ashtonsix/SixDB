// The plane256 sweeps and leaves for widths 1..32.
#include "sweeps.h"

namespace bodies::detail {
const packed_entry *packed_table_plane256() {
    static constexpr auto t = packed_entries<layout::plane256>(std::make_index_sequence<32>{});
    return t.data();
}
const packed_leaf *packed_leaves_plane256() {
    static constexpr auto t = packed_leaf_table<layout::plane256>(std::make_index_sequence<32>{});
    return t.data();
}
}  // namespace bodies::detail
