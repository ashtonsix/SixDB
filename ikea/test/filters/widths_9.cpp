#include "support.h"
void check_widths_9() {
    sp::detail::each<8>([](auto k) { width<k + 9>(); });
}
