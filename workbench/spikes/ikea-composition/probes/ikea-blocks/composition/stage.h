#pragma once

#include "native_features.h"
#include <cstddef>
#include <string>
#include <vector>

namespace ikea::composition {

// A deliberately small flattened carrier family. Both operands are native on
// ARM; wrapping bits plus scalar fields in a returned aggregate is avoided.
#if defined(__x86_64__)
#define IKEA_COMP_BITS_PARAMS __m256i bits
#define IKEA_COMP_BITS_FORWARD bits
#define IKEA_COMP_BITS_VALUE bits
#define IKEA_COMP_BITS_UNPACK(value) value
#else
#define IKEA_COMP_BITS_PARAMS uint8x16_t bits_lo, uint8x16_t bits_hi
#define IKEA_COMP_BITS_FORWARD bits_lo, bits_hi
#define IKEA_COMP_BITS_VALUE Native256{bits_lo,bits_hi}
#define IKEA_COMP_BITS_UNPACK(value) value.lo, value.hi
#endif

struct Instruction;
using Stage = std::uint64_t (*)(const Instruction* cursor, const std::uint8_t* tile,
                              std::uint64_t accumulator, std::uint64_t fields,
                              IKEA_COMP_BITS_PARAMS);
struct Instruction { Stage execute; };
struct Program {
    std::vector<Instruction> instructions;
    std::vector<std::string> operation_ids; // inspection only; not in hot cursor
};

#define IKEA_COMP_STAGE(name) \
    extern "C" __attribute__((noinline)) std::uint64_t name( \
        const Instruction* cursor, const std::uint8_t* tile, std::uint64_t accumulator, \
        std::uint64_t fields, IKEA_COMP_BITS_PARAMS)

IKEA_COMP_STAGE(ikea_comp_load);
IKEA_COMP_STAGE(ikea_comp_features);
IKEA_COMP_STAGE(ikea_comp_transition_features_stage);
IKEA_COMP_STAGE(ikea_comp_load_features);
IKEA_COMP_STAGE(ikea_comp_load_transition_features_stage);
IKEA_COMP_STAGE(ikea_comp_model);
IKEA_COMP_STAGE(ikea_comp_transition_model_stage);
IKEA_COMP_STAGE(ikea_comp_done);

} // namespace ikea::composition
