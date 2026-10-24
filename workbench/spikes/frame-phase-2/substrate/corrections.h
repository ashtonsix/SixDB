// Bounded corrections over an immutable image, for front A's edit-policy
// comparison (corrections vs fitting repair vs relocation vs native groups).
// The base image never changes; each write records the row's new logical
// value in its block's correction list. A per-block dirty bit lets pristine
// blocks keep the unchanged image path, and each dirty block keeps the exact
// SUM delta of its corrections so full SUM needs no list walk. Every read
// returns the corrected logical value. Folding is front A's to price; this
// header supplies the read and write mechanics only.
#pragma once

#include "image.h"

#include <algorithm>
#include <vector>

namespace frame_image {

struct corrected {
    const image *base = nullptr;
    // Per block: sorted (row in block, new value) pairs; empty for pristine.
    std::vector<std::vector<std::pair<u8, u64>>> fix;
    std::vector<u64> dirty;           // one bit per block
    std::vector<__int128> sum_delta;  // per block: sum of (new - old) over its corrections
    std::size_t entries = 0;

    explicit corrected(const image &f)
        : base(&f), fix(f.blocks), dirty((f.blocks + 63) / 64, 0), sum_delta(f.blocks, 0) {}

    bool is_dirty(std::size_t b) const { return dirty[b / 64] >> (b % 64) & 1; }

    u64 point(std::size_t row) const {
        const std::size_t b = row / block_rows;
        if (is_dirty(b)) {
            const auto &l = fix[b];
            const u8 i = static_cast<u8>(row % block_rows);
            const auto at = std::lower_bound(l.begin(), l.end(), i, [](const auto &e, u8 r) { return e.first < r; });
            if (at != l.end() && at->first == i)
                return at->second;
        }
        return point_with<false>(*base, row);
    }

    void write(std::size_t row, u64 value) {
        const std::size_t b = row / block_rows;
        const u8 i = static_cast<u8>(row % block_rows);
        const u64 old = point(row);
        auto &l = fix[b];
        const auto at = std::lower_bound(l.begin(), l.end(), i, [](const auto &e, u8 r) { return e.first < r; });
        if (at != l.end() && at->first == i)
            at->second = value;
        else
            l.insert(at, {i, value}), ++entries;
        sum_delta[b] += static_cast<__int128>(value) - static_cast<__int128>(old);
        dirty[b / 64] |= u64{1} << (b % 64);
    }

    std::size_t bytes() const { return entries * 9 + 8 * dirty.size() + 16 * fix.size(); }

    // Full SUM: the base image plus each dirty block's delta.
    u128 sum(const kind_index &index) const {
        __int128 delta = 0;
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1)
                delta += sum_delta[w * 64 + static_cast<std::size_t>(std::countr_zero(x))];
        return static_cast<u128>(static_cast<__int128>(sum_indexed(*base, index)) + delta);
    }

    // COUNT(x >= t): the base count, then each corrected row's old and new
    // values reconcile its contribution.
    u64 count_ge(u64 t) const {
        u64 n = frame_image::count_ge(*base, t, 0, base->blocks);
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1) {
                const std::size_t b = w * 64 + static_cast<std::size_t>(std::countr_zero(x));
                for (const auto &[i, v] : fix[b]) {
                    const u64 old = point_with<false>(*base, b * block_rows + i);
                    n += (v >= t) - (old >= t);
                }
            }
        return n;
    }

    // Row masks of x >= t: base masks, then corrected rows overwritten.
    u64 mask_ge(u64 t, u64 *masks) const {
        u64 n = frame_image::mask_ge(*base, t, 0, base->blocks, masks);
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1) {
                const std::size_t b = w * 64 + static_cast<std::size_t>(std::countr_zero(x));
                for (const auto &[i, v] : fix[b]) {
                    u64 &word = masks[4 * b + i / 64];
                    const u64 bit = u64{1} << (i % 64);
                    const bool was = word & bit, now = v >= t;
                    word = now ? word | bit : word & ~bit;
                    n += now - was;
                }
            }
        return n;
    }

    // SUM over selected rows: base masked SUM, then corrected selected rows.
    u128 masked_sum(const u64 *masks) const {
        __int128 s = static_cast<__int128>(frame_image::masked_sum(*base, masks));
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1) {
                const std::size_t b = w * 64 + static_cast<std::size_t>(std::countr_zero(x));
                for (const auto &[i, v] : fix[b])
                    if (masks[4 * b + i / 64] >> (i % 64) & 1)
                        s += static_cast<__int128>(v) - static_cast<__int128>(point_with<false>(*base, b * block_rows + i));
            }
        return static_cast<u128>(s);
    }
};

