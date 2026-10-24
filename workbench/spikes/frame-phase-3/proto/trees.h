// Named stage trees for the checks and timings. A real description would
// carry these as a headless schema; here they are constructors.
#pragma once
#include "placeholders.h"

#include <algorithm>
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
template <sp::preset P> inline std::unique_ptr<stage> delta_bound_pack(unsigned G, unsigned penalty = 0, interval_form form = interval_form::sizes) {
    return std::make_unique<intervals>(bound_pack<P>(penalty), bound_pack<P>(), G, form);
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
        {"relative16_bound_pack_compact", [] { return delta_bound_pack<sp::preset::compact>(16, 0, interval_form::relative_ends); }},
        {"relative64_bound_pack_compact", [] { return delta_bound_pack<sp::preset::compact>(64, 0, interval_form::relative_ends); }},
    };
}
/// A hand-fused point path for the bound -> pack shape: one flat entry per
/// block built at bind time, so a point is one entry load, one leaf call and
/// a field-first exception check, instead of a virtual call per stage.
/// The plan copies lo, bases and exceptions but borrows the leaf bodies: the
/// column's images must stay unchanged while it is used, and any mutation or
/// rebuild needs a new plan.
class point_plan {
    // One 24-byte entry per block (64 bytes before), so more of the
    // descriptors stay resident: each leaf kernel is stored once per width
    // array, and the exceptions are copied into the plan's own columns.
    struct entry {
        u64 lo;
        u32 base;  // the block's first row in its leaf array
        u32 exc;   // the block's first exception in pos_/val_
        u16 leaf;  // index into leaves_
        u8 count;
    };
    std::vector<entry> entries_;
    std::vector<point_kernel> leaves_;
    std::vector<u8> pos_;
    std::vector<u64> val_;

  public:
    // Entries narrow leaf indexes to u16 and exception offsets to u32; a
    // column past either limit is refused rather than aliased.
    static bool admits(std::size_t leaves, std::size_t exceptions) noexcept {
        return leaves <= std::size_t{1} << 16 && exceptions <= ~u32{0};
    }
    static std::optional<point_plan> make(const column &c) {
        const auto *root = dynamic_cast<const bound *>(&c.root());
        if (!root)
            return std::nullopt;
        point_plan plan;
        plan.leaves_.push_back({});  // leaf 0: width 0, no body
        std::vector<std::pair<const void *, u16>> seen;
        for (unsigned p = 0; p < c.parts(); ++p)
            for (unsigned b = 0; b < c.root().blocks_in(p); ++b) {
                bound::point_entry e;
                if (!root->entry(p, b, e) || e.count > 255 || e.base > ~u32{0})
                    return std::nullopt;
                u16 leaf = 0;
                if (e.kernel.get) {
                    auto it = std::find_if(seen.begin(), seen.end(), [&](auto &x) { return x.first == e.kernel.resource; });
                    if (it == seen.end()) {
                        if (!admits(plan.leaves_.size() + 1, plan.pos_.size()))
                            return std::nullopt;
                        seen.emplace_back(e.kernel.resource, static_cast<u16>(plan.leaves_.size()));
                        plan.leaves_.push_back(e.kernel);
                        it = seen.end() - 1;
                    }
                    leaf = it->second;
                }
                if (!admits(plan.leaves_.size(), plan.pos_.size() + e.count))
                    return std::nullopt;
                plan.entries_.push_back({e.lo, static_cast<u32>(e.base), static_cast<u32>(plan.pos_.size()), leaf, static_cast<u8>(e.count)});
                plan.pos_.insert(plan.pos_.end(), e.pos, e.pos + e.count);
                plan.val_.insert(plan.val_.end(), e.val, e.val + e.count);
            }
        return plan;
    }
    std::size_t bytes() const { return entries_.size() * sizeof(entry) + pos_.size() + val_.size() * 8; }
    // Staging hooks: the entry, then (with the entry cached) the row's leaf
    // tile and the block's exception positions.
    const void *entry_address(std::size_t row) const { return &entries_[row / block_rows]; }
    void prefetch_entry(std::size_t row) const { __builtin_prefetch(entry_address(row)); }
    void prefetch_body(std::size_t row) const {
        const auto &e = entries_[row / block_rows];
        const auto &k = leaves_[e.leaf];
        if (k.data)
            __builtin_prefetch(k.data + (e.base + row % block_rows) / k.tile_rows * k.tile_bytes);
        if (e.count)
            __builtin_prefetch(pos_.data() + e.exc);
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
        const auto &k = leaves_[e.leaf];
        const u64 f = k.get ? k.get(k.resource, e.base + r) : 0;
        if (f == 0)
            for (unsigned i = 0; i < e.count; ++i)
                if (pos_[e.exc + i] == r)
                    return val_[e.exc + i];
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
