// Sorted part dictionaries over the shared image: each 65,536-row part maps
// values to ranks in its sorted distinct values and stores the codes as an
// ordinary image (grouped by kind). Selection translates thresholds to codes;
// SUM uses front C's per-code weight leaves when bodies/weighted.h is present.
#pragma once

#include "image.h"
#include "sort.h"
#if __has_include("weighted.h")
#include "weighted.h"
#define FRAME_WEIGHTED_LEAVES 1
#endif

#include <algorithm>
#include <cstring>
#include <vector>

namespace frame_dict {
namespace fi = frame_image;
using fi::u128;
using fi::u32;
using fi::u64;
using fi::u8;
inline constexpr std::size_t part_rows = 65536, part_blocks = part_rows / fi::block_rows;


template <class T> T run_value(bodies::runs<T>);

struct dict_column {
    std::vector<std::vector<u64>> dict;
    fi::image codes;
    explicit dict_column(const std::vector<u64> &v) {
        std::vector<u64> c(v.size());
        for (std::size_t first = 0; first < v.size(); first += part_rows) {
            const std::size_t limit = std::min(v.size(), first + part_rows);
            std::vector<u64> d(v.begin() + first, v.begin() + limit);
            std::sort(d.begin(), d.end());
            d.erase(std::unique(d.begin(), d.end()), d.end());
            for (std::size_t i = first; i < limit; ++i)
                c[i] = std::lower_bound(d.begin(), d.end(), v[i]) - d.begin();
            dict.push_back(std::move(d));
        }
        codes = fi::build(c, fi::placement::by_kind);
    }
    std::size_t bytes() const {
        std::size_t n = codes.image_bytes();
        for (const auto &d : dict)
            n += 8 * d.size();
        return n;
    }
    u64 code_threshold(std::size_t part, u64 t) const {
        return std::lower_bound(dict[part].begin(), dict[part].end(), t) - dict[part].begin();
    }
    // Codes of one block, unpacked, for SUM and masked SUM (the per-code weight
    // leaf is front C's pending work).
    void unpack(std::size_t b, u32 *x) const {
        const u8 *p = codes.bytes.data() + codes.offset[b];
        const unsigned w = codes.width[b];
        switch (codes.kind_of[b]) {
        case fi::constant: std::fill(x, x + fi::block_rows, 0u); break;
        case fi::runs: fi::with_runs(w, [&](auto r) { decltype(r)::unpack(p, codes.n[b], x); }); break;
        case fi::plane: fi::with_plane(w, [&](auto K) { fi::P<K>::unpack(p, x); }); break;
        default:
            for (unsigned i = 0; i < fi::block_rows; ++i) {
                u64 y;
                std::memcpy(&y, p + 8 * i, 8);
                x[i] = static_cast<u32>(y);
            }
        }
    }
    u128 block_sum(std::size_t b, const u64 *sel) const {
        const u64 *d = dict[b / part_blocks].data() + codes.base[b];
#if FRAME_WEIGHTED_LEAVES
        // Front C's per-code weight leaves (bodies/weighted.h): D indexed by
        // the block's code residuals, so the table pointer starts at its base.
        {
            const u8 *p = codes.bytes.data() + codes.offset[b];
            const unsigned w = codes.width[b];
            switch (codes.kind_of[b]) {
            case fi::constant: {
                const unsigned cnt = sel ? std::popcount(sel[0]) + std::popcount(sel[1]) + std::popcount(sel[2]) + std::popcount(sel[3]) : fi::block_rows;
                return static_cast<u128>(d[0]) * cnt;
            }
            case fi::runs: {
                u128 r = 0;
                fi::with_runs(w, [&](auto rr) {
                    using W = bodies::runs_weighted<decltype(run_value(rr))>;
                    r = sel ? W::masked_weighted_sum(p, codes.n[b], sel, d) : W::weighted_sum(p, codes.n[b], d);
                });
                return r;
            }
            case fi::plane:
                if (w <= 16) {
                    u128 r = 0;
                    fi::with_plane(w, [&](auto K) {
                        if constexpr (K <= 16)
                            r = sel ? bodies::packed_weighted<bodies::layout::plane256, K>::masked_weighted_sum(p, sel, d)
                                    : bodies::packed_weighted<bodies::layout::plane256, K>::weighted_sum(p, d);
                    });
                    return r;
                }
                break;
            default: break;
            }
        }
#endif
        alignas(64) u32 x[fi::block_rows + 32];  // run unpack writes up to 32 rows past 256 (bodies/README.md, write envelopes)
        unpack(b, x);
        u64 lo = 0, hi = 0;
        for (unsigned i = 0; i < fi::block_rows; ++i) {
            const u64 y = sel && !(sel[i / 64] >> (i % 64) & 1) ? 0 : d[x[i]];
            lo += y & 0xffffffffu, hi += y >> 32;
        }
        return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
    }
    u128 sum() const {
        u128 s = 0;
        for (std::size_t b = 0; b < codes.blocks; ++b)
            s += block_sum(b, nullptr);
        return s;
    }
    u64 count_ge(u64 t) const {
        u64 n = 0;
        for (std::size_t first = 0, part = 0; first < codes.blocks; first += part_blocks, ++part)
            n += fi::count_ge(codes, code_threshold(part, t), first, std::min(codes.blocks, first + part_blocks));
        return n;
    }
    u64 mask_ge(u64 t, u64 *masks) const {
        u64 n = 0;
        for (std::size_t first = 0, part = 0; first < codes.blocks; first += part_blocks, ++part)
            n += fi::mask_ge(codes, code_threshold(part, t), first, std::min(codes.blocks, first + part_blocks), masks);
        return n;
    }
    u128 masked_sum(const u64 *masks) const {
        u128 s = 0;
        for (std::size_t b = 0; b < codes.blocks; ++b) {
            const u64 *m = masks + 4 * b;
            if (m[0] | m[1] | m[2] | m[3])
                s += block_sum(b, m);
        }
        return s;
    }
    u64 point(std::size_t row) const { return dict[row / part_rows][fi::point_default(codes, row)]; }
};

}  // namespace frame_dict
