#include "range.h"

// Striped widths.
void check_ranges_striped() {
#if defined(__aarch64__) || defined(__AVX2__)
    check_guarded<sp::layout<1, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<3, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<6, sp::geometry::striped>>(5 * 256 + 100);
    check_guarded<sp::layout<10, sp::geometry::striped>>(1100);
    check_guarded<sp::layout<15, sp::geometry::striped>>(1100);
    check_guarded<sp::layout<20, sp::geometry::striped>>(1100);
#endif
}
