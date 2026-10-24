// Direct-array boundary search: candidates against today's direct_source.
//   intervals_search_bench check            exhaustive answers + guard pages
//   intervals_search_bench run [trials]     JSON lines, one per cell and arm
// Cells: element width, count n, fixed or varying per-block counts, and three
// request streams: dependent chain (latency), independent random, independent
// sorted (predictable). Blocks are resident (~256 KiB); queries are uniform
// over each block's value domain.
#include "candidates.h"
#include "control/search_before.h"
#include <sys/mman.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace probe;
using u64 = std::uint64_t;
constexpr unsigned tail_grant = 512; // readable bytes after every benchmark block

#define ARM(NAME, TAIL, ...)                                                                        \
    struct NAME {                                                                                  \
        static constexpr const char *name = #NAME;                                                 \
        static constexpr unsigned tail = TAIL;                                                     \
        template <edge E, class T>                                                                 \
        [[gnu::always_inline]] static unsigned run(const T *p, unsigned n, T q) {                  \
            return __VA_ARGS__;                                                                    \
        }                                                                                          \
    };

ARM(current, 0,
    E == edge::at_or_after ? iv::lower_bound(iv::direct_source<T>{p, n}, q)
                           : iv::upper_bound(iv::direct_source<T>{p, n}, q))
ARM(before, 0, (ikea::intervals::frozen::hybrid_array<E, true>(p, n, q)))
ARM(std_branchy, 0, branchy<E>(p, n, q))
ARM(bisect, 0, branchless<E>(p, n, q))
// Native span W per ISA.
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
constexpr unsigned NW = 64;
#elif defined(__AVX2__)
constexpr unsigned NW = 32;
#else
constexpr unsigned NW = 16;
#endif
ARM(scan_scalar, 0, (scan<E, NW, small::scalar>(p, n, q)))
ARM(scan_halves, 0, (scan<E, NW, small::halves>(p, n, q)))
ARM(scan_page, 0, (scan<E, NW, small::page>(p, n, q)))
ARM(scan_tail, NW, (scan<E, NW, small::tail>(p, n, q)))
ARM(scan16_page, 0, (scan<E, 16, small::page>(p, n, q)))
ARM(scan32_page, 0, (scan<E, 32, small::page>(p, n, q)))
ARM(hyb16_page, 0, (hybrid<E, 16, 16, small::page>(p, n, q)))
ARM(hyb32_page, 0, (hybrid<E, 32, NW, small::page>(p, n, q)))
ARM(hyb64_page, 0, (hybrid<E, 64, NW, small::page>(p, n, q)))
ARM(hyb128_page, 0, (hybrid<E, 128, NW, small::page>(p, n, q)))
ARM(hyb256_page, 0, (hybrid<E, 256, NW, small::page>(p, n, q)))
ARM(hyb512_page, 0, (hybrid<E, 512, NW, small::page>(p, n, q)))
ARM(lerp_hyb128_page, 0, (lerp_hybrid<E, 128, NW, small::page>(p, n, q)))
ARM(lerp_hyb256_page, 0, (lerp_hybrid<E, 256, NW, small::page>(p, n, q)))
ARM(hyb64_halves, 0, (hybrid<E, 64, NW, small::halves>(p, n, q)))
ARM(hyb128_halves, 0, (hybrid<E, 128, NW, small::halves>(p, n, q)))
ARM(hyb64_scalar, 0, (hybrid<E, 64, NW, small::scalar>(p, n, q)))
ARM(hyb32_clamp, 0, (hybrid<E, 32, std::min(32u, NW), small::page, fit::clamp>(p, n, q)))
ARM(hyb64_clamp, 0, (hybrid<E, 64, NW, small::page, fit::clamp>(p, n, q)))
ARM(hyb128_clamp, 0, (hybrid<E, 128, NW, small::page, fit::clamp>(p, n, q)))
ARM(hyb256_clamp, 0, (hybrid<E, 256, NW, small::page, fit::clamp>(p, n, q)))
ARM(hyb64_clamp16, 0, (hybrid<E, 64, 16, small::page, fit::clamp>(p, n, q)))
ARM(hyb64_clamp32, 0, (hybrid<E, 64, 32, small::page, fit::clamp>(p, n, q)))
ARM(hyb64_branchy, 0, (branchy_hybrid<E, 64, NW>(p, n, q)))
ARM(hyb128_branchy, 0, (branchy_hybrid<E, 128, NW>(p, n, q)))
ARM(proposed_exact, 0, (proposed<E, 0>(p, n, q)))
ARM(proposed_pred, 0, (proposed<E, 0, tiny::predicated>(p, n, q)))
ARM(proposed_halo, 32, (proposed<E, 32>(p, n, q)))
ARM(lerp_hyb64_clamp, 0, (lerp_hybrid<E, 64, NW, small::page>(p, n, q)))
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
ARM(scan_masked, 0, (scan<E, 64, small::masked>(p, n, q)))
ARM(scan32_masked, 0, (scan<E, 32, small::masked>(p, n, q)))
ARM(hyb128_masked, 0, (hybrid<E, 128, 64, small::masked>(p, n, q)))
ARM(hyb256_masked, 0, (hybrid<E, 256, 64, small::masked>(p, n, q)))
ARM(hyb128_fmasked, 0, (hybrid<E, 128, 64, small::masked, fit::masked>(p, n, q)))
ARM(hyb256_fmasked, 0, (hybrid<E, 256, 64, small::masked, fit::masked>(p, n, q)))
ARM(hyb64_32masked, 0, (hybrid<E, 64, 32, small::masked>(p, n, q)))
ARM(hyb128_32masked, 0, (hybrid<E, 128, 32, small::masked>(p, n, q)))
#define MASKED_ARMS , scan_masked, scan32_masked, hyb128_masked, hyb256_masked, hyb64_32masked, hyb128_32masked, hyb128_fmasked, hyb256_fmasked
#else
#define MASKED_ARMS
#endif

