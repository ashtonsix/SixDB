// Real columns through the front C leaves. Each 256-row block becomes a
// constant, a runs body or a packed body (plane256 at K <= 32, else a u64
// lane), chosen by a policy; blocks are grouped by (kind, width) and each
// group is stored contiguously with its block ids, so one hoisted loop per
// group calls the leaf (the substrate probe's kind index; reordering blocks
// is lawful here because every result is per block or a sum).
//
// Operations: SUM, COUNT(x >= median), the median predicate's row masks,
// SUM of this column over another column's predicate rows (pairs), and
// independent point reads. Controls: the native global-FOR narrow array and
// the u64 array, with the same lane leaves. Every answer is checked against
// an oracle over the original values.
//
// usage: frame_bodies_columns COLS_DIR ROWS ROUNDS POLICY COLUMN[:PREDICATE_COLUMN]...
//   POLICY: bytes (smaller of runs and packed), packed (no runs bodies),
//   runsN (runs when a block has at most N runs); +starts adds run-start
//   bitmaps (32 B per run block) used for masks and points
#include "catalogue.h"
#include "packed.h"
#include "runs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace bodies;

namespace {
using clk = std::chrono::steady_clock;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(2);
    }
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
inline unsigned width_of(u64 span) { return span ? 64 - static_cast<unsigned>(std::countl_zero(span)) : 0; }

// Exact SUM of 256 u64 values (optionally selected rows) by 32-bit halves.
u128 exact_sum64(const u8 *p, const u64 *sel = nullptr) {
    typedef u64 v8 __attribute__((ext_vector_type(8)));
    v8 lo = 0, hi = 0;
    for (unsigned v = 0; v < 32; ++v) {
        v8 x;
        std::memcpy(&x, p + 64 * v, 64);
        if (sel) {
            const u8 bits = static_cast<u8>(sel[v / 8] >> (8 * (v % 8)));
            const v8 bit = {1, 2, 4, 8, 16, 32, 64, 128};
            x = ((bit & bits) != 0) ? x : v8(0);
        }
        lo += x & 0xffffffffu;
        hi += x >> 32;
    }
    return static_cast<u128>(__builtin_reduce_add(lo)) + (static_cast<u128>(__builtin_reduce_add(hi)) << 32);
}

enum kind : u8 { constant, runs8, runs16, runs32, runs64, plane, wide };


// One (kind, width) group: block ids, bases, body bytes back to back.
struct group {
    kind k;
    unsigned width;
    std::vector<u32> ids;
    std::vector<u64> base;
    std::vector<u32> offset;  // runs: per block; packed/wide: fixed stride
    std::vector<u16> n;       // runs
    std::vector<u8> bytes;
    std::vector<u64> starts;  // runs, with the +starts policy: run-start bitmaps (evidence)
};

struct frame_column {
    std::size_t blocks = 0;
    std::vector<group> groups;
    std::vector<std::pair<u32, u32>> where;  // block -> (group, index)
    std::size_t image_bytes = 0;
};

