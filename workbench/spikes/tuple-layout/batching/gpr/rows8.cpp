#include "cases.h"
void gpr_rows8() {
#if defined(__aarch64__) || defined(__AVX2__)
    methods<8>();
#endif
}
