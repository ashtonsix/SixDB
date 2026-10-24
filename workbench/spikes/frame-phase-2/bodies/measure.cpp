// Per-block cost of each body leaf against the native narrow array (the lane
// layout over the same residuals) and the logical u64 array, at a chosen
// number of rows (which sets the residence tier). Synthetic residuals: a
// packed leaf costs the same for any values of a width except where a
// comparison exits early, so uniform values in [0, 2^K) are the general case;
// runs blocks are cut at random boundaries to a target run count.
//
// usage: frame_bodies_measure ROWS ROUNDS packed K...
//        frame_bodies_measure ROWS ROUNDS runs W N...
//        frame_bodies_measure ROWS ROUNDS patched E K...
//        frame_bodies_measure ROWS ROUNDS compact K...
//        frame_bodies_measure ROWS ROUNDS histogram K...
//        frame_bodies_measure ROWS ROUNDS wide K... (33, 40, 48, 56, 63, 64)
//        frame_bodies_measure ROWS ROUNDS weighted K... (4, 6, 8, 10, 12, 16)
// Prints one JSON line per (arm, operation): median, min and max over rounds
// of ns per block (per point for point reads). Every arm's result is checked
// against the u64 arm before timing.
#include "catalogue.h"
#include "timing.h"
#include "compact.h"
#include "exceptions.h"
#include "sort.h"
#include "weighted.h"
#include "sweeps.h"
#include "kernels.h"
#include "packing.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace bodies;
namespace fe = frame_encoding;

namespace {
using clk = std::chrono::steady_clock;

// The phase 1 arm: SeriesPack's own Local kernels through phase 1's
// catalogue, on the identical local8 bytes.
u64 phase1_sweep(op o, unsigned k, const u8 *img, std::size_t blocks, const sweep_args &a) {
    const auto &c = fe::plane_catalogue;
    const std::size_t stride = 32 * k;
    u64 s = 0;
    switch (o) {
    case op::sum:
        for (std::size_t b = 0; b < blocks; ++b)
            s += c.sum[k](img + b * stride, 256);
        return s;
    case op::count_ge:
        for (std::size_t b = 0; b < blocks; ++b)
            s += c.count_ge[k](img + b * stride, 256, a.c);
        return s;
    case op::masked_sum:
        for (std::size_t b = 0; b < blocks; ++b)
            s += c.masked_sum[k](img + b * stride, a.sel + 4 * b);
        return s;
    case op::point:
        for (std::size_t i = 0; i < a.npoints; ++i)
            s += c.point[k](img + (a.points[i] >> 8) * stride, a.points[i] & 255);
        return s;
    case op::unpack:
        for (std::size_t b = 0; b < blocks; ++b) {
            c.range32[k](img + b * stride, 0, 256, a.scratch);
            asm volatile("" ::"r"(a.scratch) : "memory");
            s += a.scratch[b & 255];
        }
        return s;
    default: std::abort();
    }
}

struct packed_image {
    layout l;
    unsigned k;
    std::vector<u8> bytes;
};

void packed_cell(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << k) - 1;
    std::vector<u64> v(blocks * block_rows);
    for (auto &x : v)
        x = rng() & top;
    std::vector<packed_image> images;
    for (layout l : {layout::lane, layout::local8, layout::plane64, layout::plane256}) {
        packed_image im{l, l == layout::lane ? k : k, std::vector<u8>(blocks * block_bytes(l, k) + 256)};
        for (std::size_t b = 0; b < blocks; ++b)
            encode(l, k, v.data() + b * block_rows, im.bytes.data() + b * block_bytes(l, k));
        images.push_back(std::move(im));
    }
    packed_image logical{layout::lane, 64, std::vector<u8>(blocks * block_rows * 8 + 256)};
    std::memcpy(logical.bytes.data(), v.data(), v.size() * 8);
    v = {};

    std::vector<u64> mask_out(4 * blocks);
    std::vector<u32> scratch(block_rows + 32);
    const std::vector<u64> sel_half = selection(blocks, 2, rng), sel_rare = selection(blocks, 64, rng);
    std::vector<u32> points(std::min<std::size_t>(rows, 1u << 20));
    for (auto &p : points)
        p = static_cast<u32>((rng() % blocks) << 8 | (rng() & 255));

    struct variant {
        op o;
        const char *name;
        sweep_args a;
        double units;
    };
    sweep_args base;
    base.mask_out = mask_out.data();
    base.scratch = scratch.data();
    std::vector<variant> variants;
    variants.push_back({op::sum, "all", base, double(blocks)});
    for (u64 c : {u64{1} << (k - 1), top}) {
        sweep_args a = base;
        a.c = c;
        const char *name = c == top ? "c=top" : "c=half";
        variants.push_back({op::count_ge, name, a, double(blocks)});
        variants.push_back({op::mask_ge, name, a, double(blocks)});
        if (c != top)
            variants.push_back({op::count_eq, name, a, double(blocks)});
    }
    for (auto *s : {&sel_half, &sel_rare}) {
        sweep_args a = base;
        a.sel = s->data();
        variants.push_back({op::masked_sum, s == &sel_half ? "sel=1/2" : "sel=1/64", a, double(blocks)});
    }
    {
        sweep_args a = base;
        a.points = points.data();
        a.npoints = points.size();
        variants.push_back({op::point, "uniform", a, double(points.size())});
    }
    variants.push_back({op::unpack, "u32", base, double(blocks)});
    variants.push_back({op::max, "all", base, double(blocks)});
    variants.push_back({op::min, "all", base, double(blocks)});
    {
        // First row >= c where about one row in 256 passes.
        sweep_args a = base;
        a.c = top - (top >> 8);
        variants.push_back({op::first_ge, "c=top-1/256", a, double(blocks)});
    }

