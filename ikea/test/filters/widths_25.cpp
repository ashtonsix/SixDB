#include "support.h"
void check_widths_25() {
    sp::detail::each<8>([](auto k) { width<k + 25>(); });
}
