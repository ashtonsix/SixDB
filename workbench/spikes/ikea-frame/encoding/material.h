// Private experimental Frame material for the ikea-frame spike: block
// descriptors, constant/runs/frame-of-reference/delta bodies over real
// SeriesPack planes and Intervals run geometry, block masks, and the exact
// operations over them (SUM, narrow SUM, COUNT, whole-block decode), with
// optional part-wide dictionaries whose code blocks reuse those kinds. The
// operations read borrowed blocks (a body span plus explicit interpretation),
// so they serve a column image or bytes held elsewhere. Shared by the timing
// driver (bodies.cpp), Codex's access check and Engine's consumers.
// Not a Frame API, wire format or registry; it changes with the research.
#pragma once

#include <ikea/intervals.h>
#include <ikea/seriespack.h>

#include "kernels.h"
#include "packing.h"

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
namespace iv = ikea::intervals;
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


// Run ends stored as exclusive end minus one in u8 (0..255), projected into
// Intervals geometry as proper exclusive ends 1..256 (Codex's proposal). No
// checkpoints, control or tombstones; Intervals' ordinary algorithms apply.
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
    // Build column::decoders, d8, d16, d32, exceptions and dictionaries. Owner
    // operations here don't read them (they derive planes from the payload
    // through the shared catalogue); on by default for point/, filter/ and
    // execution/ consumers that still do. The driver turns it off with
    // --no-bindings.
    bool bind_decoders = true;
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
    // Retained bindings, filled only under options.bind_decoders. Owner
    // operations below never read them.
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
    // Retained binding state: the binding vectors' capacities (not charged to
    // the image's bytes).
    std::size_t binding_bytes() const {
        return decoders.capacity() * sizeof(decoders[0]) + d8.capacity() * sizeof(d8[0]) +
               d16.capacity() * sizeof(d16[0]) + d32.capacity() * sizeof(d32[0]) +
               exceptions.capacity() * sizeof(exceptions[0]) +
               dictionaries.capacity() * sizeof(dictionaries[0]);
    }
    std::size_t descriptor_bytes() const { return blocks.size() * sizeof(descriptor); }
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
        return descriptor_bytes() + payload.size() + part_record_bytes() +
               coded_bit_bytes();
    }
};

column encode(std::span<const u64> values, encode_options options = {});

// Borrowed blocks: one block's body read from wherever its bytes live (an
// Orbital Part, a copy, the column's own payload), with pointer-free
// interpretation. Every owner operation below is written once over this view;
// the column-indexed forms construct one with borrow(c, b) and call the same
// body.
//
// Body layout extent, reported by the owner and never inferred from the next
// descriptor's offset. fixed_body_bytes depends on the descriptor and the
// image's checkpoint option alone; patched and delta_patched add a variable
// tail of n positions and n values at width W, where n and W are the last two
// bytes of that fixed prefix. delta_exceptions doesn't enter: it only decides
// whether kind::delta_patched occurs. A coded block's body is its codes' body
// under the same rules; its dictionary plane is separate material
// (dictionary_view::bytes).
struct block_extent {
    std::size_t body_bytes;
    // Bytes from the body's start that owner reads may touch, for every
    // operation: body_bytes for every kind. Exact-tail reads of short planes
    // (run values, exception values, packed checkpoints) stay inside their
    // own 8-row tiles, and full-window reads only touch full 256-row planes.
    // Sufficient, not always necessary: a partially occupied final tile can
    // leave its unoccupied rows' bytes unread. borrowed_check establishes it
    // with guard pages on both sides.
    std::size_t read_grant_bytes;
    // The prefix that decode, SUM and COUNT touch: a delta block's
    // checkpoints serve point reads only, so its scans need plane + rbase.
    // read_grant_bytes for every other kind.
    std::size_t scan_grant_bytes;
};

inline std::size_t fixed_body_bytes(const descriptor &d, checkpoints mode) {
    const std::size_t plane = plane_bytes(d.width, block_rows);
    switch (d.type) {
    case kind::constant: return 0;
    case kind::runs: return plane_bytes(d.width, d.count) + d.count;  // values, u8 ends
    case kind::packed: return plane;
    case kind::delta: return plane + 8 + checkpoint_cost(mode, d.width);
    case kind::patched: return plane + 10;  // + lo, n, W
    case kind::delta_patched: return plane + 8 + checkpoint_cost(mode, d.width) + 10;
    }
    std::abort();
}

