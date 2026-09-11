#include "placed_benchmark.h"
void register_placed_low() {
    register_placed_format<sp::format<20, sp::geometry::striped, 8>>();
}
