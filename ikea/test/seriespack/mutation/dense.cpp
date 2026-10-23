#include "cases.h"
void check_mutation_dense() {
#if defined(__aarch64__) || defined(__AVX2__)
    // Dense bulk execution must preserve the same range, selection, summary
    // and rejection laws as placed execution, across native lane boundaries.
    check_mutation_format<sp::layout<8>, true>();
    check_mutation_format<sp::layout<12>, true>();
    check_mutation_format<sp::layout<16>, true>();
    check_mutation_format<sp::layout<31>, true>();
    check_mutation_format<sp::layout<32>, true>();
    check_mutation_format<sp::layout<33>, true>();
    check_mutation_format<sp::layout<56>, true>();
    check_mutation_format<sp::layout<64>, true>();
#endif
}
