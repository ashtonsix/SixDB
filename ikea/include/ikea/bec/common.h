#pragma once
// Vocabulary shared by every Bec variant: failures, diagnostics and the
// caller-owned destination plane. Variants re-export these names.
#include <ikea/bytes.h>
#include <ikea/effects.h>
#include <span>
#include <string_view>

namespace ikea::bec {
using ikea::byte;

enum class error {
    population, // outside the variant's 0..positions, or unlike the input
    truncated,
    split,
    rank,
    padding,
    trailing,
    capacity,
    effects,
    overlap,
    length // a block length outside its range or unlike its stream (Bec4096)
};
std::string_view describe(error) noexcept;

/// Caller-owned destination plane. Effects use this named object's identity and
/// byte offsets relative to storage. Keep it alive until effects are resolved.
struct view {
    std::span<byte> storage;
    std::span<const byte> plane_storage(unsigned) const noexcept { return storage; }
};
} // namespace ikea::bec