    for (const auto &var : variants) {
        std::vector<arm> arms;
        const sweep_args a = var.a;
        const auto add = [&](const std::string &name, const packed_image &im) {
            const packed_sweep f = packed_catalogue(im.l, im.k).f[static_cast<unsigned>(var.o)];
            const u8 *p = im.bytes.data();
            arms.push_back({name, im.bytes.size() - 256, [=] { return f(p, blocks, a); }});
        };
        add("u64", logical);
        for (const auto &im : images)
            add(im.l == layout::lane ? "native" : layout_name(im.l), im);
        if (var.o == op::sum || var.o == op::point) {
            const packed_leaf &f = packed_leaves(layout::local8, k);
            const u8 *p = images[1].bytes.data();
            const std::size_t stride = 32 * k;
            if (var.o == op::sum)
                arms.push_back({"local8-exact", images[1].bytes.size() - 256, [=] {
                                    u64 s = 0;
                                    for (std::size_t b = 0; b < blocks; ++b)
                                        s += with_exact_body(p + b * stride, stride, [&](const u8 *x) { return f.sum(x); });
                                    return s;
                                }});
            else
                arms.push_back({"local8-exact", images[1].bytes.size() - 256, [=] {
                                    u64 s = 0;
                                    for (std::size_t i = 0; i < a.npoints; ++i)
                                        s += with_exact_body(p + (a.points[i] >> 8) * stride, stride,
                                                             [&](const u8 *x) { return f.point(x, a.points[i] & 255); });
                                    return s;
                                }});
        }
        if (var.o == op::sum || var.o == op::count_ge || var.o == op::masked_sum || var.o == op::point ||
            var.o == op::unpack) {
            const u8 *p = images[1].bytes.data();
            const op o = var.o;
            arms.push_back({"phase1", images[1].bytes.size() - 256, [=] { return phase1_sweep(o, k, p, blocks, a); }});
        }
        char head[256];
        std::snprintf(head, sizeof head, "\"body\":\"packed\",\"k\":%u,\"rows\":%zu,\"op\":\"%s\",\"variant\":\"%s\"", k,
                      rows, op_name(var.o), var.name);
        measure(head, arms, rounds, var.units, var.o == op::mask_ge ? mask_out.data() : nullptr, mask_out.size());
    }
}

