#pragma once
#include "formats.h"
#include <array>
#include <cstdint>

namespace ikea::integers {
// Trusted materialising endpoints used by the comparison harness. Exact
// 32*K packed bytes and 256 byte values; buffers disjoint, values width-valid.
// Point and group reads use indices within 256; group index is divisible by 16.
// No allocation, metadata lookup, admission or validation in these functions.
struct Codec {
    uint8_t (*get1)(const uint8_t*,unsigned);
    void (*get16)(const uint8_t*,unsigned,uint8_t*);
    void (*decode)(const uint8_t*,uint8_t*);
    void (*encode)(const uint8_t*,uint8_t*);
};
const std::array<Codec,7>& codecs(Layout);
const std::array<Codec,7>& prior_codecs(Layout);
void oracle_encode(Layout,unsigned,const uint8_t*,uint8_t*);
uint8_t oracle_get(Layout,unsigned,const uint8_t*,unsigned);
} // namespace ikea::integers
