#include "packet_cases.h"

void register_tuple_packets_wide() {
#if defined(__aarch64__) || defined(__AVX2__)
    const bool broad = std::getenv("TUPLEPACK_BROAD") != nullptr;
    cases<1>(broad);
    cases<2>(broad);
    cases<4>(broad);
#endif
}
