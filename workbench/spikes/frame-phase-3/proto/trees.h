// Named stage trees for the checks and timings. A real description would
// carry these as a headless schema; here they are constructors.
#pragma once
#include "placeholders.h"

#include <functional>
#include <optional>
#include <string>

namespace frame3 {
struct tree {
    std::string name;
    std::function<std::unique_ptr<stage>()> make;
};

template <sp::preset P> inline std::unique_ptr<stage> bound_pack(unsigned penalty = 0) {
    return std::make_unique<bound>(std::make_unique<pack<P>>(), penalty);
}
// Deltas through bound -> pack, checkpoints through bound -> pack of their own.
template <sp::preset P> inline std::unique_ptr<stage> delta_bound_pack(unsigned G, unsigned penalty = 0) {
    return std::make_unique<intervals>(bound_pack<P>(penalty), bound_pack<P>(), G);
}

inline std::vector<tree> trees() {
    return {
        {"pack", [] { return std::make_unique<pack<sp::preset::compact>>(); }},
        {"bound_pack_compact", [] { return bound_pack<sp::preset::compact>(); }},
        {"bound_pack_bulk_x86", [] { return bound_pack<sp::preset::bulk_x86>(); }},
        {"bound_pack_bulk_arm", [] { return bound_pack<sp::preset::bulk_arm>(); }},
        {"bound_pack_compact_penalty4", [] { return bound_pack<sp::preset::compact>(4); }},
        {"intervals16_bound_pack_compact", [] { return delta_bound_pack<sp::preset::compact>(16); }},
        {"intervals64_bound_pack_compact", [] { return delta_bound_pack<sp::preset::compact>(64); }},
        {"intervals256_bound_pack_compact", [] { return delta_bound_pack<sp::preset::compact>(256); }},
    };
}
/// A hand-fused point path for the bound -> pack shape: one flat entry per
/// block built at bind time, so a point is one entry load, one leaf call and
/// a field-first exception check, instead of a virtual call per stage.
class point_plan {
    std::vector<bound::point_entry> entries_;

  public:
    static std::optional<point_plan> make(const column &c) {
        const auto *root = dynamic_cast<const bound *>(&c.root());
        if (!root)
            return std::nullopt;
        point_plan plan;
        for (unsigned p = 0; p < c.parts(); ++p)
            for (unsigned b = 0; b < c.root().blocks_in(p); ++b)
                if (!root->entry(p, b, plan.entries_.emplace_back()))
                    return std::nullopt;
        return plan;
    }
    // Staging hooks: the entry, then (with the entry cached) the row's leaf
    // tile and the block's exception positions.
    const void *entry_address(std::size_t row) const { return &entries_[row / block_rows]; }
    void prefetch_entry(std::size_t row) const { __builtin_prefetch(entry_address(row)); }
    void prefetch_body(std::size_t row) const {
        const auto &e = entries_[row / block_rows];
        if (e.kernel.data)
            __builtin_prefetch(e.kernel.data + (e.base + row % block_rows) / e.kernel.tile_rows * e.kernel.tile_bytes);
        if (e.count)
            __builtin_prefetch(e.pos);
    }
    // A software pipeline over independent points: the entry D ahead of the
    // body, the body D ahead of the compute, so 2D points are in flight.
    template <std::size_t D> void get_many(const u32 *rows, std::size_t n, u64 *out) const {
        for (std::size_t i = 0; i < std::min(n, 2 * D); ++i)
            prefetch_entry(rows[i]);
        for (std::size_t i = 0; i < std::min(n, D); ++i)
            prefetch_body(rows[i]);
        for (std::size_t i = 0; i < n; ++i) {
            if (i + 2 * D < n)
                prefetch_entry(rows[i + 2 * D]);
            if (i + D < n)
                prefetch_body(rows[i + D]);
            out[i] = get(rows[i]);
        }
    }
    u64 get(std::size_t row) const {
        const auto &e = entries_[row / block_rows];
        const unsigned r = row % block_rows;
        const u64 f = e.kernel.get ? e.kernel.get(e.kernel.data, e.base + r) : 0;
        if (f == 0)
            for (unsigned i = 0; i < e.count; ++i)
                if (e.pos[i] == r)
                    return e.val[i];
        return e.lo + f;
    }
};

// Placeholder shapes (prototype 2), checked but not timed.
inline std::vector<tree> placeholder_trees() {
    return {
        {"runs_bound_pack", [] { return std::make_unique<runs>(bound_pack<sp::preset::compact>(), bound_pack<sp::preset::compact>()); }},
        {"split8_bound_pack", [] { return std::make_unique<split>(bound_pack<sp::preset::compact>(), bound_pack<sp::preset::compact>(), 8); }},
        {"bound_split8_pack", [] {
             return std::make_unique<bound>(std::make_unique<split>(std::make_unique<pack<sp::preset::compact>>(), std::make_unique<pack<sp::preset::compact>>(), 8));
         }},
    };
}
}  // namespace frame3
