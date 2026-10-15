#include "bench_support.h"
void run_2() {
    series<1,2>();
    series<2,2>();
    series<3,2>();
    series<4,2>();
    series<5,2>();
    series<6,2>();
    series<7,2>();
    series<8,2>();
    series<16,2>();
    series<32,2>();
    series<64,2>();
    tuple<0,8,2>();tuple<3,5,2>();
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    wide_series<8,2>();
    wide_series<16,2>();
    wide_series<32,2>();
    wide_series<64,2>();
#endif
}
