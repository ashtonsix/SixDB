// Times each masked SUM/summary kernel over 256 decoded blocks (65,536 random
// u64 rows) under each selection density, after checking every kernel against
// a plain oracle. Prints ns per row, median of 11 samples of 64 passes.
// argv: ARCH label.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using u64 = std::uint64_t;
using u128 = unsigned __int128;
struct summ {
    u128 sum;
    u64 count, min, max;
};
extern "C" {
u128 sum_bitwalk(const u64 *, const u64 *, unsigned);
u128 sum_rejected_and(const u64 *, const u64 *, unsigned);
u128 sum_fixed_and(const u64 *, const u64 *, unsigned);
u128 sum_expand(const u64 *, const u64 *, unsigned);
u128 sum_dense(const u64 *, const u64 *, unsigned);
u128 sum_adaptive(const u64 *, const u64 *, unsigned);
void summ_adaptive(const u64 *, const u64 *, unsigned, summ *);
void summ_bitwalk(const u64 *, const u64 *, unsigned, summ *);
void summ_rejected(const u64 *, const u64 *, unsigned, summ *);
void summ_identity(const u64 *, const u64 *, unsigned, summ *);
void summ_dense(const u64 *, const u64 *, unsigned, summ *);
}
volatile u64 sink;

int main(int argc, char **argv) {
    const char *arch = argc > 1 ? argv[1] : "local";
    constexpr unsigned blocks = 256, n = 256;
    std::mt19937_64 rng(3);
    std::vector<u64> x(blocks * n);
    for (auto &v : x)
        v = rng();
    struct density {
        const char *name;
        bool (*keep)(unsigned row, std::mt19937_64 &);
    };
    const density densities[]{
        {"all", [](unsigned, std::mt19937_64 &) { return true; }},
        {"none", [](unsigned, std::mt19937_64 &) { return false; }},
        {"period3", [](unsigned r, std::mt19937_64 &) { return r % 3 == 1; }},
        {"three_in_eight", [](unsigned r, std::mt19937_64 &) { return r % 8 < 3; }},
        {"five_in_eight", [](unsigned r, std::mt19937_64 &) { return r % 8 < 5; }},
        {"seven_in_eight", [](unsigned r, std::mt19937_64 &) { return r % 8 < 7; }},
        {"half_clustered", [](unsigned r, std::mt19937_64 &) { return r % 256 < 128; }},
        {"random_3_in_4", [](unsigned, std::mt19937_64 &g) { return g() % 4 != 0; }},
        {"sparse_1_in_32", [](unsigned, std::mt19937_64 &g) { return g() % 32 == 0; }},
    };
    struct sum_kernel {
        const char *name;
        u128 (*f)(const u64 *, const u64 *, unsigned);
        bool masked;
    };
    const sum_kernel sums[]{{"sum_bitwalk", sum_bitwalk, true},       {"sum_rejected_and", sum_rejected_and, true},
                            {"sum_fixed_and", sum_fixed_and, true},   {"sum_expand", sum_expand, true},
                            {"sum_adaptive", sum_adaptive, true},
                            {"sum_dense_unmasked", sum_dense, false}};
    struct summ_kernel {
        const char *name;
        void (*f)(const u64 *, const u64 *, unsigned, summ *);
        bool masked;
    };
    const summ_kernel summs[]{{"summary_bitwalk", summ_bitwalk, true},
                              {"summary_rejected", summ_rejected, true},
                              {"summary_identity", summ_identity, true},
                              {"summary_adaptive", summ_adaptive, true},
                              {"summary_dense_unmasked", summ_dense, false}};
    const auto time = [&](auto &&pass) {
        std::vector<double> ns;
        for (int s = 0; s < 11; ++s) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < 64; ++r)
                pass();
            ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() /
                         (64.0 * blocks * n));
        }
        std::sort(ns.begin(), ns.end());
        return ns[ns.size() / 2];
    };
    summ whole{0, blocks * n, ~u64{0}, 0};
    for (u64 v : x)
        whole.sum += v, whole.min = std::min(whole.min, v), whole.max = std::max(whole.max, v);
    for (const auto &d : densities) {
        std::vector<u64> words(blocks * 4);
        for (unsigned i = 0; i < blocks * n; ++i)
            if (d.keep(i, rng))
                words[i / 64] |= u64{1} << (i % 64);
        u128 want = 0;
        summ ws{0, 0, ~u64{0}, 0};
        for (unsigned i = 0; i < blocks * n; ++i)
            if (words[i / 64] >> (i % 64) & 1)
                want += x[i], ++ws.count, ws.min = std::min(ws.min, x[i]), ws.max = std::max(ws.max, x[i]);
        ws.sum = want;
        for (const auto &k : sums) {
            u128 got = 0;
            for (unsigned b = 0; b < blocks; ++b)
                got += k.f(&x[b * n], &words[b * 4], n);
            if (got != (k.masked ? want : whole.sum)) {
                std::fprintf(stderr, "%s wrong under %s\n", k.name, d.name);
                return 1;
            }
            const double ns = time([&] {
                u128 s = 0;
                for (unsigned b = 0; b < blocks; ++b)
                    s += k.f(&x[b * n], &words[b * 4], n);
                sink = u64(s) ^ u64(s >> 64);
            });
            std::printf("{\"arch\":\"%s\",\"kernel\":\"%s\",\"density\":\"%s\",\"ns_per_row\":%.4f}\n", arch, k.name,
                        d.name, ns);
        }
        for (const auto &k : summs) {
            summ got{0, 0, ~u64{0}, 0};
            for (unsigned b = 0; b < blocks; ++b)
                k.f(&x[b * n], &words[b * 4], n, &got);
            const auto &expected = k.masked ? ws : whole;
            if (got.sum != expected.sum || got.count != expected.count || got.min != expected.min || got.max != expected.max) {
                std::fprintf(stderr, "%s wrong under %s\n", k.name, d.name);
                return 1;
            }
            const double ns = time([&] {
                summ s{0, 0, ~u64{0}, 0};
                for (unsigned b = 0; b < blocks; ++b)
                    k.f(&x[b * n], &words[b * 4], n, &s);
                sink = u64(s.sum) ^ u64(s.sum >> 64) ^ s.count ^ s.min ^ s.max;
            });
            std::printf("{\"arch\":\"%s\",\"kernel\":\"%s\",\"density\":\"%s\",\"ns_per_row\":%.4f}\n", arch, k.name,
                        d.name, ns);
        }
    }
}
