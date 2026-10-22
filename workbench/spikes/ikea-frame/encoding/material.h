// Private experimental Frame material for the ikea-frame spike: block
// descriptors, constant/runs/frame-of-reference/delta bodies over real
// SeriesPack planes and Boundlist geometry, block masks, and the exact
// operations over them (SUM, narrow SUM, COUNT, whole-block decode). Shared by
// the timing driver (bodies.cpp), Codex's access check and Engine's consumers.
// Not a Frame API, wire format or registry; it changes with the research.
#pragma once

#include <ikea/boundlist.h>
#include <ikea/effects.h>
#include <ikea/seriespack.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace frame_encoding {
namespace sp = ikea::seriespack;
namespace bl = ikea::boundlist;
using u64 = std::uint64_t;
using u128 = unsigned __int128;
using i64 = std::int64_t;

constexpr std::size_t block_rows = 256;

// delta: previous-value prediction. d.base is the block's first value (the
// anchor, not its minimum); the payload holds 256 residuals s_i = r_i - rbase
// (s_0 = 0) as a SeriesPack plane, then rbase (i64), then 15 checkpoints of the
// cumulative stored residual at rows 16, 32, ..., 240 (u32 when K + 8 <= 32,
// else u64). Checkpoints serve point reads; SUM and COUNT here don't need them.
// patched: frame of reference with exceptions. The plane holds residuals over
// d.base at width K with 0 in exception slots; after it come the block minimum
// lo (u64), the exception count n (u8), n row positions (u8) and n exception
// values relative to lo as a SeriesPack plane at the block's full width.
enum class kind : std::uint8_t { constant, runs, packed, delta, patched };

// Exception layout inside a patched block's payload.
struct patch_view {
    u64 lo;
    unsigned n;
    const std::uint8_t *positions;
    unsigned value_width;
    const std::uint8_t *values;
};
using i128 = __int128;

// The per-block descriptor as stored; its size is charged to every block.
struct descriptor {
    u64 base;              // constant value, or frame-of-reference base
    std::uint32_t offset;  // payload offset in the column body
    std::uint16_t count;   // packed values (256, or the number of runs)
    kind type;
    std::uint8_t width;    // SeriesPack width of packed values (0 = none)
};
static_assert(sizeof(descriptor) == 16);

inline unsigned width_of(u64 span) {
    return span ? 64 - std::countl_zero(span) : 0;
}

inline std::size_t plane_bytes(unsigned k, std::size_t count) {
    return (count + 7) / 8 * k;  // Local geometry: 8-row tiles of k bytes
}

// Encode values (already relative to the base) as a Local SeriesPack plane of
// runtime width k, through the real typed mutation path.
void pack(unsigned k, std::span<const u64> values, std::uint8_t *out);

const bl::layout &run_layout();

// Run ends stored as exclusive end minus one in u8 (0..255), projected into
// Boundlist geometry as proper exclusive ends 1..256 (Codex's proposal). No
// checkpoints, control or tombstones; Boundlist's ordinary algorithms apply.
struct run_ends {
    const std::uint8_t *end_minus_one;
    unsigned n;
    unsigned size() const { return n; }
    unsigned at(unsigned rank) const { return unsigned{end_minus_one[rank]} + 1; }
};

// Delta checkpoint option, a curated choice for Engine (Ashton: Engine decides,
// Ikea exposes curated options unless one is a clear overall winner):
//   native   15 checkpoints every 16 rows at u32 (u64 when K + 8 > 32)
//   packed   the same 15 checkpoints bit-packed at K + 8 bits (SeriesPack plane)
//   every64  3 checkpoints every 64 rows at native width
//   none     no checkpoints; point reads prefix-sum from the anchor
enum class checkpoints : std::uint8_t { native, packed, every64, none };

// Encoding choices, captured per column image at construction so a retained
// image is never reinterpreted under another image's settings.
struct encode_options {
    // Runs body: u8 run-end projection (default) or the compact mutable
    // Boundlist view as a control.
    bool compact_runs = false;
    checkpoints delta_checkpoints = checkpoints::native;
};

inline const char *checkpoint_name(checkpoints mode) {
    switch (mode) {
    case checkpoints::native: return "native";
    case checkpoints::packed: return "packed";
    case checkpoints::every64: return "every64";
    case checkpoints::none: return "none";
    }
    return "?";
}

// Bytes for one delta block's checkpoints at residual width k.
inline std::size_t checkpoint_cost(checkpoints mode, unsigned k) {
    const std::size_t native = k + 8 <= 32 ? 4 : 8;
    switch (mode) {
    case checkpoints::native: return 15 * native;
    case checkpoints::packed: return k + 8 <= 64 ? plane_bytes(k + 8, 15) : 15 * 8;
    case checkpoints::every64: return 3 * native;
    case checkpoints::none: return 0;
    }
    return 0;
}

struct runs_geometry {
    std::vector<bl::byte> directory, lengths, control;
    std::optional<bl::view> view;
};