template <class... A> struct arm_list {};
using arms = arm_list<current, before, std_branchy, bisect, scan_scalar, scan_halves, scan_page, scan_tail,
                      scan16_page, scan32_page, hyb16_page, hyb32_page, hyb64_page, hyb128_page,
                      hyb256_page, hyb512_page, lerp_hyb128_page, lerp_hyb256_page, hyb64_halves, hyb128_halves, hyb64_scalar,
                      hyb32_clamp, hyb64_clamp, hyb128_clamp, hyb256_clamp, hyb64_clamp16,
                      hyb64_clamp32, hyb64_branchy, hyb128_branchy, lerp_hyb64_clamp, proposed_exact,
                      proposed_pred, proposed_halo MASKED_ARMS>;

[[noreturn]] void fail(const char *what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
}

// ---------------------------------------------------------------- check
template <class T> std::vector<T> probes(const T *p, unsigned n) {
    std::vector<T> qs{T(0), T(~T(0)), T(1), T(~T(0) - 1)};
    for (unsigned i = 0; i < n; ++i) {
        const T v = element(p, i);
        qs.push_back(v);
        qs.push_back(T(v + 1));
        qs.push_back(T(v - 1));
    }
    return qs;
}
// Arrays placed flush against inaccessible pages on both sides. Arms that
// need a readable tail are checked with one instead.
struct guarded {
    std::byte *base;
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    guarded() {
        void *m = mmap(nullptr, 4 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED)
            fail("mmap");
        base = static_cast<std::byte *>(m);
        mprotect(base, page, PROT_NONE);
        mprotect(base + 3 * page, page, PROT_NONE);
    }
};