frame_column encode(const std::vector<u64> &v, std::string policy) {
    frame_column f;
    bool with_starts = false;
    if (const auto plus = policy.find("+starts"); plus != std::string::npos)
        with_starts = true, policy = policy.substr(0, plus);
    f.blocks = v.size() / block_rows;
    f.where.resize(f.blocks);
    std::vector<int> slot(7 * 65, -1);
    unsigned runs_max = 0;
    if (policy.rfind("runs", 0) == 0)
        runs_max = static_cast<unsigned>(std::atoi(policy.c_str() + 4));
    std::vector<u64> r(block_rows);
    for (std::size_t b = 0; b < f.blocks; ++b) {
        const u64 *x = v.data() + b * block_rows;
        const auto [lo, hi] = std::minmax_element(x, x + block_rows);
        const unsigned k = width_of(*hi - *lo);
        for (unsigned i = 0; i < block_rows; ++i)
            r[i] = x[i] - *lo;
        const unsigned n = count_runs(r.data());
        kind kd;
        if (n == 1)
            kd = constant;
        else {
            const std::size_t packed_bytes = k > 32 ? 8 * block_rows : 32 * k, run_bytes = runs_bytes(n, k);
            const bool use_runs = policy == "bytes" ? run_bytes < packed_bytes : policy == "packed" ? false : n <= runs_max;
            kd = use_runs ? (k <= 8 ? runs8 : k <= 16 ? runs16 : k <= 32 ? runs32 : runs64) : k > 32 ? wide : plane;
        }
        const unsigned w = kd == constant ? 0 : kd == wide ? 64 : k;
        int &s = slot[kd * 65 + w];
        if (s < 0) {
            s = static_cast<int>(f.groups.size());
            f.groups.push_back({kd, w, {}, {}, {}, {}, {}});
        }
        group &g = f.groups[static_cast<unsigned>(s)];
        f.where[b] = {static_cast<u32>(s), static_cast<u32>(g.ids.size())};
        g.ids.push_back(static_cast<u32>(b));
        g.base.push_back(*lo);
        const std::size_t at = g.bytes.size();
        switch (kd) {
        case constant: break;
        case runs8:
        case runs16:
        case runs32:
        case runs64:
            g.offset.push_back(static_cast<u32>(at));
            g.n.push_back(static_cast<u16>(n));
            g.bytes.resize(at + runs_bytes(n, k));
            encode_runs(r.data(), k, g.bytes.data() + at);
            if (with_starts) {
                g.starts.resize(g.starts.size() + 4);
                runs<u8>::run_starts(g.bytes.data() + at, n, g.starts.data() + g.starts.size() - 4);
            }
            break;
        case plane:
            g.bytes.resize(at + block_bytes(layout::plane256, k));
            bodies::encode(layout::plane256, k, r.data(), g.bytes.data() + at);
            break;
        case wide:
            g.bytes.resize(at + 8 * block_rows);
            std::memcpy(g.bytes.data() + at, r.data(), 8 * block_rows);
            break;
        }
    }
    for (auto &g : f.groups) {
        // Charge a 16-byte descriptor per block, as phase 1 did.
        f.image_bytes += g.bytes.size() + 16 * g.ids.size() + 8 * g.starts.size();
        g.bytes.resize(g.bytes.size() + 256);
    }
    return f;
}

// Packed leaves by width, as a function table over the group.
template <unsigned K> using P = bodies::packed<layout::plane256, K>;
template <class F, std::size_t... I> void with_width_(unsigned k, F &&f, std::index_sequence<I...>) {
    ((k == I + 1 ? (f(std::integral_constant<unsigned, I + 1>{}), 0) : 0), ...);
}
template <class F> void with_width(unsigned k, F &&f) { with_width_(k, f, std::make_index_sequence<32>{}); }
template <class F> void with_runs(kind k, F &&f) {
    if (k == runs8)
        f(runs<u8>{});
    else if (k == runs16)
        f(runs<u16>{});
    else if (k == runs32)
        f(runs<u32>{});
    else
        f(runs<u64>{});
}

// Stored-domain results plus base terms, per group.
u128 frame_sum(const frame_column &f) {
    u128 total = 0;
    for (const auto &g : f.groups) {
        u128 s = 0, bases = 0;
        for (u64 b : g.base)
            bases += b;
        const u8 *p = g.bytes.data();
        const std::size_t m = g.ids.size();
        switch (g.k) {
        case constant: break;
        case runs8:
        case runs16:
        case runs32:
        case runs64:
            with_runs(g.k, [&](auto r) {
                using R = decltype(r);
                for (std::size_t i = 0; i < m; ++i)
                    s += R::sum(p + g.offset[i], g.n[i]);
            });
            break;
        case plane:
            with_width(g.width, [&](auto K) {
                for (std::size_t i = 0; i < m; ++i)
                    s += P<K>::sum(p + i * 32 * K);
            });
            break;
        case wide:
            for (std::size_t i = 0; i < m; ++i)
                s += exact_sum64(p + i * 2048);
            break;
        }
        total += bases * block_rows + s;
    }
    return total;
}

