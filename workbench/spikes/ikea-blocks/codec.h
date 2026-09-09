#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace ikea_probe {
// Explicit materializing endpoints for callers whose results belong in memory.
// Native inline bodies have separate signatures in native_{neon,avx512}.h.
// Trusted decode: body/cardinality association validated for the current bytes;
// input has 64 readable bytes, output 32 writable; ranges do not overlap.
// Trusted encode: input has 32 readable bytes, output 64 writable; disjoint.
// No headers. Canonical final pad bits are zero. Empty/full use zero body bits.
unsigned encode_native(const std::uint8_t* plain, unsigned cardinality,
                       std::uint8_t* writable64);
unsigned decode_native(const std::uint8_t* readable64, unsigned cardinality,
                       std::uint8_t* plain32);
void decode_native2(const std::uint8_t* a, unsigned pop_a,
                    const std::uint8_t* b, unsigned pop_b, std::uint8_t* plain64);

// Cold admission/oracle. These do not call the trusted decoder on hostile bytes.
enum class Invalid : unsigned { none, cardinality, truncated, split, rank, padding, trailing };
struct Validation { Invalid error; unsigned bits; };
Validation validate(std::span<const std::uint8_t>, unsigned cardinality);
unsigned encode_reference(const std::uint8_t*, std::uint8_t*);
unsigned exact_bits_reference(const std::uint8_t*);
}
