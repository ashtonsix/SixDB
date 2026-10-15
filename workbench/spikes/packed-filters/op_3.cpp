#include "bench_support.h"
void run_3() {
    series<1,3>();
    series<2,3>();
    series<3,3>();
    series<4,3>();
    series<5,3>();
    series<6,3>();
    series<7,3>();
    series<8,3>();
    series<16,3>();
    series<32,3>();
    series<64,3>();
    tuple<0,8,3>();tuple<3,5,3>();
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    wide_series<8,3>();
    wide_series<16,3>();
    wide_series<32,3>();
    wide_series<64,3>();
#endif
}