// The same corrections over any stored program that answers point, sum,
// count_ge, mask_ge and masked_sum for its whole column (for example a
// dictionary-coded column, whose novel values live only in the corrections:
// base translations and bounds describe the base, never the combined image).
template <class Base> struct corrected_over {
    const Base *base = nullptr;
    std::size_t blocks = 0;
    std::vector<std::vector<std::pair<u8, u64>>> fix;
    std::vector<u64> dirty;
    std::vector<__int128> sum_delta;
    std::size_t entries = 0;

    corrected_over(const Base &b, std::size_t nblocks)
        : base(&b), blocks(nblocks), fix(nblocks), dirty((nblocks + 63) / 64, 0), sum_delta(nblocks, 0) {}
    bool is_dirty(std::size_t b) const { return dirty[b / 64] >> (b % 64) & 1; }
    template <class F> void each_correction(F &&f) const {
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1) {
                const std::size_t b = w * 64 + static_cast<std::size_t>(std::countr_zero(x));
                for (const auto &[i, v] : fix[b])
                    f(b, i, v);
            }
    }
    u64 point(std::size_t row) const {
        const std::size_t b = row / block_rows;
        if (is_dirty(b)) {
            const auto &l = fix[b];
            const u8 i = static_cast<u8>(row % block_rows);
            const auto at = std::lower_bound(l.begin(), l.end(), i, [](const auto &e, u8 r) { return e.first < r; });
            if (at != l.end() && at->first == i)
                return at->second;
        }
        return base->point(row);
    }
    void write(std::size_t row, u64 value) {
        const std::size_t b = row / block_rows;
        const u8 i = static_cast<u8>(row % block_rows);
        const u64 old = point(row);
        auto &l = fix[b];
        const auto at = std::lower_bound(l.begin(), l.end(), i, [](const auto &e, u8 r) { return e.first < r; });
        if (at != l.end() && at->first == i)
            at->second = value;
        else
            l.insert(at, {i, value}), ++entries;
        sum_delta[b] += static_cast<__int128>(value) - static_cast<__int128>(old);
        dirty[b / 64] |= u64{1} << (b % 64);
    }
    u128 sum() const {
        __int128 d = 0;
        for (std::size_t w = 0; w < dirty.size(); ++w)
            for (u64 x = dirty[w]; x; x &= x - 1)
                d += sum_delta[w * 64 + static_cast<std::size_t>(std::countr_zero(x))];
        return static_cast<u128>(static_cast<__int128>(base->sum()) + d);
    }
    u64 mask_ge(u64 t, u64 *masks) const {
        u64 n = base->mask_ge(t, masks);
        each_correction([&](std::size_t b, u8 i, u64 v) {
            u64 &word = masks[4 * b + i / 64];
            const u64 bit = u64{1} << (i % 64);
            const bool was = word & bit, now = v >= t;
            word = now ? word | bit : word & ~bit;
            n += now - was;
        });
        return n;
    }
    u64 count_ge(u64 t) const {
        u64 n = base->count_ge(t);
        each_correction([&](std::size_t b, u8 i, u64 v) { n += (v >= t) - (base->point(b * block_rows + i) >= t); });
        return n;
    }
    u128 masked_sum(const u64 *masks) const {
        __int128 s = static_cast<__int128>(base->masked_sum(masks));
        each_correction([&](std::size_t b, u8 i, u64 v) {
            if (masks[4 * b + i / 64] >> (i % 64) & 1)
                s += static_cast<__int128>(v) - static_cast<__int128>(base->point(b * block_rows + i));
        });
        return static_cast<u128>(s);
    }
};
}  // namespace frame_image