// The extent of a body within span readable bytes, by a bounded staged parse:
// the fixed prefix must fit before n and W are read from it; n >= 1 and
// 1 <= W <= 64 are required (every patch tail the encoder writes has them);
// then the positions and the tile-rounded exception plane must fit. Nullopt
// when the span or the header refuses. Reads nothing outside
// [body, body + span) and nothing at all except n and W.
inline std::optional<block_extent> extent_of(const descriptor &d, checkpoints mode,
                                             const std::uint8_t *body, std::size_t span) {
    if (d.type != kind::constant && (d.width < 1 || d.width > 64))
        return std::nullopt;
    if (d.type == kind::runs && (d.count < 1 || d.count > block_rows))
        return std::nullopt;
    std::size_t bytes = fixed_body_bytes(d, mode);
    if (bytes > span)
        return std::nullopt;
    if (d.type == kind::patched || d.type == kind::delta_patched) {
        const unsigned n = body[bytes - 2], w = body[bytes - 1];
        if (n < 1 || w < 1 || w > 64)
            return std::nullopt;
        bytes += n + plane_bytes(w, n);
        if (bytes > span)
            return std::nullopt;
    }
    const std::size_t scan =
        d.type == kind::delta ? plane_bytes(d.width, block_rows) + 8 : bytes;
    return block_extent{bytes, bytes, scan};
}

// A part dictionary as coded blocks read it: count entries in code order,
// relative to min, as one exact-tail plane of plane_bytes(width, count) bytes.
// edition names the dictionary's content: a coded block must be read with the
// dictionary it was coded against, and the mask aggregates below reuse a
// decoded dictionary or translated threshold while the edition is unchanged.
struct dictionary_view {
    u64 min;
    std::uint32_t count;
    std::uint8_t width;
    dictionary_order order;
    const std::uint8_t *plane;
    std::size_t grant;
    u64 edition;
    std::size_t bytes() const { return plane_bytes(width, count); }
};

// A non-owning view of one 256-row block. d.offset is ignored: the body is
// read at body. grant is the caller's readable bytes from body (at least
// extent_of(...).read_grant_bytes); operations don't check it, admits() does.
// rows is the block's original-coordinate population; every kind here
// requires 256. The view owns no allocation, version or liveness: it is
// valid while the caller's selected material is.
struct borrowed_block {
    descriptor d;
    const std::uint8_t *body;
    std::size_t grant;
    checkpoints delta_checkpoints;
    std::uint32_t rows = block_rows;
    bool coded = false;
    dictionary_view dictionary;  // set and read only when coded
};

// Attach-time admission of a view: kind, widths, the reported extent within
// grant, ascending patch positions, and for a coded block its dictionary's
// width, count, order and plane within the dictionary grant. No alignment is
// required: current columns place bodies at any byte, and borrowed_check
// reads every kind at all eight start residues mod 8 (header fields load
// through memcpy). Operations trust an admitted view (they don't validate
// that codes are below count).
bool admits(const borrowed_block &k);

inline dictionary_view dictionary_of(const column &c, std::size_t part) {
    const auto &p = c.parts[part];
    return {p.min,    p.count, p.width, c.options.dictionary, c.payload.data() + p.offset,
            c.payload.size() - p.offset, part};
}

inline bool is_coded(const column &c, std::size_t b) {
    return !c.coded.empty() && (c.coded[b / 64] >> (b % 64) & 1);
}
inline const dictionary_part &part_of(const column &c, std::size_t b) {
    return c.parts[b / part_blocks];
}

// Block b of a column image, with its part's dictionary when coded. The
// grant is the rest of the payload, which is readable; the edition is the
// part index, unique within the image.
inline borrowed_block borrow(const column &c, std::size_t b) {
    const auto &d = c.blocks[b];
    borrowed_block k;  // default-initialized: an uncoded view's dictionary stays unwritten
    k.d = d;
    k.body = c.payload.data() + d.offset;
    k.grant = c.payload.size() - d.offset;
    k.delta_checkpoints = c.options.delta_checkpoints;
    if (is_coded(c, b)) {
        k.coded = true;
        k.dictionary = dictionary_of(c, b / part_blocks);
    }
    return k;
}

