#pragma once
#include <ikea/frame/author/stage.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ikea::frame::detail {

/// A bind-time plan for independent points over a bound -> pack unit: one
/// flat entry per block, so a point is one entry load, one leaf call and a
/// field-first exception check rather than a virtual call per stage.
///
/// The plan copies lo, bases and exceptions but borrows the leaf bodies: the
/// unit's images stay placed, immutable and alive while the plan is used, and
/// a new image needs a new plan.
class point_plan {
    struct entry {
        std::uint64_t lo;
        std::uint32_t base; // the block's first row in its leaf array
        std::uint32_t exc;  // the block's first exception in pos_/val_
        std::uint16_t leaf; // index into leaves_
        std::uint8_t count;
    };
    std::vector<entry> entries_;
    std::vector<author::point_leaf> leaves_;
    std::vector<std::uint8_t> pos_;
    std::vector<std::uint64_t> val_;

  public:
    /// Entries narrow leaf indexes to u16 (leaf 0 is the constant block) and
    /// exception offsets to u32; a unit past either limit is refused.
    static bool admits(std::size_t leaves, std::size_t exceptions) noexcept {
        return leaves <= std::size_t{1} << 16 && exceptions <= ~std::uint32_t{0};
    }
    /// The plan for one unit of a bound -> pack root, or nullopt for any
    /// other shape (whose points use the stage tree).
    static std::optional<point_plan> make(const author::stage &root, unsigned unit);

    std::size_t bytes() const noexcept { return entries_.size() * sizeof(entry) + pos_.size() + val_.size() * 8; }
    const void *entry_address(std::size_t row) const noexcept { return &entries_[row / block_rows]; }
    void prefetch_entry(std::size_t row) const noexcept { __builtin_prefetch(entry_address(row)); }
    // With the entry cached: the row's leaf tile and the block's exceptions.
    void prefetch_body(std::size_t row) const noexcept {
        const entry &e = entries_[row / block_rows];
        const author::point_leaf &k = leaves_[e.leaf];
        if (k.data)
            __builtin_prefetch(k.data + (e.base + row % block_rows) / k.tile_rows * k.tile_bytes);
        if (e.count)
            __builtin_prefetch(pos_.data() + e.exc);
    }
    std::uint64_t get(std::size_t row) const noexcept {
        const entry &e = entries_[row / block_rows];
        const unsigned r = row % block_rows;
        const author::point_leaf &k = leaves_[e.leaf];
        const std::uint64_t f = k.get ? k.get(k.resource, e.base + r) : 0;
        if (f == 0)
            for (unsigned i = 0; i < e.count; ++i)
                if (pos_[e.exc + i] == r)
                    return val_[e.exc + i];
        return e.lo + f;
    }
    /// Independent points through a software pipeline: entries 2D ahead,
    /// bodies D ahead of the compute.
    template <std::size_t D = 16> void get_many(const std::uint32_t *rows, std::size_t n, std::uint64_t *out) const noexcept {
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
};

} // namespace ikea::frame::detail