struct column {
    encode_options options;  // how this image was built
    std::vector<descriptor> blocks;
    std::vector<std::uint8_t> payload;
    std::vector<std::unique_ptr<runs_geometry>> runs;  // per block, null unless runs
    std::vector<sp::decoder<u64>> decoders;            // per block with a plane
    // Narrowest output lane per block (u8/u16/u32), for narrow-lane reduction.
    std::vector<std::optional<sp::decoder<std::uint8_t>>> d8;
    std::vector<std::optional<sp::decoder<std::uint16_t>>> d16;
    std::vector<std::optional<sp::decoder<std::uint32_t>>> d32;
    std::vector<std::optional<sp::decoder<u64>>> exceptions;  // patched blocks only
    std::size_t rows = 0;
    std::size_t descriptor_bytes() const { return blocks.size() * sizeof(descriptor); }
    std::size_t run_geometry_bytes() const {
        std::size_t total = 0;
        for (const auto &r : runs)
            if (r)
                total += r->directory.size() + r->lengths.size() + r->control.size();
        return total;
    }
};

column encode(std::span<const u64> values, encode_options options = {});

inline patch_view patches(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    const std::uint8_t *at = c.payload.data() + d.offset + plane_bytes(d.width, block_rows);
    patch_view p;
    std::memcpy(&p.lo, at, 8);
    p.n = at[8];
    p.positions = at + 9;
    p.values = at + 9 + p.n;
    p.value_width = 0;
    return p;
}

// Visit each run's (rank, size) through Boundlist range over the chosen body.
template <class F> void each_run(const column &c, std::size_t b, F &&visit) {
    const auto &d = c.blocks[b];
    auto body = [&](unsigned rank, auto interval) { visit(rank, interval.limit - interval.first); };
    bool ok;
    if (c.runs[b])
        ok = bl::range(*c.runs[b]->view, 0, d.count, body).has_value();
    else
        ok = bl::range(bl::from_ends(run_ends{c.payload.data() + d.offset +
                                                  plane_bytes(d.width, d.count),
                                              d.count},
                                     0u),
                       0, d.count, body)
                 .has_value();
    if (!ok)
        std::abort();
}

// One bit per block, little-endian within 64-bit words. Groups of 16 blocks
// (4,096 rows) are derived from the same bits: an empty group is skipped
// without touching its descriptors, a full group takes the unmasked path, and a
// mixed group visits only its set bits. Nothing here fixes Frame's geometry.
constexpr std::size_t group_blocks = 16;

struct block_mask {
    std::vector<u64> words;
    std::size_t blocks = 0;
    explicit block_mask(std::size_t n) : words((n + 63) / 64), blocks(n) {}
    void set(std::size_t b) { words[b / 64] |= u64{1} << (b % 64); }
    std::size_t popcount() const {
        std::size_t n = 0;
        for (u64 w : words)
            n += std::popcount(w);
        return n;
    }
};

template <class F> void for_each_block(const block_mask &m, F &&visit) {
    const std::size_t groups = (m.blocks + group_blocks - 1) / group_blocks;
    for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t first = g * group_blocks;
        const std::size_t limit = std::min(first + group_blocks, m.blocks);
        const unsigned bits =
            static_cast<unsigned>(m.words[first / 64] >> (first % 64)) & 0xffffu;
        if (!bits)
            continue;
        if (bits == 0xffffu && limit - first == group_blocks) {
            for (std::size_t b = first; b < limit; ++b)
                visit(b);
            continue;
        }
        for (unsigned rest = bits; rest; rest &= rest - 1) {
            const std::size_t b = first + static_cast<unsigned>(std::countr_zero(rest));
            if (b < limit)
                visit(b);
        }
    }
}

// Exact SUM of one block, decoding packed values into u64.
inline u128 block_sum(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    alignas(64) u64 buffer[block_rows];
    u128 total = 0;
    switch (d.type) {
    case kind::constant:
        return static_cast<u128>(d.base) * block_rows;
    case kind::packed: {
        c.decoders[b].read_unchecked(0, block_rows, buffer);
        if (d.width <= 56) {  // 256 residuals below 2^56 cannot overflow u64
            u64 residuals = 0;
            for (std::size_t i = 0; i < block_rows; ++i)
                residuals += buffer[i];
            total = residuals;
        } else {
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i];
        }
        return total + static_cast<u128>(d.base) * block_rows;
    }
    case kind::patched: {
        c.decoders[b].read_unchecked(0, block_rows, buffer);
        u64 residuals = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            residuals += buffer[i];  // K <= 56 for patched blocks
        const auto p = patches(c, b);
        alignas(64) u64 exc[block_rows];
        c.exceptions[b]->read_unchecked(0, p.n, exc);
        total = static_cast<u128>(d.base) * (block_rows - p.n) + residuals;
        for (unsigned e = 0; e < p.n; ++e)
            total += static_cast<u128>(p.lo) + exc[e];
        return total;
    }
    case kind::delta: {
        // sum x_i = 256 u0 + rbase * 32640 + sum_j (256 - j) s_j  (s_0 = 0):
        // a weighted reduction of the narrow residuals, no prefix decode.
        c.decoders[b].read_unchecked(0, block_rows, buffer);
        i64 rbase;
        std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
        i128 t = static_cast<i128>(d.base) * block_rows + static_cast<i128>(rbase) * 32640;
        if (d.width <= 47) {
            u64 w = 0;
            for (std::size_t j = 1; j < block_rows; ++j)
                w += (block_rows - j) * buffer[j];
            t += w;
        } else {
            for (std::size_t j = 1; j < block_rows; ++j)
                t += static_cast<i128>(block_rows - j) * buffer[j];
        }
        return static_cast<u128>(t);
    }
    case kind::runs:
        c.decoders[b].read_unchecked(0, d.count, buffer);
        each_run(c, b, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(buffer[rank] + d.base) * size;
        });
        return total;
    }
    std::abort();
}

