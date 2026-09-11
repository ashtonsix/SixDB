#include "placed_benchmark.h"
void register_placed_wide() {
    register_placed_format<sp::format<63, sp::geometry::local, 16>>();
}