inline patch_view patch_at(const std::uint8_t *at) {
    patch_view p;
    std::memcpy(&p.lo, at, 8);
    p.n = at[8];
    p.value_width = at[9];
    p.positions = at + 10;
    p.values = at + 10 + p.n;
    return p;
}
// The exception tail of a patched or delta_patched block; its 10-byte header
// (lo, n, W) ends the fixed prefix.
inline patch_view patches(const borrowed_block &k) {
    return patch_at(k.body + fixed_body_bytes(k.d, k.delta_checkpoints) - 10);
}
inline patch_view patches(const column &c, std::size_t b) {
    return patches(borrow(c, b));
}
inline patch_view delta_patches(const column &c, std::size_t b) {
    return patches(borrow(c, b));
}
// rbase of a delta or delta_patched block, after its residual plane.
inline i64 delta_rbase(const borrowed_block &k) {
    i64 rbase;
    std::memcpy(&rbase, k.body + plane_bytes(k.d.width, block_rows), 8);
    return rbase;
}

// Owner planes, derived from the body and descriptor or body metadata through
// the shared catalogue (kernels.h). A block's own plane is full (256
// residuals) for packed, patched, delta and delta_patched, and short (d.count
// run values) for runs; exception and dictionary planes are short. Every read
// here is exact tail unless it says otherwise.
inline const std::uint8_t *body_of(const column &c, std::size_t b) {
    return c.payload.data() + c.blocks[b].offset;
}
// Rows [first, first + take) of the block's own plane into u64 lanes.
inline void read_plane(const borrowed_block &k, std::size_t first, std::size_t take, u64 *out) {
    plane_kernels(k.d.width).range(k.body, first, take, out);
}
inline void read_plane(const column &c, std::size_t b, std::size_t first, std::size_t take,
                       u64 *out) {
    read_plane(borrow(c, b), first, take, out);
}
// All n exception values of a patch tail, relative to lo, at the body's width.
inline void read_exceptions(const patch_view &p, u64 *out) {
    if (p.n)
        plane_kernels(p.value_width).range(p.values, 0, p.n, out);
}
// Entry code of a dictionary (count > 0), relative to its minimum.
inline u64 dictionary_at(const dictionary_view &p, u64 code) {
    return plane_kernels(p.width).point(p.plane, code);
}
inline u64 dictionary_at(const column &c, std::size_t part, u64 code) {
    return dictionary_at(dictionary_of(c, part), code);
}
// Entries [first, first + take) of a dictionary plane.
inline void read_dictionary(const dictionary_view &p, std::size_t first, std::size_t take,
                            u64 *out) {
    plane_kernels(p.width).range(p.plane, first, take, out);
}
inline void read_dictionary(const column &c, std::size_t part, std::size_t first,
                            std::size_t take, u64 *out) {
    read_dictionary(dictionary_of(c, part), first, take, out);
}

// Whole-block reconstruction of a delta_patched block under the law above.
inline void decode_delta_patched(const borrowed_block &k, u64 *out) {
    const auto &d = k.d;
    read_plane(k, 0, block_rows, out);
    const i64 rbase = delta_rbase(k);
    const auto p = patches(k);
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
    read_exceptions(p, exc);
    for (unsigned e = 0; e < p.n; ++e)
        out[p.positions[e]] = p.lo + exc[e];
}
inline void decode_delta_patched(const column &c, std::size_t b, u64 *out) {
    decode_delta_patched(borrow(c, b), out);
}

