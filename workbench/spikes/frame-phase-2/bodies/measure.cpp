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
// Prints one JSON line per (arm, operation): median, min and max over rounds
// of ns per block (per point for point reads). Every arm's result is checked
// against the u64 arm before timing.
#include "catalogue.h"
#include "compact.h"
#include "exceptions.h"
#include "sort.h"
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

std::vector<u64> selection(std::size_t blocks, unsigned one_in, std::mt19937_64 &rng) {
    std::vector<u64> s(4 * blocks);
    for (auto &w : s) {
        u64 x = 0;
        for (unsigned i = 0; i < 64; ++i)
            x |= u64{rng() % one_in == 0} << i;
        w = x;
    }
    return s;
}

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

struct arm {
    std::string name;
    std::size_t bytes;
    std::function<u64()> run;
};

// Runs every arm `rounds` times in rotated order; each timing repeats the
// sweep until it covers at least 2 ms. Prints the median per unit.
void measure(const std::string &head, std::vector<arm> &arms, unsigned rounds, double units, u64 *mask_out,
             std::size_t mask_words) {
    u64 want = 0, want_mask = 0;
    for (std::size_t i = 0; i < arms.size(); ++i) {
        u64 got = arms[i].run(), mask = 0;
        for (std::size_t w = 0; mask_out && w < mask_words; ++w)
            mask = mask * 0x9e3779b97f4a7c15ull + mask_out[w];
        if (i == 0)
            want = got, want_mask = mask;
        else if (got != want || mask != want_mask) {
            std::fprintf(stderr, "MISMATCH %s arm=%s\n", head.c_str(), arms[i].name.c_str());
            std::exit(1);
        }
    }
    u64 sink = 0;
    unsigned reps = 1;
    for (;;) {
        const auto t0 = clk::now();
        for (unsigned r = 0; r < reps; ++r)
            sink += arms[0].run();
        if (std::chrono::duration<double, std::nano>(clk::now() - t0).count() > 2e6 || reps >= (1u << 22))
            break;
        reps *= 2;
    }
    std::vector<std::vector<double>> t(arms.size());
    for (unsigned round = 0; round < rounds; ++round)
        for (std::size_t i = 0; i < arms.size(); ++i) {
            const std::size_t a = (i + round) % arms.size();
            const auto t0 = clk::now();
            for (unsigned r = 0; r < reps; ++r)
                sink += arms[a].run();
            t[a].push_back(std::chrono::duration<double, std::nano>(clk::now() - t0).count() / reps / units);
        }
    for (std::size_t i = 0; i < arms.size(); ++i) {
        auto &v = t[i];
        std::sort(v.begin(), v.end());
        std::printf("{\"isa\":\"%s\",%s,\"arm\":\"%s\",\"bytes\":%zu,\"ns\":%.4f,\"min\":%.4f,\"max\":%.4f}\n", isa_name(),
                    head.c_str(), arms[i].name.c_str(), arms[i].bytes, v[v.size() / 2], v.front(), v.back());
    }
    std::fflush(stdout);
    if (sink == 0x123456789)
        std::puts("");
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
