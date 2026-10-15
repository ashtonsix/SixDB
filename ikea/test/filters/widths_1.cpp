#include "support.h"
void check_widths_1() {
    sp::detail::each<8>([](auto k) { width<k + 1>(); });
}
