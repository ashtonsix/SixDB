#include "packet_cases.h"

void register_tuple_packets_medium() {
#if defined(__aarch64__) || defined(__AVX2__)
    const bool broad = std::getenv("TUPLEPACK_BROAD") != nullptr;
    cases<8>(broad);
    cases<16>(broad);
#endif
}
