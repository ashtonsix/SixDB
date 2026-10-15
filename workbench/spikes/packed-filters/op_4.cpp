#include "bench_support.h"
void run_4() {
    series<1,4>();
    series<2,4>();
    series<3,4>();
    series<4,4>();
    series<5,4>();
    series<6,4>();
    series<7,4>();
    series<8,4>();
    series<16,4>();
    series<32,4>();
    series<64,4>();
    tuple<0,8,4>();tuple<3,5,4>();
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    wide_series<8,4>();
    wide_series<16,4>();
    wide_series<32,4>();
    wide_series<64,4>();
#endif
}
