#include "support.h"
void check_widths_49() {
    sp::detail::each<8>([](auto k) { width<k + 49>(); });
}