template <class T, class Arm, edge E>
void check_one(const T *p, unsigned n, const std::vector<T> &qs) {
    if (std::getenv("SEARCH_TRACE"))
        std::fprintf(stderr, "%s u%zu n %u at %p\n", Arm::name, 8 * sizeof(T), n, (const void *)p);
    for (const T q : qs) {
        const unsigned want = branchy<E>(p, n, q);
        const unsigned got = Arm::template run<E>(p, n, q);
        if (got != want) {
            std::fprintf(stderr, "%s size %zu n %u q %llu: got %u want %u\n", Arm::name, sizeof(T), n,
                         (unsigned long long)q, got, want);
            std::exit(1);
        }
    }
}
template <class T, class... A> void check_width(arm_list<A...>, guarded &g, std::mt19937_64 &rng) {
    std::vector<unsigned> sizes;
    for (unsigned n = 0; n <= 300; ++n)
        sizes.push_back(n);
    for (unsigned n : {383u, 384u, 385u, 511u, 512u, 513u, 1000u, 1023u, 1024u, 1025u})
        sizes.push_back(n);
    for (const unsigned n : sizes) {
        if (n * sizeof(T) > g.page)
            continue;
        std::vector<T> values(n);
        // Duplicates, near-top values and gaps; sorted.
        const T top = T(~T(0));
        for (auto &v : values) {
            const auto r = rng() % 8;
            v = r == 0 ? top : r == 1 ? T(top - rng() % 4) : T(rng() % (std::max<u64>(4, 3ull * n)));
        }
        std::sort(values.begin(), values.end());
        for (const int placement : {0, 1, 2}) {
            // 0: ends at the guard page; 1: starts at the guard page; 2: tail grant.
            std::byte *at = placement == 0 ? g.base + 3 * g.page - n * sizeof(T)
                          : placement == 1 ? g.base + g.page
                                           : g.base + g.page + 64 + sizeof(T);
            std::memcpy(at, values.data(), n * sizeof(T));
            const T *p = reinterpret_cast<const T *>(at);
            const auto qs = probes(p, n);
            ([&] {
                if (placement == 2 || A::tail == 0) {
                    check_one<T, A, edge::at_or_after>(p, n, qs);
                    check_one<T, A, edge::after>(p, n, qs);
                }
            }(), ...);
        }
    }
    // Unaligned placements of u16..u64 elements.
    if constexpr (sizeof(T) > 1)
        for (unsigned n : {1u, 3u, 7u, 9u, 33u, 77u, 200u}) {
            std::vector<T> values(n);
            for (auto &v : values)
                v = T(rng());
            std::sort(values.begin(), values.end());
            std::byte *at = g.base + 3 * g.page - n * sizeof(T) - 1;
            std::memcpy(at, values.data(), n * sizeof(T));
            const T *p = reinterpret_cast<const T *>(at);
            const auto qs = probes(p, n);
            ([&] {
                if (A::tail == 0) {
                    check_one<T, A, edge::at_or_after>(p, n, qs);
                    check_one<T, A, edge::after>(p, n, qs);
                }
            }(), ...);
        }
}

