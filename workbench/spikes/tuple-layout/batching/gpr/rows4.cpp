#include "cases.h"
void gpr_rows4() {
#if defined(__aarch64__) || defined(__AVX2__)
    methods<4>();
#endif
}
