#include "bench_support.h"
void run_1() {
    series<1,1>();
    series<2,1>();
    series<3,1>();
    series<4,1>();
    series<5,1>();
    series<6,1>();
    series<7,1>();
    series<8,1>();
    series<16,1>();
    series<32,1>();
    series<64,1>();
    tuple<0,8,1>();tuple<3,5,1>();
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    wide_series<8,1>();
    wide_series<16,1>();
    wide_series<32,1>();
    wide_series<64,1>();
#endif
}