// ---------------------------------------------------------------- bench
constexpr unsigned queries = 1u << 14;
template <class T> struct fixture {
    std::vector<std::byte> storage;
    std::vector<std::size_t> offset;
    std::vector<unsigned> count;
    std::vector<unsigned> qb;
    std::vector<T> qv;
    std::vector<unsigned> salt; // dependent stream: next index = salt[i] + answer
    const T *block(unsigned b) const { return reinterpret_cast<const T *>(storage.data() + offset[b]); }
    fixture(unsigned n, bool vary, int stream, std::mt19937_64 &rng) {
        const std::size_t slot = (std::size_t(n) * sizeof(T) + 63) / 64 * 64 + 64 + tail_grant;
        const unsigned blocks = unsigned(std::clamp<std::size_t>((256u << 10) / slot, 1, 4096));
        storage.resize(slot * blocks + 64);
        const T top = T(~T(0));
        const u64 domain = std::max<u64>(4ull * n, 2);
        const u64 stride = std::max<u64>(1, u64(top) / domain);
        std::vector<T> values;
        for (unsigned b = 0; b < blocks; ++b) {
            const unsigned c = vary ? 1 + unsigned(rng() % n) : n;
            const std::size_t at = b * slot + (rng() % 64) / sizeof(T) * sizeof(T);
            values.resize(c);
            for (auto &v : values)
                v = T(std::min<u64>(u64(top), (rng() % domain) * stride));
            std::sort(values.begin(), values.end());
            std::memcpy(storage.data() + at, values.data(), c * sizeof(T));
            // Tail bytes are arbitrary; fill to catch any arm that counts them.
            for (std::size_t i = at + c * sizeof(T); i < (b + 1) * slot; ++i)
                storage[i] = std::byte(rng());
            offset.push_back(at);
            count.push_back(c);
        }
        std::vector<std::pair<unsigned, T>> qs(queries);
        for (auto &[b, v] : qs) {
            b = unsigned(rng() % blocks);
            const u64 last = count[b] ? u64(element(block(b), count[b] - 1)) : 0;
            v = T(std::min<u64>(u64(top), rng() % (last + 2)));
        }
        if (stream == 2)
            std::sort(qs.begin(), qs.end());
        if (stream == 3) // runs of 256 ascending queries within one block
            for (unsigned i = 0; i < queries; i += 256) {
                const unsigned b = unsigned(rng() % blocks);
                for (unsigned k = 0; k < 256; ++k)
                    qs[i + k].first = b;
                const u64 last = count[b] ? u64(element(block(b), count[b] - 1)) : 0;
                for (unsigned k = 0; k < 256; ++k)
                    qs[i + k].second = T(std::min<u64>(u64(top), (last + 1) * k / 255));
            }
        for (auto [b, v] : qs) {
            qb.push_back(b);
            qv.push_back(v);
            salt.push_back(unsigned(rng()));
        }
    }
};
template <class A, class T> [[gnu::noinline]] u64 independent(const fixture<T> &f) {
    u64 s = 0;
    for (unsigned i = 0; i < queries; ++i) {
        const auto b = f.qb[i];
        s += A::template run<edge::at_or_after>(f.block(b), f.count[b], f.qv[i]);
    }
    return s;
}
template <class A, class T> [[gnu::noinline]] u64 dependent(const fixture<T> &f) {
    u64 s = 0;
    unsigned j = 0;
    for (unsigned i = 0; i < queries; ++i) {
        const auto b = f.qb[j];
        const unsigned r = A::template run<edge::at_or_after>(f.block(b), f.count[b], f.qv[j]);
        s += r;
        j = (f.salt[i] + r) & (queries - 1);
    }
    return s;
}
template <class T> std::pair<u64, u64> oracle(const fixture<T> &f) {
    u64 a = 0, d = 0;
    for (unsigned i = 0; i < queries; ++i)
        a += branchy<edge::at_or_after>(f.block(f.qb[i]), f.count[f.qb[i]], f.qv[i]);
    unsigned j = 0;
    for (unsigned i = 0; i < queries; ++i) {
        const auto b = f.qb[j];
        const unsigned r = branchy<edge::at_or_after>(f.block(b), f.count[b], f.qv[j]);
        d += r;
        j = (f.salt[i] + r) & (queries - 1);
    }
    return {a, d};
}
double now() {
    return std::chrono::duration<double, std::nano>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
const char *profile() {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    return "avx512";
#elif defined(__AVX2__)
    return "avx2";
#elif defined(__aarch64__)
    return "neon";
#else
    return "other";
#endif
}

template <class T, class... A>
void bench_width(arm_list<A...>, unsigned trials, std::mt19937_64 &rng) {
    std::vector<unsigned> sizes{1,  2,  3,  4,  6,  8,   12,  16,  24,  32,   34,   48,   64,
                                96, 128, 192, 256, 384, 512, 1024, 2048, 4096, 16384};
    // Large arrays (LLC/DRAM) run a few arms only, fixed counts, two streams.
    if (std::getenv("SEARCH_LARGE") && sizeof(T) >= 4)
        for (unsigned n : {1u << 18, 1u << 20, 1u << 22, 1u << 24})
            sizes.push_back(n);
    const char *large_arms[] = {"current", "std_branchy", "bisect", "proposed_exact",
                                "lerp_hyb64_clamp", "lerp_hyb128_page", "hyb64_branchy"};
    for (const unsigned n : sizes) {
        if (sizeof(T) == 1 && n > 256)
            continue;
        for (const bool vary : {false, true})
            for (const int stream : {0, 1, 2, 3}) { // dependent, random, sorted, runs
                if ((vary && n == 1) || (n > 16384 && (vary || stream >= 2)))
                    continue;
                const fixture<T> f(n, vary, stream, rng);
                const auto [want_a, want_d] = oracle(f);
                constexpr unsigned arms = sizeof...(A);
                using fn = u64 (*)(const fixture<T> &);
                const fn run[] = {(stream == 0 ? &dependent<A, T> : &independent<A, T>)...};
                const char *names[] = {A::name...};
                const bool scan_like[] = {
                    (std::string(A::name).starts_with("scan") ||
                     (n > 16384 && std::find_if(std::begin(large_arms), std::end(large_arms),
                                                [](const char *x) { return std::string(x) == A::name; }) ==
                                       std::end(large_arms)))...};
                const u64 want = stream == 0 ? want_d : want_a;
                std::vector<std::vector<double>> samples(arms);
                for (unsigned a = 0; a < arms; ++a)
                    if (!(scan_like[a] && (n * sizeof(T) > 4096 || n > 16384)) && run[a](f) != want) {
                        std::fprintf(stderr, "%s n %u: wrong answer\n", names[a], n);
                        std::exit(1);
                    }
                const unsigned reps = n * sizeof(T) >= 4096 ? 1 : 2;
                for (unsigned t = 0; t < trials; ++t)
                    for (unsigned k = 0; k < arms; ++k) {
                        const unsigned a = (k + t) % arms;
                        if (scan_like[a] && (n * sizeof(T) > 4096 || n > 16384))
                            continue;
                        const double t0 = now();
                        u64 sink = 0;
                        for (unsigned r = 0; r < reps; ++r)
                            sink += run[a](f);
                        const double t1 = now();
                        if (sink != want * reps)
                            fail("timed answer");
                        samples[a].push_back((t1 - t0) / (double(queries) * reps));
                    }
                for (unsigned a = 0; a < arms; ++a) {
                    auto &s = samples[a];
                    if (s.empty())
                        continue;
                    std::sort(s.begin(), s.end());
                    std::printf("{\"profile\":\"%s\",\"bytes\":%zu,\"n\":%u,\"counts\":\"%s\","
                                "\"stream\":\"%s\",\"arm\":\"%s\",\"min\":%.3f,\"median\":%.3f,"
                                "\"max\":%.3f,\"samples\":%zu}\n",
                                profile(), sizeof(T), n, vary ? "vary" : "fixed",
                                stream == 0 ? "dependent" : stream == 1 ? "random" : stream == 2 ? "sorted" : "runs",
                                names[a], s.front(), s[s.size() / 2], s.back(), s.size());
                }
                std::fflush(stdout);
            }
    }
}
} // namespace

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "check";
    std::mt19937_64 rng(0x5eed1e5);
    if (mode == "check") {
        guarded g;
        check_width<std::uint8_t>(arms{}, g, rng);
        check_width<std::uint16_t>(arms{}, g, rng);
        check_width<std::uint32_t>(arms{}, g, rng);
        check_width<std::uint64_t>(arms{}, g, rng);
        std::printf("check passed (%s)\n", profile());
        return 0;
    }
    const unsigned trials = argc > 2 ? unsigned(std::atoi(argv[2])) : 9;
    bench_width<std::uint8_t>(arms{}, trials, rng);
    bench_width<std::uint16_t>(arms{}, trials, rng);
    bench_width<std::uint32_t>(arms{}, trials, rng);
    bench_width<std::uint64_t>(arms{}, trials, rng);
}