u64 frame_count_ge(const frame_column &f, u64 t) {
    u64 total = 0;
    for (const auto &g : f.groups) {
        const u8 *p = g.bytes.data();
        const std::size_t m = g.ids.size();
        const u64 top = g.width >= 64 ? ~u64{0} : (u64{1} << g.width) - 1;
        for (std::size_t i = 0; i < m; ++i) {
            const u64 base = g.base[i];
            // Decided by bounds: every row passes, or none.
            if (t <= base) {
                total += block_rows;
                continue;
            }
            if (t - base > top)
                continue;
            const u64 c = t - base;
            switch (g.k) {
            case constant: break;
            case runs8: total += runs<u8>::count_ge(p + g.offset[i], g.n[i], c); break;
            case runs16: total += runs<u16>::count_ge(p + g.offset[i], g.n[i], c); break;
            case runs32: total += runs<u32>::count_ge(p + g.offset[i], g.n[i], c); break;
            case runs64: total += runs<u64>::count_ge(p + g.offset[i], g.n[i], c); break;
            case plane: with_width(g.width, [&](auto K) { total += P<K>::count_ge(p + i * 32 * K, c); }); break;
            case wide: total += lanes<u64>::count_ge(p + i * 2048, c); break;
            }
        }
    }
    return total;
}

// Row masks of x >= t for every block, written at mask[4 * block].
void frame_mask_ge(const frame_column &f, u64 t, u64 *mask) {
    for (const auto &g : f.groups) {
        const u8 *p = g.bytes.data();
        const std::size_t m = g.ids.size();
        const u64 top = g.width >= 64 ? ~u64{0} : (u64{1} << g.width) - 1;
        const bool st = !g.starts.empty();
        const auto body = [&](std::size_t i, u64 c, u64 *out) {
            const u64 *s = st ? g.starts.data() + 4 * i : nullptr;
            switch (g.k) {
            case constant: break;
            case runs8: st ? runs<u8>::mask_ge_starts(p + g.offset[i], g.n[i], s, c, out) : runs<u8>::mask_ge(p + g.offset[i], g.n[i], c, out); break;
            case runs16: st ? runs<u16>::mask_ge_starts(p + g.offset[i], g.n[i], s, c, out) : runs<u16>::mask_ge(p + g.offset[i], g.n[i], c, out); break;
            case runs32: st ? runs<u32>::mask_ge_starts(p + g.offset[i], g.n[i], s, c, out) : runs<u32>::mask_ge(p + g.offset[i], g.n[i], c, out); break;
            case runs64: st ? runs<u64>::mask_ge_starts(p + g.offset[i], g.n[i], s, c, out) : runs<u64>::mask_ge(p + g.offset[i], g.n[i], c, out); break;
            case plane: with_width(g.width, [&](auto K) { P<K>::mask_ge(p + i * 32 * K, c, out); }); break;
            case wide: lanes<u64>::mask_ge(p + i * 2048, c, out); break;
            }
        };
        for (std::size_t i = 0; i < m; ++i) {
            u64 *out = mask + 4 * g.ids[i];
            const u64 base = g.base[i];
            if (t <= base || t - base > top) {
                const u64 all = t <= base ? ~u64{0} : 0;
                out[0] = out[1] = out[2] = out[3] = all;
                continue;
            }
            body(i, t - base, out);
        }
    }
}

// SUM over the rows selected by sel (four words per block); empty blocks skip.
u128 frame_masked_sum(const frame_column &f, const u64 *sel) {
    u128 total = 0;
    for (const auto &g : f.groups) {
        const u8 *p = g.bytes.data();
        const std::size_t m = g.ids.size();
        for (std::size_t i = 0; i < m; ++i) {
            const u64 *s = sel + 4 * g.ids[i];
            const u64 any = s[0] | s[1] | s[2] | s[3];
            if (!any)
                continue;
            const unsigned rows = static_cast<unsigned>(std::popcount(s[0]) + std::popcount(s[1]) + std::popcount(s[2]) +
                                                        std::popcount(s[3]));
            u128 r = 0;
            switch (g.k) {
            case constant: break;
            case runs8: r = runs<u8>::masked_sum(p + g.offset[i], g.n[i], s); break;
            case runs16: r = runs<u16>::masked_sum(p + g.offset[i], g.n[i], s); break;
            case runs32: r = runs<u32>::masked_sum(p + g.offset[i], g.n[i], s); break;
            case runs64: r = runs<u64>::masked_sum(p + g.offset[i], g.n[i], s); break;
            case plane: with_width(g.width, [&](auto K) { r = P<K>::masked_sum(p + i * 32 * K, s); }); break;
            case wide: total += exact_sum64(p + i * 2048, s); break;
            }
            total += static_cast<u128>(g.base[i]) * rows + r;
        }
    }
    return total;
}

