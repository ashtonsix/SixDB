// Independently owned logical groups (PROGRAMS.md, first architectural
// witness). A column is cut into groups of G blocks; each group is its own
// immutable image (descriptors, bodies grouped by kind inside the group, its
// own kind index and readable suffix) behind a shared pointer. A view is a
// vector of group pointers, so folding one group allocates only that group's
// backing, an old view keeps its groups alive, and untouched groups are
// shared rather than copied. G is a forcing parameter, not a settled grain.
#pragma once

#include "image.h"

#include <memory>
#include <span>
#include <vector>

namespace frame_image {

struct group_image {
    image f;
    kind_index index;
    explicit group_image(std::span<const u64> rows) : f(build(rows, placement::by_kind)), index(index_by_kind(f)) {}
    // Everything this group owns: descriptor columns, bodies, suffix, index.
    std::size_t bytes() const { return f.image_bytes() + (f.bytes.size() - f.body_bytes) + 4 * index.ids.size(); }
};

struct grouped_view {
    std::size_t group_blocks = 16;
    std::vector<std::shared_ptr<const group_image>> groups;
    std::size_t rows() const {
        std::size_t n = 0;
        for (const auto &g : groups)
            n += g->f.blocks * block_rows;
        return n;
    }
};

inline grouped_view build_grouped(std::span<const u64> v, std::size_t group_blocks) {
    grouped_view x;
    x.group_blocks = group_blocks;
    const std::size_t step = group_blocks * block_rows;
    for (std::size_t first = 0; first < v.size(); first += step)
        x.groups.push_back(std::make_shared<const group_image>(v.subspan(first, std::min(step, v.size() - first))));
    return x;
}

inline std::size_t bytes(const grouped_view &x) {
    std::size_t n = 8 * x.groups.size();  // the view's group pointers
    for (const auto &g : x.groups)
        n += g->bytes();
    return n;
}

inline u128 sum(const grouped_view &x) {
    u128 s = 0;
    for (const auto &g : x.groups)
        s += sum_indexed(g->f, g->index);
    return s;
}

inline u64 count_ge(const grouped_view &x, u64 t) {
    u64 n = 0;
    for (const auto &g : x.groups)
        n += count_ge(g->f, t, 0, g->f.blocks);
    return n;
}

inline u64 point(const grouped_view &x, std::size_t row) {
    const std::size_t step = x.group_blocks * block_rows;
    return point_default(x.groups[row / step]->f, row % step);
}

// Fold group g from its current logical rows: a new group image replaces
// the pointer in a copy of the view. Returns the new view; the old view (and
// any reader holding it) keeps the old group alive until released.
inline grouped_view fold_group(const grouped_view &x, std::size_t g, std::span<const u64> group_rows) {
    grouped_view y = x;
    y.groups[g] = std::make_shared<const group_image>(group_rows);
    return y;
}
}  // namespace frame_image