// Runs blocks of width w with about n runs each, against the same rows
// packed (plane256), native narrow and u64.
void runs_cell(std::size_t rows, unsigned rounds, unsigned w, unsigned target, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << w) - 1;
    std::vector<u64> v(blocks * block_rows);
    std::vector<unsigned> cut(block_rows - 1);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < block_rows - 1; ++i)
            cut[i] = i + 1;
        std::shuffle(cut.begin(), cut.end(), rng);
        std::sort(cut.begin(), cut.begin() + (target - 1));
        u64 x = rng() & top;
        unsigned at = 0;
        for (unsigned i = 0; i < block_rows; ++i) {
            if (at < target - 1 && cut[at] == i) {
                u64 y;
                do
                    y = rng() & top;
                while (y == x);
                x = y, ++at;
            }
            v[b * block_rows + i] = x;
        }
    }
    std::vector<u8> runs_img;
    std::vector<u32> offset(blocks);
    std::vector<u16> n(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        n[b] = static_cast<u16>(count_runs(v.data() + b * block_rows));
        offset[b] = static_cast<u32>(runs_img.size());
        runs_img.resize(runs_img.size() + runs_bytes(n[b], w));
        encode_runs(v.data() + b * block_rows, w, runs_img.data() + offset[b]);
    }
    const std::size_t runs_size = runs_img.size();
    runs_img.resize(runs_size + 256);
    std::vector<u64> starts(4 * blocks);
    for (std::size_t b = 0; b < blocks; ++b)
        runs<u8>::run_starts(runs_img.data() + offset[b], n[b], starts.data() + 4 * b);  // ends only
    std::vector<packed_image> images;
    for (layout l : {layout::lane, layout::plane256}) {
        packed_image im{l, w, std::vector<u8>(blocks * block_bytes(l, w) + 256)};
        for (std::size_t b = 0; b < blocks; ++b)
            encode(l, w, v.data() + b * block_rows, im.bytes.data() + b * block_bytes(l, w));
        images.push_back(std::move(im));
    }
    packed_image logical{layout::lane, 64, std::vector<u8>(blocks * block_rows * 8 + 256)};
    std::memcpy(logical.bytes.data(), v.data(), v.size() * 8);

    std::vector<u64> mask_out(4 * blocks);
    std::vector<u32> scratch(block_rows + 32);
    const std::vector<u64> sel_half = selection(blocks, 2, rng), sel_rare = selection(blocks, 64, rng);
    std::vector<u32> points(std::min<std::size_t>(rows, 1u << 20));
    for (auto &p : points)
        p = static_cast<u32>((rng() % blocks) << 8 | (rng() & 255));
    // The median value: about half the rows pass.
    std::vector<u64> sample(v.begin(), v.begin() + std::min<std::size_t>(v.size(), 1 << 16));
    std::nth_element(sample.begin(), sample.begin() + sample.size() / 2, sample.end());
    const u64 median = sample[sample.size() / 2];
    v = {};

    sweep_args base;
    base.mask_out = mask_out.data();
    base.scratch = scratch.data();
    struct variant {
        op o;
        const char *name;
        sweep_args a;
        double units;
    };
    std::vector<variant> variants;
    variants.push_back({op::sum, "all", base, double(blocks)});
    {
        sweep_args a = base;
        a.c = median;
        variants.push_back({op::count_ge, "c=median", a, double(blocks)});
        variants.push_back({op::mask_ge, "c=median", a, double(blocks)});
    }
    for (auto *s : {&sel_half, &sel_rare}) {
        sweep_args a = base;
        a.sel = s->data();
        variants.push_back({op::masked_sum, s == &sel_half ? "sel=1/2" : "sel=1/64", a, double(blocks)});
    }
    {
        sweep_args a = base;
        a.points = points.data();
        a.npoints = points.size();
        variants.push_back({op::point, "uniform", a, double(points.size())});
    }
    variants.push_back({op::unpack, "u32", base, double(blocks)});
    variants.push_back({op::max, "all", base, double(blocks)});
    {
        sweep_args a = base;
        a.c = median;
        variants.push_back({op::first_ge, "c=median", a, double(blocks)});
    }
    const runs_entry &re = runs_catalogue(run_lane(w));
    for (const auto &var : variants) {
        std::vector<arm> arms;
        const sweep_args a = var.a;
        const auto add = [&](const std::string &name, const packed_image &im) {
            const packed_sweep f = packed_catalogue(im.l, im.k).f[static_cast<unsigned>(var.o)];
            const u8 *p = im.bytes.data();
            arms.push_back({name, im.bytes.size() - 256, [=] { return f(p, blocks, a); }});
        };
        add("u64", logical);
        add("native", images[0]);
        add("plane256", images[1]);
        const runs_sweep f = re.f[static_cast<unsigned>(var.o)];
        const u8 *p = runs_img.data();
        const u32 *off = offset.data();
        const u16 *nn = n.data();
        arms.push_back({"runs", runs_size + 3 * blocks, [=] { return f(p, off, nn, blocks, a); }});
        if (var.o == op::mask_ge || var.o == op::point) {
            // The run-start bitmap as evidence beside each body (+32 B/block).
            const u64 *st = starts.data();
            const auto with = [&](auto t) {
                using T = decltype(t);
                if (var.o == op::mask_ge)
                    arms.push_back({"runs-starts", runs_size + 35 * blocks, [=] {
                                        for (std::size_t b = 0; b < blocks; ++b)
                                            runs<T>::mask_ge_starts(p + off[b], nn[b], st + 4 * b, a.c, a.mask_out + 4 * b);
                                        u64 f = 0;
                                        for (std::size_t b = 0; b < blocks; b += 61)
                                            f += a.mask_out[4 * b] ^ a.mask_out[4 * b + 3];
                                        return f;
                                    }});
                else
                    arms.push_back({"runs-starts", runs_size + 35 * blocks, [=] {
                                        u64 s = 0;
                                        for (std::size_t i = 0; i < a.npoints; ++i) {
                                            const u32 b = a.points[i] >> 8;
                                            s += runs<T>::point_starts(p + off[b], nn[b], st + 4 * b, a.points[i] & 255);
                                        }
                                        return s;
                                    }});
            };
            const unsigned lane = run_lane(w);
            if (lane == 1)
                with(u8{});
            else if (lane == 2)
                with(u16{});
            else
                with(u32{});
        }
        if (var.o == op::sum || var.o == op::point) {
            // The exact boundary route: every body read from a zero-tailed copy.
            const unsigned ww = w;
            const auto with = [&](auto t) {
                using T = decltype(t);
                if (var.o == op::sum)
                    arms.push_back({"runs-exact", runs_size + 3 * blocks, [=] {
                                        u64 s = 0;
                                        for (std::size_t b = 0; b < blocks; ++b)
                                            s += with_exact_body(p + off[b], runs_bytes(nn[b], ww), [&](const u8 *x) { return runs<T>::sum(x, nn[b]); });
                                        return s;
                                    }});
                else
                    arms.push_back({"runs-exact", runs_size + 3 * blocks, [=] {
                                        u64 s = 0;
                                        for (std::size_t i = 0; i < a.npoints; ++i) {
                                            const u32 b = a.points[i] >> 8;
                                            s += with_exact_body(p + off[b], runs_bytes(nn[b], ww),
                                                                 [&](const u8 *x) { return runs<T>::point(x, nn[b], a.points[i] & 255); });
                                        }
                                        return s;
                                    }});
            };
            const unsigned lane = run_lane(w);
            if (lane == 1)
                with(u8{});
            else if (lane == 2)
                with(u16{});
            else
                with(u32{});
        }
        for (const auto &alt : re.alt)
            if (alt.f && alt.o == var.o) {
                const runs_sweep g = alt.f;
                arms.push_back({alt.name, runs_size + 3 * blocks, [=] { return g(p, off, nn, blocks, a); }});
            }
        char head[256];
        std::snprintf(head, sizeof head,
                      "\"body\":\"runs\",\"k\":%u,\"runs\":%u,\"rows\":%zu,\"op\":\"%s\",\"variant\":\"%s\"", w, target,
                      rows, op_name(var.o), var.name);
        measure(head, arms, rounds, var.units, var.o == op::mask_ge ? mask_out.data() : nullptr, mask_out.size());
    }
}
// Packed FOR at width k with exactly e exceptions per block (values in
// [2^k, 2^32)), against plane256 and native at the 32 bits the exceptions
// force without them.
template <unsigned K, unsigned E> packed_entry patched_entry() {
    return detail::packed_sweeps<patched<K>, patched_bytes(K, E)>::entry;
}
template <unsigned E, std::size_t... I> packed_entry patched_for(unsigned k, std::index_sequence<I...>) {
    packed_entry r{};
    ((k == I + 1 ? (r = patched_entry<I + 1, E>(), 0) : 0), ...);
    return r;
}
void patched_cell(std::size_t rows, unsigned rounds, unsigned k, unsigned e, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << k) - 1;
    std::vector<u64> v(blocks * block_rows);
    std::vector<unsigned> order(block_rows);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (unsigned i = 0; i < block_rows; ++i)
            v[b * block_rows + i] = rng() & top, order[i] = i;
        std::shuffle(order.begin(), order.end(), rng);
        for (unsigned i = 0; i < e; ++i)
            v[b * block_rows + order[i]] = top + 1 + rng() % (0xffffffffull - top);
    }
    const std::size_t stride = patched_bytes(k, e);
    std::vector<u8> img(blocks * stride + 256);
    for (std::size_t b = 0; b < blocks; ++b) {
        unsigned n = 0;
        if (!encode_patched(k, v.data() + b * block_rows, img.data() + b * stride, n) || n != e)
            std::abort();
    }
    std::vector<packed_image> images;
    for (layout l : {layout::lane, layout::plane256}) {
        packed_image im{l, 32, std::vector<u8>(blocks * block_bytes(l, 32) + 256)};
        for (std::size_t b = 0; b < blocks; ++b)
            encode(l, 32, v.data() + b * block_rows, im.bytes.data() + b * block_bytes(l, 32));
        images.push_back(std::move(im));
    }
    packed_image logical{layout::lane, 64, std::vector<u8>(blocks * block_rows * 8 + 256)};
    std::memcpy(logical.bytes.data(), v.data(), v.size() * 8);
    v = {};
    std::vector<u64> mask_out(4 * blocks);
    std::vector<u32> scratch(block_rows + 32);
    const std::vector<u64> sel_half = selection(blocks, 2, rng);
    std::vector<u32> points(std::min<std::size_t>(rows, 1u << 20));
    for (auto &p : points)
        p = static_cast<u32>((rng() % blocks) << 8 | (rng() & 255));
    const packed_entry pe = e == 1 ? patched_for<1>(k, std::make_index_sequence<24>{})
                          : e == 4 ? patched_for<4>(k, std::make_index_sequence<24>{})
                                   : patched_for<16>(k, std::make_index_sequence<24>{});
    sweep_args base;
    base.mask_out = mask_out.data();
    base.scratch = scratch.data();
    struct variant {
        op o;
        const char *name;
        sweep_args a;
        double units;
    };
    std::vector<variant> variants = {{op::sum, "all", base, double(blocks)}};
    {
        sweep_args a = base;
        a.c = u64{1} << (k - 1);
        variants.push_back({op::count_ge, "c=half", a, double(blocks)});
        variants.push_back({op::mask_ge, "c=half", a, double(blocks)});
        a.sel = sel_half.data();
        variants.push_back({op::masked_sum, "sel=1/2", a, double(blocks)});
        a.points = points.data();
        a.npoints = points.size();
        variants.push_back({op::point, "uniform", a, double(points.size())});
    }
    variants.push_back({op::unpack, "u32", base, double(blocks)});
    for (const auto &var : variants) {
        std::vector<arm> arms;
        const sweep_args a = var.a;
        const auto add = [&](const std::string &name, const packed_image &im) {
            const packed_sweep f = packed_catalogue(im.l, im.k).f[static_cast<unsigned>(var.o)];
            const u8 *p = im.bytes.data();
            arms.push_back({name, im.bytes.size() - 256, [=] { return f(p, blocks, a); }});
        };
        add("u64", logical);
        add("native", images[0]);
        add("plane256-32", images[1]);
        const packed_sweep f = pe.f[static_cast<unsigned>(var.o)];
        const u8 *p = img.data();
        arms.push_back({"patched", img.size() - 256, [=] { return f(p, blocks, a); }});
        char head[256];
        std::snprintf(head, sizeof head,
                      "\"body\":\"patched\",\"k\":%u,\"exceptions\":%u,\"rows\":%zu,\"op\":\"%s\",\"variant\":\"%s\"", k, e,
                      rows, op_name(var.o), var.name);
        measure(head, arms, rounds, var.units, var.o == op::mask_ge ? mask_out.data() : nullptr, mask_out.size());
    }
}
// Filter to an encoded result at width k: plane256 and local8 compactors
// into a plane256 stream, against native lane and u64 compaction.
template <layout L, std::size_t... I> auto compactors(std::index_sequence<I...>) {
    using F = void (*)(const u8 *, const u64 *, u8 *, std::size_t &);
    return std::array<F, 33>{nullptr, compactor<L, I + 1>::compact...};
}
void compact_cell(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << k) - 1;
    std::vector<u64> v(blocks * block_rows);
    for (auto &x : v)
        x = rng() & top;
    std::vector<packed_image> images;
    for (layout l : {layout::lane, layout::local8, layout::plane256}) {
        packed_image im{l, k, std::vector<u8>(blocks * block_bytes(l, k) + 256)};
        for (std::size_t b = 0; b < blocks; ++b)
            encode(l, k, v.data() + b * block_rows, im.bytes.data() + b * block_bytes(l, k));
        images.push_back(std::move(im));
    }
    packed_image logical{layout::lane, 64, std::vector<u8>(blocks * block_rows * 8 + 256)};
    std::memcpy(logical.bytes.data(), v.data(), v.size() * 8);
    static const auto plane_c = compactors<layout::plane256>(std::make_index_sequence<32>{});
    static const auto local_c = compactors<layout::local8>(std::make_index_sequence<32>{});
    for (unsigned one_in : {2u, 16u}) {
        const std::vector<u64> sel = selection(blocks, one_in, rng);
        std::vector<u64> want;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (sel[i / 64] >> (i % 64) & 1)
                want.push_back(v[i]);
        std::vector<u8> out_plane(blocks * 32 * k + 1024), out_lane(blocks * block_rows * 8 + 1024);
        const auto run_stream = [&, k](auto f, const u8 *img, std::size_t stride, std::vector<u8> &out) {
            std::size_t n = 0;
            for (std::size_t b = 0; b < blocks; ++b)
                f(img + b * stride, sel.data() + 4 * b, out.data(), n);
            return u64{n} ^ out[n % 1024];
        };
        std::vector<arm> arms;
        const auto lane_arm = [&](const std::string &name, const packed_image &im) {
            const unsigned lb = lane_bits(im.k) / 8;
            const u8 *img = im.bytes.data();
            using F = void (*)(const u8 *, const u64 *, u8 *, std::size_t &);
            const F f = lb == 1 ? F(lane_compactor<u8>::compact) : lb == 2 ? F(lane_compactor<u16>::compact)
                      : lb == 4 ? F(lane_compactor<u32>::compact) : F(lane_compactor<u64>::compact);
            arms.push_back({name, im.bytes.size() - 256, [&, f, img, lb] { return run_stream(f, img, 256 * lb, out_lane) & 0; }});
        };
        lane_arm("u64", logical);
        lane_arm("native", images[0]);
        {
            const u8 *img = images[1].bytes.data();
            const auto f = local_c[k];
            arms.push_back({"local8", images[1].bytes.size() - 256, [&, f, img] { return run_stream(f, img, 32 * k, out_plane) & 0; }});
            const u8 *img2 = images[2].bytes.data();
            const auto f2 = plane_c[k];
            arms.push_back({"plane256", images[2].bytes.size() - 256, [&, f2, img2] { return run_stream(f2, img2, 32 * k, out_plane) & 0; }});
        }
        // Each compactor's output against the selected values.
        for (std::size_t a = 0; a < arms.size(); ++a) {
            arms[a].run();
            const bool lane = a < 2;
            const unsigned lb = a == 0 ? 8 : lane_bits(k) / 8;
            for (std::size_t i = 0; i < want.size(); ++i) {
                u64 got = 0;
                if (lane)
                    std::memcpy(&got, out_lane.data() + i * lb, lb);
                else
                    got = packed_leaves(layout::plane256, k).point(out_plane.data() + (i / block_rows) * 32 * k, i % block_rows);
                if (got != want[i]) {
                    std::fprintf(stderr, "MISMATCH compact k=%u arm=%s row=%zu\n", k, arms[a].name.c_str(), i);
                    std::exit(1);
                }
            }
        }
        char head[256];
        std::snprintf(head, sizeof head, "\"body\":\"packed\",\"k\":%u,\"rows\":%zu,\"op\":\"compact\",\"variant\":\"sel=1/%u\"", k,
                      rows, one_in);
        measure(head, arms, rounds, double(blocks), nullptr, 0);
    }
}
// 2^k-bin histograms of every block (k <= 8): native u8 lanes, plane256 and
// local8 bit-sliced or decoded, and the same rows as runs of about 24.
template <layout L, std::size_t... I> auto histograms(std::index_sequence<I...>) {
    using F = void (*)(const u8 *, u32 *);
    return std::array<F, 9>{nullptr, packed_histogram<L, I + 1>::histogram...};
}
void histogram_cell(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << k) - 1;
    const unsigned bins = 1u << k;
    for (const char *shape : {"uniform", "runs24"}) {
        std::vector<u64> v(blocks * block_rows);
        if (shape[0] == 'u') {
            for (auto &x : v)
                x = rng() & top;
        } else {
            for (std::size_t b = 0; b < blocks; ++b) {
                u64 x = rng() & top;
                for (unsigned i = 0; i < block_rows; ++i) {
                    if (rng() % 11 == 0 && top)
                        x = (x + 1 + rng() % top) & top;
                    v[b * block_rows + i] = x;
                }
            }
        }
        std::vector<packed_image> images;
        for (layout l : {layout::lane, layout::local8, layout::plane256}) {
            packed_image im{l, k, std::vector<u8>(blocks * block_bytes(l, k) + 256)};
            for (std::size_t b = 0; b < blocks; ++b)
                encode(l, k, v.data() + b * block_rows, im.bytes.data() + b * block_bytes(l, k));
            images.push_back(std::move(im));
        }
        std::vector<u8> runs_img;
        std::vector<u32> offset(blocks);
        std::vector<u16> n(blocks);
        for (std::size_t b = 0; b < blocks; ++b) {
            n[b] = static_cast<u16>(count_runs(v.data() + b * block_rows));
            offset[b] = static_cast<u32>(runs_img.size());
            runs_img.resize(runs_img.size() + runs_bytes(n[b], k) + (n[b] < 2 ? 2 : 0));
            if (n[b] < 2) {  // a constant block, stored as one run
                runs_img[offset[b]] = 255;
                runs_img[offset[b] + 1] = static_cast<u8>(v[b * block_rows]);
            } else {
                encode_runs(v.data() + b * block_rows, k, runs_img.data() + offset[b]);
            }
        }
        runs_img.resize(runs_img.size() + 256);
        std::vector<u32> want(bins, 0), counts(blocks * bins);
        for (u64 x : v)
            ++want[x];
        static const auto plane_h = histograms<layout::plane256>(std::make_index_sequence<8>{});
        static const auto local_h = histograms<layout::local8>(std::make_index_sequence<8>{});
        const auto fold = [&] {
            u64 f = 0;
            for (std::size_t b = 0; b < blocks; b += 97)
                f += counts[b * bins] + counts[b * bins + bins - 1];
            return f;
        };
        std::vector<arm> arms;
        {
            const u8 *p = images[0].bytes.data();
            arms.push_back({"native", images[0].bytes.size() - 256, [&, p, bins] {
                                for (std::size_t b = 0; b < blocks; ++b)
                                    lane_histogram::histogram(p + 256 * b, bins, counts.data() + b * bins);
                                return fold();
                            }});
        }
        if (k <= 5) {
            // Native, one vector equality count per key: stronger for few bins.
            const u8 *p = images[0].bytes.data();
            arms.push_back({"native-eq", images[0].bytes.size() - 256, [&, p, bins] {
                                for (std::size_t b = 0; b < blocks; ++b)
                                    for (unsigned x = 0; x < bins; ++x)
                                        counts[b * bins + x] = lanes<u8>::count_eq(p + 256 * b, x);
                                return fold();
                            }});
        }
        for (unsigned which : {1u, 2u}) {
            const u8 *p = images[which].bytes.data();
            const auto f = which == 1 ? local_h[k] : plane_h[k];
            arms.push_back({which == 1 ? "local8" : "plane256", images[which].bytes.size() - 256, [&, p, f, k] {
                                for (std::size_t b = 0; b < blocks; ++b)
                                    f(p + 32 * k * b, counts.data() + b * bins);
                                return fold();
                            }});
        }
        {
            const u8 *p = runs_img.data();
            arms.push_back({"runs", runs_img.size() - 256, [&, p, bins] {
                                for (std::size_t b = 0; b < blocks; ++b)
                                    runs_histogram<u8>::histogram(p + offset[b], n[b] < 2 ? 1 : n[b], bins, counts.data() + b * bins);
                                return fold();
                            }});
        }
        for (auto &a : arms) {
            a.run();
            std::vector<u32> total(bins, 0);
            for (std::size_t b = 0; b < blocks; ++b)
                for (unsigned x = 0; x < bins; ++x)
                    total[x] += counts[b * bins + x];
            if (total != want) {
                std::fprintf(stderr, "MISMATCH histogram k=%u arm=%s\n", k, a.name.c_str());
                std::exit(1);
            }
        }
        double mean_runs = 0;
        for (u16 x : n)
            mean_runs += x;
        char head[256];
        std::snprintf(head, sizeof head, "\"body\":\"packed\",\"k\":%u,\"rows\":%zu,\"op\":\"histogram\",\"variant\":\"%s\",\"mean_runs\":%.1f",
                      k, rows, shape, mean_runs / blocks);
        measure(head, arms, rounds, double(blocks), nullptr, 0);
    }
}
// K 33..64: plane256 (u128 sums) against the u64 lane, which is the native
// narrow array at these widths.
template <unsigned K> void wide_cell(std::size_t rows, unsigned rounds, std::mt19937_64 &rng) {
    using P = packed<layout::plane256, K>;
    using N = lanes<u64>;
    const std::size_t blocks = rows / block_rows;
    const u64 top = K == 64 ? ~u64{0} : (u64{1} << K) - 1;
    std::vector<u64> v(blocks * block_rows);
    for (auto &x : v)
        x = rng() & top;
    std::vector<u8> plane(blocks * 32 * K + 256), native(blocks * 2048 + 256);
    for (std::size_t b = 0; b < blocks; ++b)
        encode(layout::plane256, K, v.data() + b * block_rows, plane.data() + b * 32 * K);
    std::memcpy(native.data(), v.data(), v.size() * 8);
    v = {};
    std::vector<u64> mask_out(4 * blocks);
    const std::vector<u64> sel = selection(blocks, 2, rng);
    std::vector<u32> points(std::min<std::size_t>(rows, 1u << 20));
    for (auto &x : points)
        x = static_cast<u32>((rng() % blocks) << 8 | (rng() & 255));
    alignas(64) static u64 scratch[block_rows];
    const u64 c = u64{1} << (K - 1);
    const u8 *pp = plane.data(), *np = native.data();
    const auto cellrun = [&](const char *op, auto fp, auto fn, double units, bool mask) {
        std::vector<arm> arms = {{"native", native.size() - 256, [=] { return fn(np); }},
                                 {"plane256", plane.size() - 256, [=] { return fp(pp); }}};
        char head[200];
        std::snprintf(head, sizeof head, "\"body\":\"wide\",\"k\":%u,\"rows\":%zu,\"op\":\"%s\",\"variant\":\"-\"", K, rows, op);
        measure(head, arms, rounds, units, mask ? mask_out.data() : nullptr, mask_out.size());
    };
    const double nb = double(blocks);
    // SUM folds modulo 2^64 (the u64 arm's law) so both arms agree.
    cellrun("sum", [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += static_cast<u64>(P::sum(x + b * 32 * K)); return s; },
            [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += N::sum(x + b * 2048); return s; }, nb, false);
    cellrun("count_ge", [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += P::count_ge(x + b * 32 * K, c); return s; },
            [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += N::count_ge(x + b * 2048, c); return s; }, nb, false);
    u64 *mo = mask_out.data();
    cellrun("mask_ge", [=](const u8 *x) { for (std::size_t b = 0; b < blocks; ++b) P::mask_ge(x + b * 32 * K, c, mo + 4 * b); return mo[5]; },
            [=](const u8 *x) { for (std::size_t b = 0; b < blocks; ++b) N::mask_ge(x + b * 2048, c, mo + 4 * b); return mo[5]; }, nb, true);
    const u64 *sp = sel.data();
    cellrun("masked_sum", [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += static_cast<u64>(P::masked_sum(x + b * 32 * K, sp + 4 * b)); return s; },
            [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += N::masked_sum(x + b * 2048, sp + 4 * b); return s; }, nb, false);
    const u32 *pt = points.data();
    const std::size_t np_ = points.size();
    cellrun("point", [=](const u8 *x) { u64 s = 0; for (std::size_t i = 0; i < np_; ++i) s += P::point(x + (pt[i] >> 8) * 32 * K, pt[i] & 255); return s; },
            [=](const u8 *x) { u64 s = 0; for (std::size_t i = 0; i < np_; ++i) s += N::point(x + (pt[i] >> 8) * 2048, pt[i] & 255); return s; }, double(np_), false);
    cellrun("max", [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += P::max(x + b * 32 * K); return s; },
            [=](const u8 *x) { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += N::max(x + b * 2048); return s; }, nb, false);
    cellrun("unpack", [=](const u8 *x) { u64 f = 0; for (std::size_t b = 0; b < blocks; ++b) { P::unpack64(x + b * 32 * K, scratch); asm volatile("" ::"r"(scratch) : "memory"); f += scratch[b & 255]; } return f; },
            [=](const u8 *x) { u64 f = 0; for (std::size_t b = 0; b < blocks; ++b) { std::memcpy(scratch, x + b * 2048, 2048); asm volatile("" ::"r"(scratch) : "memory"); f += scratch[b & 255]; } return f; }, nb, false);
}
void wide(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    switch (k) {
    case 33: return wide_cell<33>(rows, rounds, rng);
    case 40: return wide_cell<40>(rows, rounds, rng);
    case 48: return wide_cell<48>(rows, rounds, rng);
    case 56: return wide_cell<56>(rows, rounds, rng);
    case 63: return wide_cell<63>(rows, rounds, rng);
    case 64: return wide_cell<64>(rows, rounds, rng);
    default: std::abort();
    }
}
// SUM of D[code] over blocks of K-bit dictionary codes: plane256 codes
// (histogram dot D, or decode and gather), runs, and the native code lane,
// against the u64 array of materialised values.
template <unsigned K> void weighted_cell(std::size_t rows, unsigned rounds, std::mt19937_64 &rng) {
    const std::size_t blocks = rows / block_rows;
    const u64 top = (u64{1} << K) - 1;
    std::vector<u64> D(std::size_t{1} << K);
    for (auto &d : D)
        d = rng() >> 16;
    using T = std::conditional_t<(K <= 8), u8, u16>;
    for (const char *shape : {"uniform", "runs24"}) {
        std::vector<u64> v(blocks * block_rows);
        for (std::size_t b = 0; b < blocks; ++b) {
            u64 x = rng() & top;
            for (unsigned i = 0; i < block_rows; ++i) {
                if (shape[0] == 'u')
                    x = rng() & top;
                else if (rng() % 11 == 0)
                    x = (x + 1 + rng() % top) & top;
                v[b * block_rows + i] = x;
            }
        }
        std::vector<u8> plane(blocks * 32 * K + 256), lane(blocks * 256 * sizeof(T) + 256), values(blocks * 2048 + 256);
        std::vector<u8> runs_img;
        std::vector<u32> offset(blocks);
        std::vector<u16> n(blocks);
        u128 want = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            const u64 *x = v.data() + b * block_rows;
            encode(layout::plane256, K, x, plane.data() + b * 32 * K);
            encode(layout::lane, K, x, lane.data() + b * 256 * sizeof(T));
            for (unsigned i = 0; i < block_rows; ++i) {
                const u64 d = D[x[i]];
                std::memcpy(values.data() + (b * block_rows + i) * 8, &d, 8);
                want += d;
            }
            n[b] = static_cast<u16>(count_runs(x));
            offset[b] = static_cast<u32>(runs_img.size());
            if (n[b] < 2) {
                runs_img.push_back(255);
                runs_img.resize(runs_img.size() + sizeof(T));
                std::memcpy(runs_img.data() + offset[b] + 1, &x[0], sizeof(T));
                n[b] = 1;
            } else {
                runs_img.resize(runs_img.size() + runs_bytes(n[b], K));
                encode_runs(x, K, runs_img.data() + offset[b]);
            }
        }
        const std::size_t runs_size = runs_img.size();
        runs_img.resize(runs_size + 256);
        const u64 *d = D.data();
        const u8 *pp = plane.data(), *lp = lane.data(), *vp = values.data(), *rp = runs_img.data();
        const u32 *op = offset.data();
        const u16 *np = n.data();
        std::vector<arm> arms = {
            {"u64", values.size() - 256, [=] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += lanes<u64>::sum(vp + b * 2048); return s; }},
            {"native", lane.size() - 256, [=] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += static_cast<u64>(lane_weighted<T>::weighted_sum(lp + b * 256 * sizeof(T), d)); return s; }},
            {"plane256", plane.size() - 256, [=] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += static_cast<u64>(packed_weighted<layout::plane256, K>::weighted_sum(pp + b * 32 * K, d)); return s; }},
            {"runs", runs_size, [=] { u64 s = 0; for (std::size_t b = 0; b < blocks; ++b) s += static_cast<u64>(runs_weighted<T>::weighted_sum(rp + op[b], np[b], d)); return s; }}};
        if constexpr (K <= 8) {
            // Prepared byte tables: built once per dictionary, outside the timing.
            static byte_tables *tables = nullptr;
            delete tables;
            tables = new byte_tables(d, 1u << K);
            const byte_tables *tp = tables;
            arms.push_back({"plane256-tables", plane.size() - 256, [=] {
                                u64 s = 0;
                                for (std::size_t b = 0; b < blocks; ++b)
                                    s += static_cast<u64>(packed_weighted<layout::plane256, K>::weighted_sum(pp + b * 32 * K, *tp));
                                return s;
                            }});
        }
        if (static_cast<u64>(want) != arms[0].run()) {
            std::fprintf(stderr, "MISMATCH weighted oracle k=%u\n", K);
            std::exit(1);
        }
        double mean_runs = 0;
        for (u16 x : n)
            mean_runs += x;
        char head[220];
        std::snprintf(head, sizeof head, "\"body\":\"weighted\",\"k\":%u,\"rows\":%zu,\"op\":\"weighted_sum\",\"variant\":\"%s\",\"mean_runs\":%.1f",
                      K, rows, shape, mean_runs / blocks);
        measure(head, arms, rounds, double(blocks), nullptr, 0);
    }
}
void weighted(std::size_t rows, unsigned rounds, unsigned k, std::mt19937_64 &rng) {
    switch (k) {
    case 4: return weighted_cell<4>(rows, rounds, rng);
    case 6: return weighted_cell<6>(rows, rounds, rng);
    case 8: return weighted_cell<8>(rows, rounds, rng);
    case 10: return weighted_cell<10>(rows, rounds, rng);
    case 12: return weighted_cell<12>(rows, rounds, rng);
    case 16: return weighted_cell<16>(rows, rounds, rng);
    default: std::abort();
    }
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s ROWS ROUNDS packed K... | runs W N...\n", argv[0]);
        return 2;
    }
    const std::size_t rows = std::strtoull(argv[1], nullptr, 10) / block_rows * block_rows;
    const unsigned rounds = static_cast<unsigned>(std::atoi(argv[2]));
    const std::string body = argv[3];
    std::mt19937_64 rng(7);
    if (body == "packed") {
        for (int i = 4; i < argc; ++i)
            packed_cell(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "runs") {
        const unsigned w = static_cast<unsigned>(std::atoi(argv[4]));
        for (int i = 5; i < argc; ++i)
            runs_cell(rows, rounds, w, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "weighted") {
        for (int i = 4; i < argc; ++i)
            weighted(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "wide") {
        for (int i = 4; i < argc; ++i)
            wide(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "histogram") {
        for (int i = 4; i < argc; ++i)
            histogram_cell(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "compact") {
        for (int i = 4; i < argc; ++i)
            compact_cell(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), rng);
    } else if (body == "patched") {
        // patched E K...: e in {1, 4, 16}, k <= 24
        const unsigned e = static_cast<unsigned>(std::atoi(argv[4]));
        for (int i = 5; i < argc; ++i)
            patched_cell(rows, rounds, static_cast<unsigned>(std::atoi(argv[i])), e, rng);
    } else {
        return 2;
    }
}
