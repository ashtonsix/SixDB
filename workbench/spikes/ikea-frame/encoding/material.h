// Private experimental Frame material for the ikea-frame spike: block
// descriptors, constant/runs/frame-of-reference/delta bodies over real
// SeriesPack planes and Boundlist geometry, block masks, and the exact
// operations over them (SUM, narrow SUM, COUNT, whole-block decode), with
// optional part-wide dictionaries whose code blocks reuse those kinds. Shared by
// the timing driver (bodies.cpp), Codex's access check and Engine's consumers.
// Not a Frame API, wire format or registry; it changes with the research.
#pragma once

#include <ikea/boundlist.h>
#include <ikea/effects.h>
#include <ikea/seriespack.h>

#include "kernels.h"

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
// lo (u64), the exception count n (u8), the exception value width W (u8), n
// ascending row positions (u8) and n exception values relative to lo as a
// SeriesPack plane at width W. W is stored so the body is interpretable without
// any retained decoder binding.
// delta_patched: delta with exceptions. The delta body (plane, rbase,
// checkpoints, all as delta under the same checkpoint option) is followed by the
// patched exception tail: lo (u64), n (u8), W (u8), n ascending u8 positions and
// n values relative to lo at width W. Prediction law: exception rows are
// excluded from the chain. Let f be the first non-exception row; d.base (the
// anchor) is row f's value. Each later non-exception row i stores
// s_i = (x_i - x_p) - rbase, where p is the previous non-exception row; row f
// and every exception slot store 0 and take no step. So for any row i, with
// S(i) = s_0 + ... + s_i and m(i) = max(0, i - |{e in E : e <= i}|) steps
// taken, the chain value is anchor + rbase * m(i) + S(i): a non-exception
// row's value, or for an exception row the last non-exception value before it
// (the anchor before f). Checkpoints hold S(i) at the delta rows, i.e. the
// cumulative stored residuals of non-exception rows; exception values are never
// in them. A point reader adds rbase * m(i), counting positions <= i.
enum class kind : std::uint8_t { constant, runs, packed, delta, patched, delta_patched };

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

// Part-wide dictionary option. A part is 65,536 rows (256 blocks; the last part
// may be shorter, always whole blocks). sorted: codes are ranks of the part's
// distinct values in ascending order, so they keep order. frequency: codes
// rank values by descending frequency, ties by ascending value, so frequent
// values get small codes; order is lost.
enum class dictionary_order : std::uint8_t { none, sorted, frequency };
constexpr std::size_t part_blocks = 256;
constexpr std::size_t part_rows = part_blocks * block_rows;

inline const char *dictionary_name(dictionary_order mode) {
    switch (mode) {
    case dictionary_order::none: return "none";
    case dictionary_order::sorted: return "sorted";
    case dictionary_order::frequency: return "frequency";
    }
    return "?";
}

// Encoding choices, captured per column image at construction so a retained
// image is never reinterpreted under another image's settings.
struct encode_options {
    // Runs body: u8 run-end projection (default) or the compact mutable
    // Boundlist view as a control.
    bool compact_runs = false;
    checkpoints delta_checkpoints = checkpoints::native;
    // Off by default until point/, filter/ and execution/ consumers handle
    // kind::delta_patched; the driver opts in with --delta-exceptions.
    bool delta_exceptions = false;
    // Off by default until point/, filter/ and execution/ consumers interpret
    // coded blocks; the driver opts in with --dictionary=sorted|frequency.
    dictionary_order dictionary = dictionary_order::none;
    // Keep every part's dictionary even when it doesn't pay overall (the
    // byte model's assumption); by default a part drops a losing dictionary.
    bool dictionary_every_part = false;
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

// Dictionary layout. Each part with a dictionary stores, in the column payload
// before its blocks' bodies, one SeriesPack plane of count values in code order,
// each relative to the part minimum, at width W = max(1, width(max - min)). The
// metadata lives in this per-part record on the column, charged at its size.
// count == 0 means the part has no dictionary (dropped because it didn't pay,
// see encode) and none of its blocks is coded. One bit per block on the column
// (column::coded, charged in 64-bit words) says whether that block's stored
// values are codes. A coded block is an ordinary block of any kind over its
// codes: descriptors, layouts and bodies are unchanged and only their
// interpretation differs. Row i's value is part.min + dictionary[stored_i].
struct dictionary_part {
    u64 min;               // part minimum; dictionary values are relative to it
    std::uint32_t offset;  // dictionary plane offset in the column payload
    std::uint32_t count;   // distinct values (codes 0..count-1); 0 = no dictionary
    std::uint8_t width;    // SeriesPack width of the dictionary plane
};
static_assert(sizeof(dictionary_part) == 24);

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
    std::vector<std::optional<sp::decoder<u64>>> exceptions;  // patched, delta_patched
    std::vector<dictionary_part> parts;  // empty unless options.dictionary
    std::vector<u64> coded;              // one bit per block; empty unless options.dictionary
    std::vector<std::optional<sp::decoder<u64>>> dictionaries;  // per part with a dictionary
    std::size_t rows = 0;
    std::size_t descriptor_bytes() const { return blocks.size() * sizeof(descriptor); }
    std::size_t run_geometry_bytes() const {
        std::size_t total = 0;
        for (const auto &r : runs)
            if (r)
                total += r->directory.size() + r->lengths.size() + r->control.size();
        return total;
    }
    // Dictionary planes (inside payload), per-part records and coded bits.
    std::size_t dictionary_bytes() const {
        std::size_t total = 0;
        for (const auto &p : parts)
            total += p.count ? plane_bytes(p.width, p.count) : 0;
        return total;
    }
    std::size_t part_record_bytes() const { return parts.size() * sizeof(dictionary_part); }
    std::size_t coded_bit_bytes() const { return coded.size() * sizeof(u64); }
    std::size_t coded_blocks() const {
        std::size_t n = 0;
        for (u64 w : coded)
            n += std::popcount(w);
        return n;
    }
    // Every charged byte: dictionary planes are already inside payload.
    std::size_t total_bytes() const {
        return descriptor_bytes() + payload.size() + run_geometry_bytes() + part_record_bytes() +
               coded_bit_bytes();
    }
};