inline run_ends runs_of(const borrowed_block &k) {
    return {k.body + plane_bytes(k.d.width, k.d.count), k.d.count};
}
// Visit each run's (rank, size) through Intervals range over the u8 run ends.
template <class F> void each_run(const borrowed_block &k, F &&visit) {
    auto body = [&](unsigned rank, auto interval) { visit(rank, interval.limit - interval.first); };
    const bool ok = iv::range(iv::from_ends(runs_of(k), 0u), 0, k.d.count, body).has_value();
    if (!ok)
        std::abort();
}
template <class F> void each_run(const column &c, std::size_t b, F &&visit) {
    each_run(borrow(c, b), visit);
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

// A catalogue narrow-lane range (range8/16/32).
template <class T>
using lane_range = void (*)(const std::uint8_t *, std::size_t, std::size_t, T *);

// sum_j (256 - j) * s_j over one full residual plane decoded into lane T.
template <class T> u64 weighted_residuals(lane_range<T> range, const std::uint8_t *plane) {
    alignas(64) T s[block_rows];
    range(plane, 0, block_rows, s);
    u64 w = 0;
    for (std::size_t j = 1; j < block_rows; ++j) {
        if constexpr (sizeof(T) <= 2)
            w += static_cast<std::uint32_t>(block_rows - j) * s[j];
        else
            w += static_cast<u64>(block_rows - j) * s[j];
    }
    return w;
}

// Stored-domain operations read a block's stored values: its values, or its
// codes if coded, never the dictionary. Logical-value operations (block_sum,
// block_count_ge, value_at, decode_block and the mask aggregates) map a coded
// block's codes through its dictionary: row i's value is min + D[stored_i].

// Stored value of one row (exact tail, plus checkpoints for delta kinds).
u64 stored_at(const borrowed_block &k, unsigned row);
inline u64 value_at(const borrowed_block &k, unsigned row) {
    const u64 v = stored_at(k, row);
    return k.coded ? k.dictionary.min + dictionary_at(k.dictionary, v) : v;
}
inline u64 value_at(const column &c, std::size_t row) {
    return value_at(borrow(c, row / block_rows), static_cast<unsigned>(row % block_rows));
}

// Reconstruct one block's stored values.
void decode_stored(const borrowed_block &k, u64 *out);
void decode_stored(const column &c, std::size_t b, u64 *out);

// Exact SUM of one block's stored values, decoding packed values into u64.
inline u128 stored_sum(const borrowed_block &k) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    u128 total = 0;
    switch (d.type) {
    case kind::constant:
        return static_cast<u128>(d.base) * block_rows;
    case kind::packed: {
        read_plane(k, 0, block_rows, buffer);
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
        read_plane(k, 0, block_rows, buffer);
        u64 residuals = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            residuals += buffer[i];  // K <= 56 for patched blocks
        const auto p = patches(k);
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        total = static_cast<u128>(d.base) * (block_rows - p.n) + residuals;
        for (unsigned e = 0; e < p.n; ++e)
            total += static_cast<u128>(p.lo) + exc[e];
        return total;
    }
    case kind::delta: {
        // sum x_i = 256 u0 + rbase * 32640 + sum_j (256 - j) s_j  (s_0 = 0):
        // a weighted reduction of the narrow residuals, no prefix decode.
        i128 t = static_cast<i128>(d.base) * block_rows + static_cast<i128>(delta_rbase(k)) * 32640;
        // Decode into the narrowest lane and weight there; products fit u32
        // for K <= 16 and the sums fit u64 for K <= 47.
        const auto &ops = plane_kernels(d.width);
        if (ops.range8)
            return static_cast<u128>(t + weighted_residuals(ops.range8, k.body));
        if (ops.range16)
            return static_cast<u128>(t + weighted_residuals(ops.range16, k.body));
        if (ops.range32)
            return static_cast<u128>(t + weighted_residuals(ops.range32, k.body));
        read_plane(k, 0, block_rows, buffer);
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
        read_plane(k, 0, block_rows, buffer);
        const i64 rbase = delta_rbase(k);
        const auto p = patches(k);
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
        read_exceptions(p, exc);
        for (unsigned e = 0; e < p.n; ++e)
            t += static_cast<i128>(p.lo) + exc[e];
        return static_cast<u128>(t);
    }
    case kind::runs:
        read_plane(k, 0, d.count, buffer);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(buffer[rank] + d.base) * size;
        });
        return total;
    }
    std::abort();
}
inline u128 stored_sum(const column &c, std::size_t b) {
    return stored_sum(borrow(c, b));
}

