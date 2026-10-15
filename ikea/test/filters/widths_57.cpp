#include "support.h"
void check_widths_57() {
    sp::detail::each<8>([](auto k) { width<k + 57>(); });
}
