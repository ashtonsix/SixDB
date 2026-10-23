#include "placed_benchmark.h"
void register_placed_wide() {
    register_placed_format<sp::layout<63, sp::geometry::local, 16>>();
}
