// A second body family through the same seams: sorted part dictionaries.
// Each 65,536-row part maps its values to ranks in its sorted distinct
// values; the codes go through the unchanged shared image (descriptor
// columns over front C's bodies). COUNT(x >= t) translates t to a code
// threshold per part and calls the ordinary mask leaves; SUM needs the
// per-code values, so it uses front C's histogram leaves (codes of at most
// 8 bits, and runs) or unpacks codes and gathers. Every answer is checked.
//
// usage: frame_substrate_dict COLS_DIR ROWS ROUNDS COLUMN...
#include "image.h"
#include "sort.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fi = frame_image;
using fi::u128;
using fi::u32;
using fi::u64;
using fi::u8;

namespace {
constexpr std::size_t part_rows = 65536, part_blocks = part_rows / fi::block_rows;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / fi::block_rows * fi::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct coded {
    std::vector<std::vector<u64>> dict;  // per part, sorted distinct values
    fi::image codes;
    std::size_t dict_bytes() const {
        std::size_t n = 0;
        for (const auto &d : dict)
            n += 8 * d.size();
        return n;
    }
};

coded build(const std::vector<u64> &v) {
    coded c;
    std::vector<u64> codes(v.size());
    for (std::size_t first = 0; first < v.size(); first += part_rows) {
        const std::size_t limit = std::min(v.size(), first + part_rows);
        std::vector<u64> d(v.begin() + first, v.begin() + limit);
        std::sort(d.begin(), d.end());
        d.erase(std::unique(d.begin(), d.end()), d.end());
        for (std::size_t i = first; i < limit; ++i)
            codes[i] = std::lower_bound(d.begin(), d.end(), v[i]) - d.begin();
        c.dict.push_back(std::move(d));
    }
    c.codes = fi::build(codes, fi::placement::address);
    return c;
}

// SUM over one coded block: per-code counts times the part's values.
u128 coded_block_sum(const coded &c, std::size_t b) {
    const fi::image &f = c.codes;
    const u64 *d = c.dict[b / part_blocks].data();
    const u64 base = f.base[b];
    const u8 *p = f.bytes.data() + f.offset[b];
    const unsigned w = f.width[b];
    alignas(64) u32 counts[256];
    u128 s = 0;
    switch (f.kind_of[b]) {
    case fi::constant: return static_cast<u128>(d[base]) * fi::block_rows;
    case fi::runs:
        if (w <= 8) {
            bodies::runs_histogram<u8>::histogram(p, f.n[b], 1u << w, counts);
            for (unsigned v = 0; v < (1u << w); ++v)
                s += static_cast<u128>(counts[v]) * d[base + v];
            return s;
        }
        break;
    case fi::plane:
        if (w <= 8) {
            fi::with_plane(w, [&](auto K) {
                if constexpr (K <= 8)
                    bodies::packed_histogram<bodies::layout::plane256, K>::histogram(p, counts);
            });
            for (unsigned v = 0; v < (1u << w); ++v)
                s += static_cast<u128>(counts[v]) * d[base + v];
            return s;
        }
        break;
    default: break;
    }
    // Wider codes: unpack and gather.
    alignas(64) u32 x[fi::block_rows + 32];  // run unpack writes up to 32 rows past 256 (bodies/README.md, write envelopes)
    if (f.kind_of[b] == fi::runs)
        fi::with_runs(w, [&](auto r) { decltype(r)::unpack(p, f.n[b], x); });
    else if (f.kind_of[b] == fi::plane)
        fi::with_plane(w, [&](auto K) { fi::P<K>::unpack(p, x); });
    else
        for (unsigned i = 0; i < fi::block_rows; ++i) {
            u64 y;
            std::memcpy(&y, p + 8 * i, 8);
            x[i] = static_cast<u32>(y);
        }
    u64 lo = 0, hi = 0;
    for (unsigned i = 0; i < fi::block_rows; ++i) {
        const u64 y = d[base + x[i]];
        lo += y & 0xffffffffu, hi += y >> 32;
    }
    return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
}

u64 coded_count_ge(const coded &c, u64 t) {
    u64 n = 0;
    const std::size_t blocks = c.codes.blocks;
    for (std::size_t first = 0, part = 0; first < blocks; first += part_blocks, ++part) {
        const auto &d = c.dict[part];
        const u64 ct = std::lower_bound(d.begin(), d.end(), t) - d.begin();
        n += fi::count_ge(c.codes, ct, first, std::min(blocks, first + part_blocks));
    }
    return n;
}

struct arm {
    const char *op, *name;
    std::function<u128()> run;
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir + "/" + name + ".u64", want);
        const std::size_t n = v.size(), blocks = n / fi::block_rows;
        const coded c = build(v);
        const fi::image plain = fi::build(v, fi::placement::by_kind);
        const fi::kind_index index = fi::index_by_kind(plain);
        std::vector<u64> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        u128 sum_oracle = 0;
        u64 count_oracle = 0;
        for (u64 x : v)
            sum_oracle += x, count_oracle += x >= t;
        const arm arms[] = {
            {"sum", "dict", [&] {
                 u128 s = 0;
                 for (std::size_t b = 0; b < blocks; ++b)
                     s += coded_block_sum(c, b);
                 return s;
             }},
            {"sum", "integer", [&] { return fi::sum_indexed(plain, index); }},
            {"count_ge", "dict", [&] { return u128{coded_count_ge(c, t)}; }},
            {"count_ge", "integer", [&] { return u128{fi::count_ge(plain, t, 0, blocks)}; }},
        };
        constexpr std::size_t narms = sizeof arms / sizeof arms[0];
        const u128 expect[narms] = {sum_oracle, sum_oracle, count_oracle, count_oracle};
        for (std::size_t i = 0; i < narms; ++i)
            if (arms[i].run() != expect[i]) {
                std::fprintf(stderr, "%s %s %s: wrong answer\n", name.c_str(), arms[i].op, arms[i].name);
                return 1;
            }
        std::vector<std::vector<double>> samples(narms);
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < narms; ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                arms[i].run();
                bool ok = true;
                samples[i].push_back(time_ns([&] { ok = arms[i].run() == expect[i]; }));
                if (!ok)
                    return 1;
            }
        std::size_t distinct = 0;
        for (const auto &d : c.dict)
            distinct += d.size();
        for (std::size_t i = 0; i < narms; ++i)
            std::printf("{\"column\":\"%s\",\"rows\":%zu,\"op\":\"%s\",\"arm\":\"%s\",\"ns_per_block\":%.2f,"
                        "\"coded_bytes\":%zu,\"dict_bytes\":%zu,\"integer_bytes\":%zu,\"mean_part_distinct\":%.1f}\n",
                        name.c_str(), n, arms[i].op, arms[i].name, median(samples[i]) / blocks, c.codes.image_bytes(),
                        c.dict_bytes(), plain.image_bytes(), double(distinct) / c.dict.size());
    }
}
