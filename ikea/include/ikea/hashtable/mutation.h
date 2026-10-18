#pragma once
#include <ikea/effects.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>

namespace ikea::hashtable {
enum class access_intent { read, write };
enum class mutation_status { inserted, updated, unchanged, erased, absent, denied, needs_capacity };
enum class lookup_status { found, absent, denied };
enum class owner_part {
    header,
    primary,
    swiss_controls,
    swiss_tuples,
    singleton_bags,
    small_variable_bags,
    large_variable_bags
};

// The caller resolves this live source to its object incarnation, physical part
// and generation. Pointer/offset alone is not a persistent damage identity.
struct mutation_source {
    const void* owner;
    owner_part part;
    const std::uint8_t* base;
    std::size_t extent;
};
struct mutation_damage {
    mutation_source source;
    ikea::byte_write span;
};

inline bool input_overlaps(std::span<const std::uint8_t> input, const void* base,
                           std::size_t extent) {
    if (input.empty() || !extent)
        return false;
    const auto src = reinterpret_cast<std::uintptr_t>(input.data()),
               dst = reinterpret_cast<std::uintptr_t>(base);
    return src <= dst ? dst - src < input.size() : src - dst < extent;
}

// A context is supplied by the caller, not a key-lock implementation:
//   enter(owner, canonical_key, intent): validate the logical grant and retain
//       stable read storage / private or exclusively writable physical storage.
//   prepare(span<mutation_damage>): validate source generations and preflight
//       all preservation/journal capacity; false means no effects or stores.
//   before(source, byte_write) noexcept: publish qualified damage before stores.
// No callback may reenter or invalidate the owner or remaining input. A successful enter retains
// its permissions until the whole synchronous operation has returned. Distinct
// key grants alone do not protect shared packed bytes. Growth must be prepared
// separately and followed by fresh source admission.

} // namespace ikea::hashtable