// Exact SUM of a coded block. With D the part's dictionary (relative to the part
// minimum m), SUM = 256 m + sum_i D[code_i]. Constant: 256 (m + D[base]).
// Runs: sum over runs of size * (m + D[base + run value]), one lookup per run.
// Other kinds: decode the codes, then one lookup per row. lookup(code) = D[code].
template <class L> u128 coded_sum(const borrowed_block &k, L &&lookup) {
    const auto &d = k.d;
    const auto &p = k.dictionary;
    const u128 rows_min = static_cast<u128>(p.min) * block_rows;
    if (d.type == kind::constant)
        return rows_min + static_cast<u128>(lookup(d.base)) * block_rows;
    alignas(64) u64 buffer[block_rows];
    u128 total = rows_min;
    if (d.type == kind::runs) {
        read_plane(k, 0, d.count, buffer);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(lookup(d.base + buffer[rank])) * size;
        });
        return total;
    }
    decode_stored(k, buffer);
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
inline u128 block_sum(const borrowed_block &k) {
    if (!k.coded)
        return stored_sum(k);
    return coded_sum(k, [&](u64 code) { return dictionary_at(k.dictionary, code); });
}
inline u128 block_sum(const column &c, std::size_t b) {
    return block_sum(borrow(c, b));
}

// Mask SUM over coded blocks: blocks arrive in ascending order, so the current
// edition's dictionary is decoded once into a reused array and each lookup is a
// load. This beat a plane point read per lookup on every column tried, even
// URLHash under a sparse-10% mask (~40k entries decoded per part for ~6.6k
// selected rows). Lazy: touches nothing until a coded block is visited.
struct code_sum {
    bool ready = false;
    u64 edition = 0;
    std::vector<u64> values;
    code_sum() = default;
    explicit code_sum(const column &) {}
    u128 operator()(const borrowed_block &k) {
        if (!ready || k.dictionary.edition != edition) {
            ready = true;
            edition = k.dictionary.edition;
            values.resize(k.dictionary.count);
            read_dictionary(k.dictionary, 0, values.size(), values.data());
        }
        return coded_sum(k, [&](u64 code) { return values[code]; });
    }
};

// Exact SUM over the blocks selected by mask (one bit per block).
inline u128 encoded_sum(const column &c, const block_mask &mask) {
    u128 total = 0;
    code_sum codes;
    for_each_block(mask, [&](std::size_t b) {
        const auto k = borrow(c, b);
        total += k.coded ? codes(k) : stored_sum(k);
    });
    return total;
}

// The same SUM, decoding packed blocks into their narrowest lane and reducing
// there before one widening add per block (Calico's split-arithmetic idea).
template <class A, class T>
A narrow_block_sum(lane_range<T> range, const std::uint8_t *plane) {
    alignas(64) T buffer[block_rows];
    range(plane, 0, block_rows, buffer);
    A s = 0;
    for (std::size_t i = 0; i < block_rows; ++i)
        s += buffer[i];
    return s;
}

// Stored SUM of one block, packed blocks through their narrowest lane.
inline u128 narrow_stored_sum(const borrowed_block &k) {
    const auto &d = k.d;
    if (d.type != kind::packed)
        return stored_sum(k);
    const u128 base = static_cast<u128>(d.base) * block_rows;
    const auto &ops = plane_kernels(d.width);
    if (ops.range8)
        return base + narrow_block_sum<std::uint32_t>(ops.range8, k.body);
    if (ops.range16)
        return base + narrow_block_sum<std::uint32_t>(ops.range16, k.body);
    if (ops.range32)
        return base + narrow_block_sum<u64>(ops.range32, k.body);
    return stored_sum(k);
}

inline u128 encoded_sum_narrow(const column &c, const block_mask &mask) {
    u128 total = 0;
    code_sum codes;
    for_each_block(mask, [&](std::size_t b) {
        const auto k = borrow(c, b);
        total += k.coded ? codes(k) : narrow_stored_sum(k);
    });
    return total;
}

