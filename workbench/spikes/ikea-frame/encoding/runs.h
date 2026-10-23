// Run-value programs: how a runs block's short value plane (d.count values at
// d.width, exact tail) is read by SUM, COUNT(>=), COUNT(==), the predicate
// runs law, the row-selected forms and fused programs. Included by material.h
// before the stored-domain operations; each of them dispatches its runs case
// here unless the program is wide.
//   wide       the plane decoded into u64 lanes, run sizes from an Intervals
//              pass over the u8 ends (the original leaf).
//   narrow     the plane decoded through the catalogue into the narrowest lane
//              that holds K (range8/16/32; u64 past 32), then one walk over the
//              u8 ends with u64 v * len arithmetic.
//   in_place   as narrow, but a plane whose width is exactly 8, 16 or 32 bits
//              is read where it lies: at a byte-multiple width a Local
//              SeriesPack plane is a row-major little-endian array (R = 0, H =
//              0; run_check's width sweep checks it per width), so value
//              r is bytes [r K/8, (r + 1) K/8), inside the plane's exact
//              grant. Other widths take the narrow path.
// The program is a runtime choice, read once per runs block, so that one
// binary times every arm. The default stays wide: narrow is exact and mostly
// faster, but lost on two cells locally (README, "Run-value programs"), so
// it and in_place are opt-in. Byte-multiple planes come from
// encode_options::run_values (byte_lanes), an opt-in encoding that rounds K
// up to 8, 16 or 32 at the cost of weaker width-derived bounds
// (base + 2^K' - 1).
#pragma once

#include <bit>

namespace frame_encoding {

enum class run_program : std::uint8_t { wide, narrow, in_place };
inline const char *run_program_name(run_program p) {
    switch (p) {
    case run_program::wide: return "wide";
    case run_program::narrow: return "narrow";
    case run_program::in_place: return "in_place";
    }
    return "?";
}
inline run_program run_values_program = run_program::wide;

// A little-endian lane read in place: value r is the K'/8 bytes at r * K'/8,
// loaded by memcpy (bodies sit at any byte).
template <class T> struct in_place_lane {
    static_assert(std::endian::native == std::endian::little);
    const std::uint8_t *at;
    T operator[](unsigned r) const {
        T x;
        std::memcpy(&x, at + r * sizeof(T), sizeof(T));
        return x;
    }
};

// f(values) with values[r] the r-th run value (relative to d.base), in the
// narrowest lane the program allows. Requires a runs block and a non-wide
// program.
template <class F> decltype(auto) with_run_values(const borrowed_block &k, run_program program, F &&f) {
    const unsigned w = k.d.width, n = k.d.count;
    if (program == run_program::in_place) {
        if (w == 8)
            return f(static_cast<const std::uint8_t *>(k.body));
        if (w == 16)
            return f(in_place_lane<std::uint16_t>{k.body});
        if (w == 32)
            return f(in_place_lane<std::uint32_t>{k.body});
    }
    const auto &c = plane_catalogue;
    if (w <= 8) {
        alignas(64) std::uint8_t v[block_rows];
        c.range8[w](k.body, 0, n, v);
        return f(static_cast<const std::uint8_t *>(v));
    }
    if (w <= 16) {
        alignas(64) std::uint16_t v[block_rows];
        c.range16[w](k.body, 0, n, v);
        return f(static_cast<const std::uint16_t *>(v));
    }
    if (w <= 32) {
        alignas(64) std::uint32_t v[block_rows];
        c.range32[w](k.body, 0, n, v);
        return f(static_cast<const std::uint32_t *>(v));
    }
    alignas(64) u64 v[block_rows];
    c.range[w](k.body, 0, n, v);
    return f(static_cast<const u64 *>(v));
}

// visit(rank, first, limit) for each run, straight over the u8 ends (each
// stores its exclusive end minus one, so ends ascend to 256 by construction of
// an admitted block).
template <class F> void walk_runs(const borrowed_block &k, F &&visit) {
    const std::uint8_t *e = runs_of(k).end_minus_one;
    unsigned prev = 0;
    for (unsigned r = 0; r < k.d.count; ++r) {
        const unsigned end = unsigned{e[r]} + 1;
        visit(r, prev, end);
        prev = end;
    }
}

// SUM of a runs block: 256 base + sum v_r len_r. u64 is exact while
// 256 (2^K - 1) < 2^64, so K <= 56; wider planes accumulate u128.
inline u128 runs_sum(const borrowed_block &k, run_program program) {
    const u128 base = static_cast<u128>(k.d.base) * block_rows;
    return with_run_values(k, program, [&](const auto &v) -> u128 {
        if (k.d.width <= 56) {
            u64 s = 0;
            walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) { s += u64{v[r]} * (limit - first); });
            return base + s;
        }
        u128 s = 0;
        walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) { s += static_cast<u128>(v[r]) * (limit - first); });
        return base + s;
    });
}

// Rows of a runs block whose run value v (relative to d.base) satisfies
// keep(v): one mask-and-add per run.
template <class Keep> u64 runs_count(const borrowed_block &k, run_program program, Keep &&keep) {
    return with_run_values(k, program, [&](const auto &v) {
        u64 total = 0;
        walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) {
            total += (limit - first) & (u64{0} - static_cast<u64>(keep(u64{v[r]})));
        });
        return total;
    });
}
}  // namespace frame_encoding
