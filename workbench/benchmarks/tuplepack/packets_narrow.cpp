#include "packet_cases.h"

void register_tuple_packets_narrow() {
#if defined(__aarch64__) || defined(__AVX2__)
    const bool broad = std::getenv("TUPLEPACK_BROAD") != nullptr;
    cases<32>(broad);
    cases<64>(broad);
#endif
}
