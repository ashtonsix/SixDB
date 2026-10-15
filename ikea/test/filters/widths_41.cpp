#include "support.h"
void check_widths_41() {
    sp::detail::each<8>([](auto k) { width<k + 41>(); });
}
