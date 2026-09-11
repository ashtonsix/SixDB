#include <cstdio>

namespace seriespack_measurement { bool run_reduction_probe_checks(); }

int main() {
#if defined(__aarch64__) || defined(__AVX2__)
    const bool good = seriespack_measurement::run_reduction_probe_checks();
    std::puts(good ? "PASS: reduction carrier runtime cutoffs and modulo-overflow witnesses"
                   : "FAIL: reduction carrier differs from scalar oracle");
    return !good;
#else
    std::puts("SKIP: reduction carrier needs NEON or AVX2");
    return 77;
#endif
}