template <std::size_t... I> constexpr std::array<u64 (*)(const u8 *, unsigned), 33> point_table(std::index_sequence<I...>) {
    return {nullptr, P<I + 1>::point...};
}
constexpr auto plane_points = point_table(std::make_index_sequence<32>{});

u64 frame_point(const frame_column &f, std::size_t row) {
    const auto [gi, i] = f.where[row / block_rows];
    const group &g = f.groups[gi];
    const unsigned r = row % block_rows;
    const u8 *p = g.bytes.data();
    u64 x = 0;
    const u64 *s = g.starts.empty() ? nullptr : g.starts.data() + 4 * i;
    switch (g.k) {
    case constant: break;
    case runs8: x = s ? runs<u8>::point_starts(p + g.offset[i], g.n[i], s, r) : runs<u8>::point(p + g.offset[i], g.n[i], r); break;
    case runs16: x = s ? runs<u16>::point_starts(p + g.offset[i], g.n[i], s, r) : runs<u16>::point(p + g.offset[i], g.n[i], r); break;
    case runs32: x = s ? runs<u32>::point_starts(p + g.offset[i], g.n[i], s, r) : runs<u32>::point(p + g.offset[i], g.n[i], r); break;
    case runs64: x = s ? runs<u64>::point_starts(p + g.offset[i], g.n[i], s, r) : runs<u64>::point(p + g.offset[i], g.n[i], r); break;
    case plane: x = plane_points[g.width](p + i * 32 * g.width, r); break;
    case wide: x = load_le<u64>(p + i * 2048 + 8 * r); break;
    }
    return g.base[i] + x;
}

// Native global FOR in the narrowest byte lane, through the lane leaves.
struct native_column {
    u64 min = 0;
    unsigned lane = 1;
    std::vector<u8> bytes;
    std::size_t blocks = 0;
    explicit native_column(const std::vector<u64> &v, bool logical) : blocks(v.size() / block_rows) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        min = logical ? 0 : *lo;
        const u64 span = *hi - min;
        lane = logical ? 8 : span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        bytes.resize(v.size() * lane + 256);
        for (std::size_t i = 0; i < v.size(); ++i) {
            const u64 r = v[i] - min;
            std::memcpy(bytes.data() + i * lane, &r, lane);
        }
    }
    template <class F> decltype(auto) with(F &&f) const {
        switch (lane) {
        case 1: return f(lanes<u8>{});
        case 2: return f(lanes<u16>{});
        case 4: return f(lanes<u32>{});
        default: return f(lanes<u64>{});
        }
    }
    u128 sum() const {
        return with([&](auto l) {
            using L = decltype(l);
            u128 s = 0;
            for (std::size_t b = 0; b < blocks; ++b)
                s += lane == 8 ? exact_sum64(bytes.data() + b * 2048) : u128{L::sum(bytes.data() + b * 256 * lane)};
            return s + static_cast<u128>(min) * blocks * block_rows;
        });
    }
    u64 count_ge(u64 t) const {
        if (t <= min)
            return blocks * block_rows;
        const u64 c = t - min;
        return with([&](auto l) {
            using L = decltype(l);
            u64 n = 0;
            if (lane < 8 && c >> (8 * lane))
                return n;
            for (std::size_t b = 0; b < blocks; ++b)
                n += L::count_ge(bytes.data() + b * 256 * lane, c);
            return n;
        });
    }
    void mask_ge(u64 t, u64 *mask) const {
        const u64 c = t <= min ? 0 : t - min;
        with([&](auto l) {
            using L = decltype(l);
            if (lane < 8 && c >> (8 * lane)) {
                std::memset(mask, 0, 32 * blocks);
                return 0;
            }
            for (std::size_t b = 0; b < blocks; ++b)
                L::mask_ge(bytes.data() + b * 256 * lane, c, mask + 4 * b);
            return 0;
        });
    }
    u128 masked_sum(const u64 *sel) const {
        return with([&](auto l) {
            using L = decltype(l);
            u128 s = 0;
            u64 rows = 0;
            for (std::size_t b = 0; b < blocks; ++b) {
                const u64 *w = sel + 4 * b;
                if (!(w[0] | w[1] | w[2] | w[3]))
                    continue;
                rows += static_cast<u64>(std::popcount(w[0]) + std::popcount(w[1]) + std::popcount(w[2]) + std::popcount(w[3]));
                s += lane == 8 ? exact_sum64(bytes.data() + b * 2048, w) : u128{L::masked_sum(bytes.data() + b * 256 * lane, w)};
            }
            return s + static_cast<u128>(min) * rows;
        });
    }
    u64 point(std::size_t row) const {
        u64 x = 0;
        std::memcpy(&x, bytes.data() + row * lane, lane);
        return min + x;
    }
};

