#include "cases.h"
void check_mutation_dense() {
#if defined(__aarch64__) || defined(__AVX2__)
    // Dense bulk execution must preserve the same range, selection, summary
    // and rejection laws as placed execution, across native lane boundaries.
    check_mutation_format<sp::format<8>, true>();
    check_mutation_format<sp::format<12>, true>();
    check_mutation_format<sp::format<16>, true>();
    check_mutation_format<sp::format<31>, true>();
    check_mutation_format<sp::format<32>, true>();
    check_mutation_format<sp::format<33>, true>();
    check_mutation_format<sp::format<56>, true>();
    check_mutation_format<sp::format<64>, true>();
#endif
}
