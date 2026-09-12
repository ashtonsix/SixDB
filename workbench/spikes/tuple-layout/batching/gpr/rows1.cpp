#include "cases.h"
void gpr_rows1() {
#if defined(__aarch64__) || defined(__AVX2__)
    methods<1>();
#endif
}