// Exact SUM over the blocks selected by mask (one bit per block).
inline u128 encoded_sum(const column &c, const block_mask &mask) {
    u128 total = 0;
    for_each_block(mask, [&](std::size_t b) { total += block_sum(c, b); });
    return total;
}

// The same SUM, decoding packed blocks into their narrowest lane and reducing
// there before one widening add per block (Calico's split-arithmetic idea).
template <class T, class A>
A narrow_block_sum(const sp::decoder<T> &decoder) {
    alignas(64) T buffer[block_rows];
    decoder.read_unchecked(0, block_rows, buffer);
    A s = 0;
    for (std::size_t i = 0; i < block_rows; ++i)
        s += buffer[i];
    return s;
}

inline u128 encoded_sum_narrow(const column &c, const block_mask &mask) {
    u128 total = 0;
    for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        if (d.type != kind::packed) {
            total += block_sum(c, b);
            return;
        }
        total += static_cast<u128>(d.base) * block_rows;
        if (c.d8[b])
            total += narrow_block_sum<std::uint8_t, std::uint32_t>(*c.d8[b]);
        else if (c.d16[b])
            total += narrow_block_sum<std::uint16_t, std::uint32_t>(*c.d16[b]);
        else if (c.d32[b])
            total += narrow_block_sum<std::uint32_t, u64>(*c.d32[b]);
        else
            total += block_sum(c, b) - static_cast<u128>(d.base) * block_rows;
    });
    return total;
}

// Exact COUNT(u >= threshold) over the selected blocks.
inline u64 encoded_count_ge(const column &c, const block_mask &mask, u64 threshold) {
    u64 total = 0;
    alignas(64) u64 buffer[block_rows];
    for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        // Constant, runs and packed bases are block minima; a delta anchor isn't.
        if (d.type != kind::delta && d.type != kind::patched && d.base >= threshold) {
            total += block_rows;
            return;
        }
        switch (d.type) {
        case kind::constant:
            return;
        case kind::delta: {
            c.decoders[b].read_unchecked(0, block_rows, buffer);
            i64 rbase;
            std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
            u64 x = d.base;
            total += x >= threshold;
            for (std::size_t i = 1; i < block_rows; ++i) {
                x += static_cast<u64>(rbase) + buffer[i];
                total += x >= threshold;
            }
            break;
        }
        case kind::patched: {
            // Exceptions can lie below the base, so no whole-block shortcut.
            const auto p = patches(c, b);
            alignas(64) u64 exc[block_rows];
            c.exceptions[b]->read_unchecked(0, p.n, exc);
            for (unsigned e = 0; e < p.n; ++e)
                total += p.lo + exc[e] >= threshold;
            if (d.base >= threshold) {
                total += block_rows - p.n;
                return;
            }
            const u64 rhs = threshold - d.base;
            if (d.width < 64 && rhs >> d.width)
                return;
            c.decoders[b].read_unchecked(0, block_rows, buffer);
            u64 inliers = 0;
            for (std::size_t i = 0; i < block_rows; ++i)
                inliers += buffer[i] >= rhs;
            // Exception slots hold residual 0, which counts only if rhs == 0.
            total += inliers - (rhs == 0 ? p.n : 0);
            break;
        }
        case kind::packed: {
            const u64 rhs = threshold - d.base;
            if (d.width < 64 && rhs >> d.width)
                return;  // the threshold exceeds every representable residual
            c.decoders[b].read_unchecked(0, block_rows, buffer);
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i] >= rhs;
            break;
        }
        case kind::runs: {
            const u64 rhs = threshold - d.base;
            c.decoders[b].read_unchecked(0, d.count, buffer);
            each_run(c, b, [&](unsigned rank, unsigned size) {
                if (buffer[rank] >= rhs)
                    total += size;
            });
            break;
        }
        }
    });
    return total;
}

// Reconstruct one block's current working-domain values (no overrides yet).
void decode_block(const column &c, std::size_t b, u64 *out);

inline volatile u64 sink;
inline volatile u64 sink_high;
// Consume both halves of a wide result so timed code keeps the carry work.
inline void consume(u128 v) {
    sink = static_cast<u64>(v);
    sink_high = static_cast<u64>(v >> 64);
}
}  // namespace frame_encoding