// Exact COUNT(stored >= threshold) within one block (values, or codes if coded).
inline u64 stored_count_ge(const borrowed_block &k, u64 threshold) {
    u64 total = 0;
    alignas(64) u64 buffer[block_rows];
    const auto &d = k.d;
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
        read_plane(k, 0, block_rows, buffer);
        const i64 rbase = delta_rbase(k);
        u64 x = d.base;
        total += x >= threshold;
        for (std::size_t i = 1; i < block_rows; ++i) {
            x += static_cast<u64>(rbase) + buffer[i];
            total += x >= threshold;
        }
        break;
    }
    case kind::delta_patched: {
        decode_delta_patched(k, buffer);
        for (std::size_t i = 0; i < block_rows; ++i)
            total += buffer[i] >= threshold;
        break;
    }
    case kind::patched: {
        // Exceptions can lie below the base, so no whole-block shortcut.
        const auto p = patches(k);
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        for (unsigned e = 0; e < p.n; ++e)
            total += p.lo + exc[e] >= threshold;
        if (d.base >= threshold)
            return total + block_rows - p.n;
        const u64 rhs = threshold - d.base;
        if (d.width < 64 && rhs >> d.width)
            return total;
        read_plane(k, 0, block_rows, buffer);
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
        read_plane(k, 0, block_rows, buffer);
        for (std::size_t i = 0; i < block_rows; ++i)
            total += buffer[i] >= rhs;
        break;
    }
    case kind::runs: {
        const u64 rhs = threshold - d.base;
        read_plane(k, 0, d.count, buffer);
        each_run(k, [&](unsigned rank, unsigned size) {
            if (buffer[rank] >= rhs)
                total += size;
        });
        break;
    }
    }
    return total;
}
inline u64 stored_count_ge(const column &c, std::size_t b, u64 threshold) {
    return stored_count_ge(borrow(c, b), threshold);
}

// COUNT(u >= t) for coded blocks, translated once per dictionary edition
// (lazily, on the first coded block visited with it; blocks of one edition
// arrive together in ascending masks).
//   sorted: the dictionary ascends, so value >= t iff code >= ct, where
//     ct = lower_bound(D, t - m) (0 if t <= m; count if no value reaches t).
//     The code block's ordinary stored COUNT at ct is the answer.
//   frequency: codes don't keep order, so a truth table T (one bit per code,
//     T[k] = m + D[k] >= t) built from the dictionary; COUNT = sum_i T[code_i],
//     one lookup per run for runs and the whole block for constant.
struct code_count {
    const column *c = nullptr;  // only for the column-indexed count(b)
    u64 threshold;
    bool ready = false;
    u64 edition = 0;
    u64 code_threshold = 0;  // sorted
    std::vector<u64> truth;  // frequency
    // Touches nothing until a coded block is visited, so an empty selection
    // still reads no owner state.
    explicit code_count(u64 t) : threshold(t) {}
    code_count(const column &column, u64 t) : c(&column), threshold(t) {}

