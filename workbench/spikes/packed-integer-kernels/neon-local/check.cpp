#include "verify.h"
int main(){ikea::seriespack::detail::static_for<5>([](auto i){candidate::verify64<i+3>();});std::puts("NEON coalescing widths3..7: basis, projection, exact, aligned and guarded regions passed");}
