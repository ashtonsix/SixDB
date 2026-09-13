#pragma once
#include "metadata.h"
#include <ikea/bec256/author/chain.h>

namespace bec_study {
enum class execution { inline_body, shared_body, materialized, cps, cps_fused };
const char *name(execution);
struct bitset {
    std::vector<bc::byte> plain, body;
    std::vector<entry> catalog;
    explicit bitset(std::span<const bc::byte> input);
    unsigned size() const { return catalog.size(); }
    void admit(const directory &) const;
};
struct request {
    unsigned first, count, every = 1;
    // Optional one-bit-per-original-Bec256 prefilter, never rebased to first.
    // Caller supplies ceil(bitset.size()/64) words; null selects every ordinal
    // in the strided range. Inactive bodies/query blocks must not be accessed.
    const std::uint64_t *active = nullptr;
};
using counter = std::uint64_t (*)(const directory &, const bitset &, const bc::byte *, request);
counter bind_count(layout, resolution, execution);
std::uint64_t count_reference(const bitset &, const bc::byte *query, request);
// A shared compiled region owns decode + Boolean + reduction. Metadata layout
// does not replicate it. The caller supplies two independently resolved bodies.
std::uint64_t consume_pair(const bc::byte *, unsigned, const bc::byte *, unsigned, const bc::byte *,
                           const bc::byte *) noexcept;
} // namespace bec_study
