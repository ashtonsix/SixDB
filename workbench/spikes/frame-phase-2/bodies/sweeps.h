// Sweep templates behind catalogue.h; included by the per-layout TUs.
#pragma once

#include "catalogue.h"
#include "packed.h"
#include "runs.h"

#include <utility>

namespace bodies::detail {

template <class Leaf, std::size_t stride> struct packed_sweeps {
    static u64 sum(const u8 *img, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::sum(img + b * stride);
        return s;
    }
    static u64 count_ge(const u8 *img, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::count_ge(img + b * stride, a.c);
        return s;
    }
    static u64 count_eq(const u8 *img, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::count_eq(img + b * stride, a.c);
        return s;
    }
    static u64 mask_ge(const u8 *img, std::size_t blocks, const sweep_args &a) {
        for (std::size_t b = 0; b < blocks; ++b)
            Leaf::mask_ge(img + b * stride, a.c, a.mask_out + 4 * b);
        u64 f = 0;
        for (std::size_t b = 0; b < blocks; b += 61)
            f += a.mask_out[4 * b] ^ a.mask_out[4 * b + 3];
        return f;
    }
    static u64 masked_sum(const u8 *img, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::masked_sum(img + b * stride, a.sel + 4 * b);
        return s;
    }
    static u64 point(const u8 *img, std::size_t, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t i = 0; i < a.npoints; ++i)
            s += Leaf::point(img + (a.points[i] >> 8) * stride, a.points[i] & 255);
        return s;
    }
    static u64 unpack(const u8 *img, std::size_t blocks, const sweep_args &a) {
        u64 f = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            Leaf::unpack(img + b * stride, a.scratch);
            asm volatile("" ::"r"(a.scratch) : "memory");
            f += a.scratch[b & 255];
        }
        return f;
    }
    static u64 max(const u8 *img, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::max(img + b * stride);
        return s;
    }
    static u64 min(const u8 *img, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::min(img + b * stride);
        return s;
    }
    static u64 first_ge(const u8 *img, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::first_ge(img + b * stride, a.c);
        return s;
    }
    static constexpr packed_entry entry{{sum, count_ge, count_eq, mask_ge, masked_sum, point, unpack, max, min, first_ge}};
    static constexpr packed_leaf leaf{Leaf::sum,        Leaf::count_ge, Leaf::count_eq, Leaf::mask_ge,
                                      Leaf::masked_sum, Leaf::point,    Leaf::unpack,   Leaf::max,
                                      Leaf::min,        Leaf::first_ge};
};

template <class T> struct runs_sweeps {
    using Leaf = runs<T>;
    static u64 sum(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::sum(img + off[b], n[b]);
        return s;
    }
    static u64 count_ge(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::count_ge(img + off[b], n[b], a.c);
        return s;
    }
    static u64 mask_ge(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        for (std::size_t b = 0; b < blocks; ++b)
            Leaf::mask_ge(img + off[b], n[b], a.c, a.mask_out + 4 * b);
        u64 f = 0;
        for (std::size_t b = 0; b < blocks; b += 61)
            f += a.mask_out[4 * b] ^ a.mask_out[4 * b + 3];
        return f;
    }
    template <void (*leaf)(const u8 *, unsigned, u64, u64 *)>
    static u64 mask_with(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        for (std::size_t b = 0; b < blocks; ++b)
            leaf(img + off[b], n[b], a.c, a.mask_out + 4 * b);
        u64 f = 0;
        for (std::size_t b = 0; b < blocks; b += 61)
            f += a.mask_out[4 * b] ^ a.mask_out[4 * b + 3];
        return f;
    }
    template <u64 (*leaf)(const u8 *, unsigned, const u64 *)>
    static u64 masked_with(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += leaf(img + off[b], n[b], a.sel + 4 * b);
        return s;
    }
    static u64 masked_sum(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::masked_sum(img + off[b], n[b], a.sel + 4 * b);
        return s;
    }
    static u64 point(const u8 *img, const u32 *off, const u16 *n, std::size_t, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t i = 0; i < a.npoints; ++i) {
            const u32 b = a.points[i] >> 8;
            s += Leaf::point(img + off[b], n[b], a.points[i] & 255);
        }
        return s;
    }
    static u64 unpack(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        u64 f = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            Leaf::unpack(img + off[b], n[b], a.scratch);
            asm volatile("" ::"r"(a.scratch) : "memory");
            f += a.scratch[b & 255];
        }
        return f;
    }
    static u64 max(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::max(img + off[b], n[b]);
        return s;
    }
    static u64 min(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::min(img + off[b], n[b]);
        return s;
    }
    static u64 first_ge(const u8 *img, const u32 *off, const u16 *n, std::size_t blocks, const sweep_args &a) {
        u64 s = 0;
        for (std::size_t b = 0; b < blocks; ++b)
            s += Leaf::first_ge(img + off[b], n[b], a.c);
        return s;
    }
    static constexpr runs_entry entry{{sum, count_ge, nullptr, mask_ge, masked_sum, point, unpack, max, min, first_ge},
                                      {{"runs-every-run", op::mask_ge, mask_with<Leaf::mask_ge_every_run>},
                                       {"runs-fill", op::mask_ge, mask_with<Leaf::mask_ge_fill>},
                                       {"runs-scatter", op::mask_ge, mask_with<Leaf::mask_ge_scatter>},
                                       {"runs-fill", op::masked_sum, masked_with<Leaf::masked_sum_fill>},
                                       {"runs-scalar", op::masked_sum, masked_with<Leaf::masked_sum_scalar>}}};
    static constexpr runs_leaf leaf{Leaf::sum,        Leaf::count_ge,        Leaf::mask_ge, Leaf::mask_ge_every_run,
                                    Leaf::mask_ge_fill, Leaf::mask_ge_scatter, Leaf::masked_sum, Leaf::masked_sum_fill, Leaf::masked_sum_scalar, Leaf::point,
                                    Leaf::unpack, Leaf::max, Leaf::min, Leaf::first_ge};
};

template <layout L, unsigned K> using packed_sweep_of = packed_sweeps<packed<L, K>, block_bytes(L, K)>;

template <layout L, std::size_t... I> constexpr std::array<packed_entry, 33> packed_entries(std::index_sequence<I...>) {
    return {packed_entry{}, packed_sweep_of<L, I + 1>::entry...};
}
template <layout L, std::size_t... I> constexpr std::array<packed_leaf, 33> packed_leaf_table(std::index_sequence<I...>) {
    return {packed_leaf{}, packed_sweep_of<L, I + 1>::leaf...};
}
}  // namespace bodies::detail
