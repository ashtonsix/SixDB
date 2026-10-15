#include "bench_support.h"
void run_0() {
    series<1,0>();
    series<2,0>();
    series<3,0>();
    series<4,0>();
    series<5,0>();
    series<6,0>();
    series<7,0>();
    series<8,0>();
    series<16,0>();
    series<32,0>();
    series<64,0>();
    tuple<0,8,0>();tuple<3,5,0>();
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    wide_series<8,0>();
    wide_series<16,0>();
    wide_series<32,0>();
    wide_series<64,0>();
#endif
}
