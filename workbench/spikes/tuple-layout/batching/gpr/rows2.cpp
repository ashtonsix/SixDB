#include "cases.h"
void gpr_rows2() {
#if defined(__aarch64__) || defined(__AVX2__)
    methods<2>();
#endif
}