column encode(std::span<const u64> values, encode_options options = {});

inline patch_view patches(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    const std::uint8_t *at = c.payload.data() + d.offset + plane_bytes(d.width, block_rows);
    patch_view p;
    std::memcpy(&p.lo, at, 8);
    p.n = at[8];
    p.value_width = at[9];
    p.positions = at + 10;
    p.values = at + 10 + p.n;
    return p;
}

// The exception tail of a delta_patched block, after its delta checkpoints.
inline patch_view delta_patches(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    const std::uint8_t *at = c.payload.data() + d.offset + plane_bytes(d.width, block_rows) + 8 +
                             checkpoint_cost(c.options.delta_checkpoints, d.width);
    patch_view p;
    std::memcpy(&p.lo, at, 8);
    p.n = at[8];
    p.value_width = at[9];
    p.positions = at + 10;
    p.values = at + 10 + p.n;
    return p;
}

// Whole-block reconstruction of a delta_patched block under the law above.
inline void decode_delta_patched(const column &c, std::size_t b, u64 *out) {
    const auto &d = c.blocks[b];
    c.decoders[b].read_unchecked(0, block_rows, out);
    i64 rbase;
    std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
    const auto p = delta_patches(c, b);
    // step[i] is all ones at rows that take a step, zero at exception slots.
    alignas(64) u64 step[block_rows];
    std::fill(step, step + block_rows, ~u64{0});
    for (unsigned e = 0; e < p.n; ++e)
        step[p.positions[e]] = 0;
    unsigned f = 0;
    while (f < p.n && p.positions[f] == f)
        ++f;  // leading exceptions; row f is the anchor
    u64 x = d.base;
    for (std::size_t i = 0; i <= f; ++i)
        out[i] = x;
    for (std::size_t i = f + 1; i < block_rows; ++i) {
        x += (static_cast<u64>(rbase) & step[i]) + out[i];
        out[i] = x;
    }
    alignas(64) u64 exc[block_rows];
    c.exceptions[b]->read_unchecked(0, p.n, exc);
    for (unsigned e = 0; e < p.n; ++e)
        out[p.positions[e]] = p.lo + exc[e];
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

// sum_j (256 - j) * s_j over one decoded narrow residual plane.
template <class T> u64 weighted_residuals(const sp::decoder<T> &decoder) {
    alignas(64) T s[block_rows];
    decoder.read_unchecked(0, block_rows, s);
    u64 w = 0;
    for (std::size_t j = 1; j < block_rows; ++j) {
        if constexpr (sizeof(T) <= 2)
            w += static_cast<std::uint32_t>(block_rows - j) * s[j];
        else
            w += static_cast<u64>(block_rows - j) * s[j];
    }
    return w;
}

inline bool is_coded(const column &c, std::size_t b) {
    return !c.coded.empty() && (c.coded[b / 64] >> (b % 64) & 1);
}
inline const dictionary_part &part_of(const column &c, std::size_t b) {
    return c.parts[b / part_blocks];
}

// Reconstruct one block's stored values: its values, or its codes if coded.
void decode_stored(const column &c, std::size_t b, u64 *out);

// Exact SUM of one block's stored values, decoding packed values into u64.
inline u128 stored_sum(const column &c, std::size_t b) {
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
        i64 rbase;
        std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
        i128 t = static_cast<i128>(d.base) * block_rows + static_cast<i128>(rbase) * 32640;
        // Decode into the narrowest lane and weight there; products fit u32
        // for K <= 16 and the sums fit u64 for K <= 47.
        if (c.d8[b])
            return static_cast<u128>(t + weighted_residuals<std::uint8_t>(*c.d8[b]));
        if (c.d16[b])
            return static_cast<u128>(t + weighted_residuals<std::uint16_t>(*c.d16[b]));
        if (c.d32[b])
            return static_cast<u128>(t + weighted_residuals<std::uint32_t>(*c.d32[b]));
        c.decoders[b].read_unchecked(0, block_rows, buffer);
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
    case kind::delta_patched: {
        // With N = 256 - n chain rows, their m values are 0..N-1, and
        // sum_{i not in E} S(i) = sum_all S(i) - sum_{e in E} S(e), so
        // sum x_i = N u0 + rbase * N(N-1)/2 + sum_j (256 - j) s_j
        //           - sum_{e in E} S(e) + sum_{e in E} (lo + v_e).
        // The weighted term is delta's; the correction scans only up to the
        // last exception position.
        c.decoders[b].read_unchecked(0, block_rows, buffer);
        i64 rbase;
        std::memcpy(&rbase, c.payload.data() + d.offset + plane_bytes(d.width, block_rows), 8);
        const auto p = delta_patches(c, b);
        const i128 chain = block_rows - p.n;
        i128 t = static_cast<i128>(d.base) * chain + static_cast<i128>(rbase) * (chain * (chain - 1) / 2);
        const unsigned last = p.positions[p.n - 1];
        if (d.width <= 47) {  // S(i) < 2^55 and 32 of them fit u64
            u64 w = 0, s = 0, held = 0;
            for (std::size_t j = 1; j < block_rows; ++j)
                w += (block_rows - j) * buffer[j];
            for (unsigned j = 0, e = 0; j <= last; ++j) {
                s += buffer[j];
                if (p.positions[e] == j)
                    held += s, ++e;
            }
            t += static_cast<i128>(w) - static_cast<i128>(held);
        } else {
            i128 s = 0;
            for (std::size_t j = 1; j < block_rows; ++j)
                t += static_cast<i128>(block_rows - j) * buffer[j];
            for (unsigned j = 0, e = 0; j <= last; ++j) {
                s += buffer[j];
                if (p.positions[e] == j)
                    t -= s, ++e;
            }
        }
        alignas(64) u64 exc[block_rows];
        c.exceptions[b]->read_unchecked(0, p.n, exc);
        for (unsigned e = 0; e < p.n; ++e)
            t += static_cast<i128>(p.lo) + exc[e];
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

// Exact SUM of a coded block. With D the part's dictionary (relative to the part
// minimum m), SUM = 256 m + sum_i D[code_i]. Constant: 256 (m + D[base]).
// Runs: sum over runs of size * (m + D[base + run value]), one lookup per run.
// Other kinds: decode the codes, then one lookup per row. lookup(code) = D[code].
template <class L> u128 coded_sum(const column &c, std::size_t b, L &&lookup) {
    const auto &d = c.blocks[b];
    const auto &p = part_of(c, b);
    const u128 rows_min = static_cast<u128>(p.min) * block_rows;
    if (d.type == kind::constant)
        return rows_min + static_cast<u128>(lookup(d.base)) * block_rows;
    alignas(64) u64 buffer[block_rows];
    u128 total = rows_min;
    if (d.type == kind::runs) {
        c.decoders[b].read_unchecked(0, d.count, buffer);
        each_run(c, b, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(lookup(d.base + buffer[rank])) * size;
        });
        return total;
    }
    decode_stored(c, b, buffer);
    if (p.width <= 56) {  // 256 entries below 2^56 cannot overflow u64
        u64 s = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            s += lookup(buffer[i]);
        return total + s;
    }
    for (std::size_t i = 0; i < block_rows; ++i)
        total += lookup(buffer[i]);
    return total;
}

// Exact SUM of one block's values; a coded block reads D through its plane.
inline u128 block_sum(const column &c, std::size_t b) {
    if (!is_coded(c, b))
        return stored_sum(c, b);
    const auto &dictionary = *c.dictionaries[b / part_blocks];
    return coded_sum(c, b, [&](u64 code) { return dictionary.get_unchecked(code); });
}

// Mask SUM over coded blocks: blocks arrive in ascending order, so the current
// part's dictionary is decoded once into a reused array and each lookup is a
// load. This beat a plane point read per lookup on every column tried, even
// URLHash under a sparse-10% mask (~40k entries decoded per part for ~6.6k
// selected rows). Lazy: touches nothing until a coded block is visited.
struct code_sum {
    const column &c;
    std::size_t part = SIZE_MAX;
    std::vector<u64> values;
    explicit code_sum(const column &column) : c(column) {}
    u128 operator()(std::size_t b) {
        if (b / part_blocks != part) {
            part = b / part_blocks;
            values.resize(c.parts[part].count);
            c.dictionaries[part]->read_unchecked(0, values.size(), values.data());
        }
        return coded_sum(c, b, [&](u64 code) { return values[code]; });
    }
};

// Exact SUM over the blocks selected by mask (one bit per block).
inline u128 encoded_sum(const column &c, const block_mask &mask) {
    u128 total = 0;
    code_sum codes(c);
    for_each_block(mask, [&](std::size_t b) {
        total += is_coded(c, b) ? codes(b) : stored_sum(c, b);
    });
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
    code_sum codes(c);
    for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        if (is_coded(c, b)) {
            total += codes(b);
            return;
        }
        if (d.type != kind::packed) {
            total += stored_sum(c, b);
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
            total += stored_sum(c, b) - static_cast<u128>(d.base) * block_rows;
    });
    return total;
}

// Exact COUNT(stored >= threshold) within one block (values, or codes if coded).
inline u64 stored_count_ge(const column &c, std::size_t b, u64 threshold) {
    u64 total = 0;
    alignas(64) u64 buffer[block_rows];
    {
        const auto &d = c.blocks[b];
        // Constant, runs and packed bases are block minima; a delta anchor isn't,
        // and patched or delta_patched exceptions can lie below the base.
        if (d.type != kind::delta && d.type != kind::patched && d.type != kind::delta_patched &&
            d.base >= threshold) {
            return block_rows;
        }
        switch (d.type) {
        case kind::constant:
            return 0;
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
        case kind::delta_patched: {
            decode_delta_patched(c, b, buffer);
            for (std::size_t i = 0; i < block_rows; ++i)
                total += buffer[i] >= threshold;
            break;
        }
        case kind::patched: {
            // Exceptions can lie below the base, so no whole-block shortcut.
            const auto p = patches(c, b);
            alignas(64) u64 exc[block_rows];
            c.exceptions[b]->read_unchecked(0, p.n, exc);
            for (unsigned e = 0; e < p.n; ++e)
                total += p.lo + exc[e] >= threshold;
            if (d.base >= threshold)
                return total + block_rows - p.n;
            const u64 rhs = threshold - d.base;
            if (d.width < 64 && rhs >> d.width)
                return total;
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
                return 0;  // the threshold exceeds every representable residual
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
    }
    return total;
}

// COUNT(u >= t) for coded blocks, translated once per part per call (lazily,
// on the first coded block visited in that part).
//   sorted: the dictionary ascends, so value >= t iff code >= ct, where
//     ct = lower_bound(D, t - m) (0 if t <= m; count if no value reaches t).
//     The code block's ordinary stored COUNT at ct is the answer.
//   frequency: codes don't keep order, so a truth table T (one bit per code,
//     T[k] = m + D[k] >= t) built from the dictionary; COUNT = sum_i T[code_i],
//     one lookup per run for runs and the whole block for constant.
struct code_count {
    const column &c;
    u64 threshold;
    std::vector<std::uint8_t> ready;
    std::vector<u64> code_threshold;           // sorted
    std::vector<std::vector<u64>> truth;       // frequency
    // Touches nothing of the column until a coded block is visited, so an
    // empty selection still reads no owner state.
    code_count(const column &column, u64 t) : c(column), threshold(t) {}

    void prepare(std::size_t part) {
        if (ready.empty()) {
            ready.resize(c.parts.size());
            code_threshold.resize(c.parts.size());
            truth.resize(c.parts.size());
        }
        if (ready[part])
            return;
        ready[part] = 1;
        const auto &p = c.parts[part];
        const auto &dictionary = *c.dictionaries[part];
        if (c.options.dictionary == dictionary_order::sorted) {
            if (threshold <= p.min)
                return;  // code_threshold 0: every code counts
            const u64 rhs = threshold - p.min;
            u64 lo = 0, hi = p.count;
            while (lo < hi) {
                const u64 mid = lo + (hi - lo) / 2;
                if (dictionary.get_unchecked(mid) < rhs)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            code_threshold[part] = lo;
            return;
        }
        auto &t = truth[part];
        t.assign((p.count + 63) / 64, 0);
        alignas(64) u64 buffer[block_rows];
        for (u64 first = 0; first < p.count; first += block_rows) {
            const u64 n = std::min<u64>(block_rows, p.count - first);
            dictionary.read_unchecked(first, n, buffer);
            for (u64 i = 0; i < n; ++i)
                if (p.min + buffer[i] >= threshold)  // no overflow: at most the part maximum
                    t[(first + i) / 64] |= u64{1} << ((first + i) % 64);
        }
    }
    bool holds(std::size_t part, u64 code) const {
        return truth[part][code / 64] >> (code % 64) & 1;
    }

    u64 count(std::size_t b) {
        const std::size_t part = b / part_blocks;
        prepare(part);
        if (c.options.dictionary == dictionary_order::sorted) {
            const u64 ct = code_threshold[part];
            return ct >= c.parts[part].count ? 0 : stored_count_ge(c, b, ct);
        }
        const auto &d = c.blocks[b];
        if (d.type == kind::constant)
            return holds(part, d.base) ? block_rows : 0;
        alignas(64) u64 buffer[block_rows];
        u64 total = 0;
        if (d.type == kind::runs) {
            c.decoders[b].read_unchecked(0, d.count, buffer);
            each_run(c, b, [&](unsigned rank, unsigned size) {
                if (holds(part, d.base + buffer[rank]))
                    total += size;
            });
            return total;
        }
        decode_stored(c, b, buffer);
        for (std::size_t i = 0; i < block_rows; ++i)
            total += holds(part, buffer[i]);
        return total;
    }
};

// Exact COUNT(u >= threshold) within one block. A lone coded block translates
// the threshold for its part on each call; the mask operations below translate
// once per part.
inline u64 block_count_ge(const column &c, std::size_t b, u64 threshold) {
    if (!is_coded(c, b))
        return stored_count_ge(c, b, threshold);
    code_count codes(c, threshold);
    return codes.count(b);
}

// Exact COUNT(u >= threshold) over the selected blocks.
inline u64 encoded_count_ge(const column &c, const block_mask &mask, u64 threshold) {
    u64 total = 0;
    code_count codes(c, threshold);
    for_each_block(mask, [&](std::size_t b) {
        total += is_coded(c, b) ? codes.count(b) : stored_count_ge(c, b, threshold);
    });
    return total;
}

// Catalogue arms: the same SUM and COUNT over packed blocks, deriving each
// plane from the column payload and the descriptor alone (no per-block decoder
// binding), with native packet reduction for SUM and SeriesPack filters for
// COUNT. Other kinds use the owner operations above. These require the body to
// live in c.payload, so they are measured separately until every consumer
// places bodies that way.
inline u128 catalogue_sum(const column &c, const block_mask &mask) {
    u128 total = 0;
    code_sum codes(c);
    for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        if (is_coded(c, b))
            total += codes(b);
        else if (d.type == kind::packed && d.width <= 56)
            total += static_cast<u128>(d.base) * block_rows +
                     plane_kernels(d.width).sum(c.payload.data() + d.offset, block_rows);
        else
            total += stored_sum(c, b);
    });
    return total;
}

// A sorted coded packed block takes the same plane filter at the translated code
// threshold; other coded blocks use the owner's code COUNT.
inline u64 catalogue_count_ge(const column &c, const block_mask &mask, u64 threshold) {
    u64 total = 0;
    code_count codes(c, threshold);
    for_each_block(mask, [&](std::size_t b) {
        const auto &d = c.blocks[b];
        u64 t = threshold;
        if (is_coded(c, b)) {
            if (c.options.dictionary != dictionary_order::sorted || d.type != kind::packed) {
                total += codes.count(b);
                return;
            }
            codes.prepare(b / part_blocks);
            t = codes.code_threshold[b / part_blocks];
            if (t >= part_of(c, b).count)
                return;
        }
        if (d.type != kind::packed || d.base >= t) {
            total += stored_count_ge(c, b, t);
            return;
        }
        const u64 rhs = t - d.base;
        if (d.width < 64 && rhs >> d.width)
            return;
        total += plane_kernels(d.width).count_ge(c.payload.data() + d.offset, block_rows, rhs);
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