    void prepare(const dictionary_view &p) {
        if (ready && p.edition == edition)
            return;
        ready = true;
        edition = p.edition;
        code_threshold = 0;
        if (p.order == dictionary_order::sorted) {
            if (threshold <= p.min)
                return;  // code_threshold 0: every code counts
            const u64 rhs = threshold - p.min;
            u64 lo = 0, hi = p.count;
            while (lo < hi) {
                const u64 mid = lo + (hi - lo) / 2;
                if (dictionary_at(p, mid) < rhs)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            code_threshold = lo;
            return;
        }
        truth.assign((p.count + 63) / 64, 0);
        alignas(64) u64 buffer[block_rows];
        for (u64 first = 0; first < p.count; first += block_rows) {
            const u64 n = std::min<u64>(block_rows, p.count - first);
            read_dictionary(p, first, n, buffer);
            for (u64 i = 0; i < n; ++i)
                if (p.min + buffer[i] >= threshold)  // no overflow: at most the part maximum
                    truth[(first + i) / 64] |= u64{1} << ((first + i) % 64);
        }
    }
    bool holds(u64 code) const { return truth[code / 64] >> (code % 64) & 1; }

    u64 count(const borrowed_block &k) {
        prepare(k.dictionary);
        if (k.dictionary.order == dictionary_order::sorted)
            return code_threshold >= k.dictionary.count ? 0 : stored_count_ge(k, code_threshold);
        const auto &d = k.d;
        if (d.type == kind::constant)
            return holds(d.base) ? block_rows : 0;
        alignas(64) u64 buffer[block_rows];
        u64 total = 0;
        if (d.type == kind::runs) {
            read_plane(k, 0, d.count, buffer);
            each_run(k, [&](unsigned rank, unsigned size) {
                if (holds(d.base + buffer[rank]))
                    total += size;
            });
            return total;
        }
        decode_stored(k, buffer);
        for (std::size_t i = 0; i < block_rows; ++i)
            total += holds(buffer[i]);
        return total;
    }
    u64 count(std::size_t b) { return count(borrow(*c, b)); }
};

// Exact COUNT(u >= threshold) within one block. A lone coded block translates
// the threshold for its dictionary on each call; the mask operations below
// translate once per edition.
inline u64 block_count_ge(const borrowed_block &k, u64 threshold) {
    if (!k.coded)
        return stored_count_ge(k, threshold);
    code_count codes(threshold);
    return codes.count(k);
}
inline u64 block_count_ge(const column &c, std::size_t b, u64 threshold) {
    return block_count_ge(borrow(c, b), threshold);
}

// Exact COUNT(u >= threshold) over the selected blocks.
inline u64 encoded_count_ge(const column &c, const block_mask &mask, u64 threshold) {
    u64 total = 0;
    code_count codes(threshold);
    for_each_block(mask, [&](std::size_t b) {
        const auto k = borrow(c, b);
        total += k.coded ? codes.count(k) : stored_count_ge(k, threshold);
    });
    return total;
}

// Catalogue arms: the same SUM and COUNT, but packed blocks take the
// catalogue's native packet SUM (full window over their 256-row plane) and
// SeriesPack filters for COUNT instead of a range decode. Other kinds use the
// owner operations above, which read the same catalogue.
inline u128 catalogue_stored_sum(const borrowed_block &k) {
    const auto &d = k.d;
    if (d.type == kind::packed && d.width <= 56)
        return static_cast<u128>(d.base) * block_rows + plane_kernels(d.width).sum(k.body, block_rows);
    return stored_sum(k);
}
inline u128 catalogue_sum(const column &c, const block_mask &mask) {
    u128 total = 0;
    code_sum codes;
    for_each_block(mask, [&](std::size_t b) {
        const auto k = borrow(c, b);
        total += k.coded ? codes(k) : catalogue_stored_sum(k);
    });
    return total;
}

inline u64 catalogue_stored_count_ge(const borrowed_block &k, u64 t) {
    const auto &d = k.d;
    if (d.type != kind::packed || d.base >= t)
        return stored_count_ge(k, t);
    const u64 rhs = t - d.base;
    if (d.width < 64 && rhs >> d.width)
        return 0;
    return plane_kernels(d.width).count_ge(k.body, block_rows, rhs);
}
// A sorted coded packed block takes the same plane filter at the translated code
// threshold; other coded blocks use the owner's code COUNT.
inline u64 catalogue_block_count_ge(code_count &codes, const borrowed_block &k) {
    if (!k.coded)
        return catalogue_stored_count_ge(k, codes.threshold);
    if (k.dictionary.order != dictionary_order::sorted || k.d.type != kind::packed)
        return codes.count(k);
    codes.prepare(k.dictionary);
    if (codes.code_threshold >= k.dictionary.count)
        return 0;
    return catalogue_stored_count_ge(k, codes.code_threshold);
}
inline u64 catalogue_count_ge(const column &c, const block_mask &mask, u64 threshold) {
    u64 total = 0;
    code_count codes(threshold);
    for_each_block(mask, [&](std::size_t b) { total += catalogue_block_count_ge(codes, borrow(c, b)); });
    return total;
}

// Reconstruct one block's current working-domain values (no overrides yet).
void decode_block(const borrowed_block &k, u64 *out);
void decode_block(const column &c, std::size_t b, u64 *out);

inline volatile u64 sink;
inline volatile u64 sink_high;
// Consume both halves of a wide result so timed code keeps the carry work.
inline void consume(u128 v) {
    sink = static_cast<u64>(v);
    sink_high = static_cast<u64>(v >> 64);
}
}  // namespace frame_encoding
