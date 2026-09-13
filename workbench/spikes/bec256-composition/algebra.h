#pragma once
#include "consumer.h"

namespace bec_study {
enum class boolean_op { intersection, set_union };
enum class output_kind { complete, selected_only };
struct operand_view {
    const bitset &bits;
    const directory *metadata; // Null means plain storage; no population evidence.
    resolution lookup = resolution::native16;
};

// Experimental whole operation, not a proposed Ikea API. Both inputs have the
// same original coordinate domain and have already passed bitset::admit when
// compressed. Each body has an initialized 64-byte read window. Output is plain,
// disjoint from both inputs, directories, selection and effect storage; it holds
// at least bits.size()*32 bytes. Journal has room for one complete-output event
// or bits.size() selected-only events. Inputs have at least one block.
// No allocation, rejection or suspension after these owner proofs. Mask null
// selects every original ordinal. Selected-only preserves inactive output bytes;
// complete establishes zero there. Effects report every issued output span.
using algebra_call = void (*)(operand_view, operand_view, const std::uint64_t *, bc::destination &,
                              ikea::source_write_journal &);
algebra_call bind_algebra(boolean_op, output_kind, unsigned output_grain = 2);
} // namespace bec_study
