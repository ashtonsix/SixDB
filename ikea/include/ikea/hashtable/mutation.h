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
enum class plane {
    header,
    primary,
    swiss_controls,
    swiss_tuples
};

inline bool input_overlaps(std::span<const std::byte> input, const void* base,
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
//   prepare(span<const ikea::journal::entry>): validate source generations and preflight
//       all preservation/journal capacity; false means no effects or stores.
//       Resolve each entry.source to the actual named table and use
//       table.plane_storage(plane(entry.plane)) for its current byte extent.
//   before(owner, plane, offset, size) noexcept: publish qualified damage before stores.
// No callback may reenter or invalidate the owner or remaining input. A successful enter retains
// its permissions until the whole synchronous operation has returned. Distinct
// key grants alone do not protect shared packed bytes. Growth must be prepared
// separately and followed by fresh source admission.

} // namespace ikea::hashtable
