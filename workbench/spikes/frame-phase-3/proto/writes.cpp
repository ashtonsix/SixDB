// Prototype 4: where should write-time outliers live? (change.h)
//
// For each column, policy and workload: apply the writes through
// mutable_column, then check every answer against a plain array, check
// that every issued store was declared before the write, that the SUM
// contributions add up, that block statistics stay safe, and that replaying
// the same writes on a second copy gives the same bytes. Timings: the writes
// themselves, then SUM, COUNT(x >= median) and points before and after
// (the read tax). Warm; single core.
//
// usage: frame3_writes COLS_DIR ROWS ROUNDS COLUMN...  (gen_* columns are generated)
#include "change.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <string>

using namespace frame3;

namespace {
std::vector<u64> load(const std::string &dir, const std::string &name, std::size_t rows) {
    if (name.starts_with("gen_")) {
        std::mt19937_64 rng(5);
        std::vector<u64> v(rows);
        u64 t = u64{1} << 40;
        for (auto &x : v)
            x = name == "gen_ts" ? (t += rng() % 1000) : name == "gen_narrow" ? 1000 + rng() % 200 : rng() % 50 ? 5000 + rng() % 300 : rng();
        return v;
    }
    std::ifstream f(dir + "/" + name + ".u64", std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8);
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

struct policy {
    std::string name;
    std::function<std::unique_ptr<stage>()> make;
};
std::vector<policy> policies() {
    using P = std::integral_constant<sp::preset, sp::preset::compact>;
    auto packed = [] { return std::make_unique<pack<P::value>>(group_blocks); };
    return {
        {"slack32", [=] { return std::make_unique<bound>(packed(), 0, 64, 32); }},
        {"buffer64", [=] { return std::make_unique<overrides>(std::make_unique<bound>(packed()), 64); }},
        {"reencode", [=] { return std::make_unique<bound>(packed()); }},
        {"reencode_part", [] { return std::make_unique<bound>(std::make_unique<pack<P::value>>()); }},
    };
}

struct write {
    std::size_t row;
    u64 value;
};
// hot: 16 rows rewritten; spread: one write per block; outliers: one
// out-of-window write per block, alternating below and above the column.
std::vector<write> workload(const std::string &kind, const std::vector<u64> &v, std::mt19937_64 &rng) {
    std::vector<write> w;
    const std::size_t n = v.size(), blocks = n / block_rows;
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    if (kind == "hot") {
        std::array<std::size_t, 16> rows;
        for (auto &r : rows)
            r = rng() % n;
        for (std::size_t i = 0; i < blocks; ++i)
            w.push_back({rows[i % 16], v[rng() % n]});
    } else
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t row = b * block_rows + rng() % block_rows;
            u64 value = v[rng() % n];
            if (kind == "outliers")
                value = b % 2 ? (*mx < ~u64{0} - 1000 ? *mx + 1 + rng() % 1000 : *mx) : (*mn > 1000 ? *mn - 1 - rng() % 1000 : 0);
            w.push_back({row, value});
        }
    return w;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    int failures = 0;
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v0 = load(dir, name, want);
        const std::size_t n = v0.size();
        std::vector<u64> sorted = v0;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        std::mt19937_64 prng(29);
        std::vector<u32> points(16384);
        for (auto &r : points)
            r = static_cast<u32>(prng() % n);
        for (const std::string kind : {"hot", "spread", "outliers"})
            for (const auto &pol : policies()) {
                std::mt19937_64 rng(17);
                const auto writes = workload(kind, v0, rng);
                std::vector<u64> v = v0;
                mutable_column c(pol.make(), v0), replay(pol.make(), v0);
                auto reads = [&](std::vector<double> &ts, std::vector<double> &tc, std::vector<double> &tp) {
                    u128 s = 0;
                    u64 k = 0, q = 0;
                    for (int r = 0; r < rounds; ++r) {
                        ts.push_back(time_ns([&] { s = c.read().sum(); }));
                        tc.push_back(time_ns([&] { k = c.read().count(comparison::greater_equal, t); }));
                        tp.push_back(time_ns([&] {
                            q = 0;
                            for (u32 row : points)
                                q += c.read().get(row);
                        }));
                    }
                    u128 so = 0;
                    u64 ko = 0, qo = 0;
                    for (u64 x : v)
                        so += x, ko += x >= t;
                    for (u32 row : points)
                        qo += v[row];
                    return s == so && k == ko && q == qo;
                };
                std::vector<double> s0, c0, p0, s1, c1, p1;
                bool ok = reads(s0, c0, p0);
                std::string why;
                auto note = [&](bool good, const char *what) {
                    if (!good && why.size() < 80)
                        why += what, why += ' ';
                    return good;
                };
                const std::size_t bytes0 = c.read().bytes();
                __int128 sum = 0;
                for (u64 x : v)
                    sum += x;
                std::size_t undeclared = 0, declared = 0, issued = 0, records = 0, loosened = 0;
                double write_ns = 0;
                for (const auto &w : writes) {
                    contribution out;
                    write_ns += time_ns([&] { out = c.set(w.row, w.value); });
                    replay.set(w.row, w.value);
                    sum += out.sum;
                    loosened += out.loosened;
                    v[w.row] = w.value;
                    if (!out.reencoded) {
                        const std::size_t u = c.undeclared_bytes();
                        undeclared += u;
                        declared += c.declared.stores.bytes();
                        for (const auto &e : c.effects.entries())
                            issued += e.size;
                        records += c.effects.used;
                    }
                }
                ok &= note(reads(s1, c1, p1), "reads");
                u128 so = 0;
                for (u64 x : v)
                    so += x;
                ok &= note(static_cast<u128>(sum) == so, "contribution");
                // Every row, and statistics that never under-cover a block.
                std::vector<u64> all(n);
                c.read().read(all.data());
                ok &= note(all == v, "rows");
                for (std::size_t b = 0; b * block_rows < n; ++b) {
                    const auto first = v.begin() + b * block_rows, last = v.begin() + std::min(n, (b + 1) * block_rows);
                    const auto [lo, hi] = std::minmax_element(first, last);
                    const auto &st = c.stats(b * block_rows);
                    ok &= note(st.min <= *lo && st.max >= *hi && (!st.exact || (st.min == *lo && st.max == *hi)), "stats");
                }
                const bool deterministic = c.read().digest() == replay.read().digest();
                ok &= deterministic && undeclared == 0;
                if (!ok)
                    ++failures, std::fprintf(stderr, "FAIL %s %s %s (undeclared %zu, deterministic %d) %s\n", name.c_str(), kind.c_str(), pol.name.c_str(),
                                             undeclared, deterministic, why.c_str());
                const std::size_t applied = writes.size() - c.reencoded_groups - c.reencoded_parts;
                std::printf("{\"column\":\"%s\",\"workload\":\"%s\",\"policy\":\"%s\",\"rows\":%zu,\"writes\":%zu,\"reencoded_groups\":%zu,"
                            "\"reencoded_parts\":%zu,\"ns_per_write\":%.1f,\"declared_store_bytes\":%.1f,\"issued_bytes\":%.1f,\"records\":%.2f,"
                            "\"loosened\":%zu,\"bits_before\":%.3f,\"bits_after\":%.3f,\"sum_before\":%.2f,\"sum_after\":%.2f,"
                            "\"count_before\":%.2f,\"count_after\":%.2f,\"point_before\":%.2f,\"point_after\":%.2f,\"ok\":%d}\n",
                            name.c_str(), kind.c_str(), pol.name.c_str(), n, writes.size(), c.reencoded_groups, c.reencoded_parts,
                            write_ns / static_cast<double>(writes.size()), applied ? static_cast<double>(declared) / static_cast<double>(applied) : 0.0,
                            applied ? static_cast<double>(issued) / static_cast<double>(applied) : 0.0,
                            applied ? static_cast<double>(records) / static_cast<double>(applied) : 0.0, loosened,
                            8.0 * static_cast<double>(bytes0) / static_cast<double>(n), 8.0 * static_cast<double>(c.read().bytes()) / static_cast<double>(n),
                            median(s0) / static_cast<double>(n / block_rows), median(s1) / static_cast<double>(n / block_rows),
                            median(c0) / static_cast<double>(n / block_rows), median(c1) / static_cast<double>(n / block_rows),
                            median(p0) / static_cast<double>(points.size()), median(p1) / static_cast<double>(points.size()), ok);
            }
    }
    return failures ? 1 : 0;
}