struct timing {
    std::string arm;
    std::function<u64()> run;
};

void report(const std::string &head, std::vector<timing> &arms, unsigned rounds, double units) {
    u64 sink = 0;
    unsigned reps = 1;
    for (;;) {
        const auto t0 = clk::now();
        for (unsigned r = 0; r < reps; ++r)
            sink += arms[0].run();
        if (std::chrono::duration<double, std::nano>(clk::now() - t0).count() > 2e6 || reps >= (1u << 20))
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
        std::printf("{\"isa\":\"%s\",%s,\"arm\":\"%s\",\"ns\":%.4f,\"min\":%.4f,\"max\":%.4f}\n", isa_name(), head.c_str(),
                    arms[i].arm.c_str(), v[v.size() / 2], v.front(), v.back());
    }
    std::fflush(stdout);
    if (sink == 0x5eed)
        std::puts("");
}

[[noreturn]] void mismatch(const char *what, const std::string &column) {
    std::fprintf(stderr, "MISMATCH %s %s\n", what, column.c_str());
    std::exit(1);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS POLICY COLUMN[:PREDICATE]...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    const unsigned rounds = static_cast<unsigned>(std::atoi(argv[3]));
    const std::string policy = argv[4];
    std::mt19937_64 rng(11);
    for (int a = 5; a < argc; ++a) {
        std::string spec = argv[a], pred;
        if (const auto colon = spec.find(':'); colon != std::string::npos)
            pred = spec.substr(colon + 1), spec = spec.substr(0, colon);
        const auto v = load(dir + "/" + spec + ".u64", rows);
        const std::size_t blocks = v.size() / block_rows;
        const frame_column f = encode(v, policy);
        const native_column nat(v, false), logical(v, true);
        // Census and oracles.
        std::size_t census[7] = {}, runs_total = 0, runs_blocks = 0;
        for (const auto &g : f.groups) {
            census[g.k] += g.ids.size();
            for (u16 n : g.n)
                runs_total += n, ++runs_blocks;
        }
        std::vector<u64> sorted(v.begin(), v.begin() + std::min<std::size_t>(v.size(), 1 << 20));
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const u64 t = sorted[sorted.size() / 2];
        u128 want_sum = 0;
        u64 want_count = 0;
        for (u64 x : v)
            want_sum += x, want_count += x >= t;
        char head[512];
        std::snprintf(head, sizeof head,
                      "\"column\":\"%s\",\"policy\":\"%s\",\"rows\":%zu,\"frame_bytes\":%zu,\"native_bytes\":%zu,"
                      "\"constant\":%zu,\"runs\":%zu,\"packed\":%zu,\"wide\":%zu,\"mean_runs\":%.1f",
                      spec.c_str(), policy.c_str(), v.size(), f.image_bytes, nat.bytes.size() - 256, census[constant],
                      census[runs8] + census[runs16] + census[runs32] + census[runs64], census[plane], census[wide],
                      runs_blocks ? double(runs_total) / runs_blocks : 0.0);
        if (frame_sum(f) != want_sum || nat.sum() != want_sum)
            mismatch("sum", spec);
        if (frame_count_ge(f, t) != want_count || nat.count_ge(t) != want_count || logical.count_ge(t) != want_count)
            mismatch("count_ge", spec);
        std::vector<u64> m_frame(4 * blocks), m_native(4 * blocks);
        frame_mask_ge(f, t, m_frame.data());
        nat.mask_ge(t, m_native.data());
        if (m_frame != m_native)
            mismatch("mask_ge", spec);
        std::vector<u32> points(std::min<std::size_t>(v.size(), 1 << 16));
        for (auto &p : points)
            p = static_cast<u32>(rng() % v.size());
        u64 want_points = 0;
        for (u32 p : points)
            want_points += v[p];

        const auto row = [&](const char *op, const char *variant) {
            return std::string(head) + ",\"op\":\"" + op + "\",\"variant\":\"" + variant + "\"";
        };
        {
            std::vector<timing> arms = {{"frame", [&] { return static_cast<u64>(frame_sum(f)); }},
                                        {"native", [&] { return static_cast<u64>(nat.sum()); }},
                                        {"u64", [&] { return static_cast<u64>(logical.sum()); }}};
            report(row("sum", "all"), arms, rounds, double(blocks));
        }
        {
            std::vector<timing> arms = {{"frame", [&] { return frame_count_ge(f, t); }},
                                        {"native", [&] { return nat.count_ge(t); }},
                                        {"u64", [&] { return logical.count_ge(t); }}};
            report(row("count_ge", "median"), arms, rounds, double(blocks));
        }
        {
            std::vector<timing> arms = {{"frame", [&] { frame_mask_ge(f, t, m_frame.data()); return m_frame[7]; }},
                                        {"native", [&] { nat.mask_ge(t, m_native.data()); return m_native[7]; }},
                                        {"u64", [&] { logical.mask_ge(t, m_native.data()); return m_native[7]; }}};
            report(row("mask_ge", "median"), arms, rounds, double(blocks));
        }
        {
            std::vector<timing> arms = {{"frame", [&] { u64 s = 0; for (u32 p : points) s += frame_point(f, p); return s; }},
                                        {"native", [&] { u64 s = 0; for (u32 p : points) s += nat.point(p); return s; }},
                                        {"u64", [&] { u64 s = 0; for (u32 p : points) s += logical.point(p); return s; }}};
            if (arms[0].run() != want_points || arms[1].run() != want_points)
                mismatch("point", spec);
            report(row("point", "uniform"), arms, rounds, double(points.size()));
        }
        if (!pred.empty()) {
            // SUM(this) WHERE pred >= its median: the predicate's masks are
            // computed once (as native), then each arm sums its selected rows.
            const auto pv = load(dir + "/" + pred + ".u64", rows);
            std::vector<u64> ps(pv.begin(), pv.begin() + std::min<std::size_t>(pv.size(), 1 << 20));
            std::nth_element(ps.begin(), ps.begin() + ps.size() / 2, ps.end());
            const u64 pt = ps[ps.size() / 2];
            std::vector<u64> sel(4 * blocks);
            u128 want = 0;
            for (std::size_t i = 0; i < v.size(); ++i)
                if (pv[i] >= pt) {
                    sel[i / 64] |= u64{1} << (i % 64);
                    want += v[i];
                }
            if (frame_masked_sum(f, sel.data()) != want || nat.masked_sum(sel.data()) != want)
                mismatch("masked_sum", spec);
            std::vector<timing> arms = {{"frame", [&] { return static_cast<u64>(frame_masked_sum(f, sel.data())); }},
                                        {"native", [&] { return static_cast<u64>(nat.masked_sum(sel.data())); }},
                                        {"u64", [&] { return static_cast<u64>(logical.masked_sum(sel.data())); }}};
            report(row("masked_sum", (pred + ">=median").c_str()), arms, rounds, double(blocks));
        }
    }
}
