#include "bec_region.h"

namespace ikea::heterogeneous {
extern "C" std::uint64_t ikea_heterogeneous_bec_count1(
    const std::uint8_t* body, unsigned population,
    const std::uint8_t* query32) noexcept {
    return bec_count1_inline(body, population, query32);
}

extern "C" std::uint64_t ikea_heterogeneous_bec_count2(
    const std::uint8_t* a, unsigned pop_a,
    const std::uint8_t* b, unsigned pop_b,
    const std::uint8_t* query64) noexcept {
    return bec_count2_inline(a, pop_a, b, pop_b, query64);
}
}
