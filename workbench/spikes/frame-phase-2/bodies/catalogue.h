// Sweeps: one hoisted loop per (body, layout, width, operation) with the leaf
// inlined, so a timing measures the leaf and its loads rather than calls or
// dispatch (the substrate probe found the unit of batching moves costs by
// about 10%). Instantiated once per layout in its own TU.
#pragma once

#include "body.h"

#include <cstddef>

namespace bodies {

enum class op : u8 { sum, count_ge, count_eq, mask_ge, masked_sum, point, unpack, max, min, first_ge };
inline constexpr unsigned op_count = 10;
inline const char *op_name(op o) {
    static const char *names[] = {"sum", "count_ge", "count_eq", "mask_ge", "masked_sum", "point", "unpack", "max", "min", "first_ge"};
    return names[static_cast<unsigned>(o)];
}

struct sweep_args {
    u64 c = 0;                     // comparison constant, in the residual domain
    const u64 *sel = nullptr;      // masked_sum: four row words per block
    u64 *mask_out = nullptr;       // mask_ge: four row words per block
    const u32 *points = nullptr;   // point: block << 8 | row
    std::size_t npoints = 0;
    u32 *scratch = nullptr;        // unpack: 256 u32, reused per block
};

// Packed blocks are fixed-size and contiguous (stride block_bytes(l, k)).
// Returns a fold of the results (sums and counts add; masks and unpacked
// values are written and also folded so nothing is dead).
using packed_sweep = u64 (*)(const u8 *image, std::size_t blocks, const sweep_args &);
struct packed_entry {
    packed_sweep f[op_count] = {};
};
// k in 1..32 for bit layouts; for lane, k picks the lane (k <= 64).
const packed_entry &packed_catalogue(layout l, unsigned k);

// Runs blocks: body b at image + offset[b] with n[b] runs; values in a lane
// of `lane` bytes (1, 2 or 4).
using runs_sweep = u64 (*)(const u8 *image, const u32 *offset, const u16 *n, std::size_t blocks, const sweep_args &);
struct runs_alternative {
    const char *name = nullptr;
    op o = op::sum;
    runs_sweep f = nullptr;
};
struct runs_entry {
    runs_sweep f[op_count] = {};
    runs_alternative alt[5];  // other leaves for the same operation, timed beside f
};
const runs_entry &runs_catalogue(unsigned lane);

// The leaves the sweeps wrap, called once per block (for check.cpp).
struct packed_leaf {
    u64 (*sum)(const u8 *);
    unsigned (*count_ge)(const u8 *, u64);
    unsigned (*count_eq)(const u8 *, u64);
    void (*mask_ge)(const u8 *, u64, u64 *);
    u64 (*masked_sum)(const u8 *, const u64 *);
    u64 (*point)(const u8 *, unsigned);
    void (*unpack)(const u8 *, u32 *);
    u64 (*max)(const u8 *);
    u64 (*min)(const u8 *);
    unsigned (*first_ge)(const u8 *, u64);
};
const packed_leaf &packed_leaves(layout l, unsigned k);
struct runs_leaf {
    u64 (*sum)(const u8 *, unsigned);
    unsigned (*count_ge)(const u8 *, unsigned, u64);
    void (*mask_ge)(const u8 *, unsigned, u64, u64 *);
    void (*mask_ge_every_run)(const u8 *, unsigned, u64, u64 *);
    void (*mask_ge_fill)(const u8 *, unsigned, u64, u64 *);
    void (*mask_ge_scatter)(const u8 *, unsigned, u64, u64 *);
    u64 (*masked_sum)(const u8 *, unsigned, const u64 *);
    u64 (*masked_sum_fill)(const u8 *, unsigned, const u64 *);
    u64 (*masked_sum_scalar)(const u8 *, unsigned, const u64 *);
    u64 (*point)(const u8 *, unsigned, unsigned);
    void (*unpack)(const u8 *, unsigned, u32 *);
    u64 (*max)(const u8 *, unsigned);
    u64 (*min)(const u8 *, unsigned);
    unsigned (*first_ge)(const u8 *, unsigned, u64);
};
const runs_leaf &runs_leaves(unsigned lane);
}  // namespace bodies
