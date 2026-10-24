// Ashton's multi-yield idea through author::scan_regions: one decode per
// native group feeds several consumers with values still in registers.
// Consumers: SUM, COUNT >= t, COUNT == t and MAX. Arms (ns per 256 rows, warm):
//   fused        one scan, all four consumers
//   separate     range SUM kernel + two one-pass counts + a MAX-only scan
//   max_scan     the MAX-only scan (what a consumer without a kernel pays)
//   native       the four consumers fused over the byte-lane array
#include <ikea/seriespack.h>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
namespace sp = ikea::seriespack;
namespace cp = sp::author;
using u64 = std::uint64_t;

struct stats {
    u64 sum = 0, ge = 0, eq = 0, max = 0;
    bool operator==(const stats&) const = default;
};
// Every timed arm returns all four results through one value, so none of the
// consumers can be removed from the timed work.
inline u64 digest(const stats& s) { return s.sum ^ (s.ge * 0x9e3779b97f4a7c15ULL) ^ (s.eq << 21) ^ (s.max << 43); }
// COUNT op t of one native group, kept in vector masks on NEON (a bitmask
// per group costs a movemask emulation); bit words elsewhere.
template <ikea::comparison Op, unsigned K, unsigned N> struct counter {
    u64 rhs, n = 0;
#if defined(__aarch64__) && !defined(YIELD_BITS)
    uint16x8_t acc = vdupq_n_u16(0);  // a lane gains at most 2 per group; flushed by finish only (bench sizes)
    void add(const sp::native_group::values<K, N>& x) {
        sp::detail::each<N / 16>([&](auto p) {
            if constexpr (sizeof(sp::uint_for<K>) == 1) {
                const auto t = vdupq_n_u8(static_cast<std::uint8_t>(rhs));
                const auto m = Op == ikea::comparison::equal ? vceqq_u8(x.block[p].v[0], t) : vcgeq_u8(x.block[p].v[0], t);
                acc = vpadalq_u8(acc, vshrq_n_u8(m, 7));
            } else
                sp::detail::each<2>([&](auto j) {
                    const auto v = vreinterpretq_u16_u8(x.block[p].v[j]);
                    const auto t = vdupq_n_u16(static_cast<std::uint16_t>(rhs));
                    acc = vsubq_u16(acc, Op == ikea::comparison::equal ? vceqq_u16(v, t) : vcgeq_u16(v, t));
                });
        });
    }
    u64 finish() const { return n + vaddlvq_u16(acc); }
#else
    void add(const sp::native_group::values<K, N>& x) { n += std::popcount(cp::filter_values<Op>(x, rhs)); }
    u64 finish() const { return n; }
#endif
};
// Lane maximum of one native group (K <= 16), folded into a running vector.
template <unsigned K, unsigned N> struct maxer {
#if defined(__aarch64__)
    uint8x16_t m8 = vdupq_n_u8(0);
    uint16x8_t m16 = vdupq_n_u16(0);
    void add(const sp::native_group::values<K, N>& x) {
        sp::detail::each<N / 16>([&](auto p) {
            if constexpr (sizeof(sp::uint_for<K>) == 1)
                m8 = vmaxq_u8(m8, x.block[p].v[0]);
            else
                sp::detail::each<2>([&](auto j) { m16 = vmaxq_u16(m16, vreinterpretq_u16_u8(x.block[p].v[j])); });
        });
    }
    u64 finish() const { return std::max<u64>(vmaxvq_u8(m8), vmaxvq_u16(m16)); }
#else
    __m512i m = _mm512_setzero_si512();
    void add(const sp::native_group::values<K, N>& x) {
        sp::detail::each<sp::native_group::values<K, N>::parts>([&](auto p) {
            if constexpr (sizeof(sp::uint_for<K>) == 1)
                m = _mm512_max_epu8(m, x.v[p]);
            else
                m = _mm512_max_epu16(m, x.v[p]);
        });
    }
    u64 finish() const {
        if constexpr (sizeof(sp::uint_for<K>) == 1)
            return _mm512_reduce_max_epu32(_mm512_max_epu32(_mm512_and_si512(m, _mm512_set1_epi32(255)),
                   _mm512_max_epu32(_mm512_and_si512(_mm512_srli_epi32(m, 8), _mm512_set1_epi32(255)),
                   _mm512_max_epu32(_mm512_and_si512(_mm512_srli_epi32(m, 16), _mm512_set1_epi32(255)), _mm512_srli_epi32(m, 24)))));
        else
            return _mm512_reduce_max_epu32(_mm512_max_epu32(_mm512_and_si512(m, _mm512_set1_epi32(0xffff)), _mm512_srli_epi32(m, 16)));
    }
#endif
};

template <unsigned K, sp::preset P> void run(std::size_t n, const char* name) {
    using L = sp::preset_layout<K, P>;
    std::mt19937_64 rng(K);
    std::vector<std::uint32_t> v(n);
    for (auto& x : v) x = static_cast<std::uint32_t>(rng() & ((1u << K) - 1));
    const u64 t = (1u << K) / 3, t2 = v[7];
    stats want;
    for (auto x : v) want.sum += x, want.ge += x >= t, want.eq += x == t2, want.max = std::max<u64>(want.max, x);
    std::vector<std::byte> bytes(n / L::tile_rows * L::tile_bytes);
    auto view = *sp::view<L>::attach(n, {{{std::span(bytes), L::tile_bytes}, {}, {}}});
    auto w = sp::bind_writer(view);
    auto e = w->template erase<std::uint32_t>();
    std::vector<ikea::journal::entry> rec(*w->effect_capacity_initialize());
    ikea::journal effects{rec};
    if (!e.initialize(v, effects)) std::abort();
    const auto dense = *sp::dense(view.as_const());
    const auto expr = cp::expression(dense);
    const auto reader = sp::bind_reader(dense);
    constexpr unsigned N = K <= 8 ? 64 : 32;
    auto fused = [&] {
        stats s;
        sp::native_group::sum_state sum;
        maxer<K, N> mx;
        counter<ikea::comparison::greater_equal, K, N> ge{t};
        counter<ikea::comparison::equal, K, N> eq{t2};
        cp::scan_regions(expr, 0, n, [&](std::size_t, const auto& x) __attribute__((always_inline)) {
            sum.add(sp::native_group::sum(x, sp::native_group::active_mask<K, N>(~u64{0})));
            ge.add(x);
            eq.add(x);
            mx.add(x);
        }, [&](std::size_t, u64 x) { s.sum += x, s.ge += x >= t, s.eq += x == t2, s.max = std::max(s.max, x); });
        s.sum += sum.finish();
        s.ge += ge.finish();
        s.eq += eq.finish();
        s.max = std::max(s.max, mx.finish());
        return s;
    };
    auto max_scan = [&] {
        maxer<K, N> mx;
        u64 m = 0;
        cp::scan_regions(expr, 0, n, [&](std::size_t, const auto& x) __attribute__((always_inline)) { mx.add(x); },
                         [&](std::size_t, u64 x) { m = std::max(m, x); });
        return std::max(m, mx.finish());
    };
    auto separate = [&] {
        stats s;
        s.sum = reader.sum_unchecked(0, n);
        s.ge = reader.template count_unchecked<ikea::comparison::greater_equal>(0, n, t);
        s.eq = reader.template count_unchecked<ikea::comparison::equal>(0, n, t2);
        s.max = max_scan();
        return s;
    };
    std::vector<sp::uint_for<K>> lanes(v.begin(), v.end());
    auto native = [&] {
        stats s;
        for (std::size_t i = 0; i < n; ++i) {
            const u64 x = lanes[i];
            s.sum += x, s.ge += x >= t, s.eq += x == t2, s.max = std::max(s.max, x);
        }
        return s;
    };
    auto time = [&](const char* arm, auto&& f, bool ok) {
        if (!ok) {
            std::fprintf(stderr, "K=%u %s %s: wrong result\n", K, name, arm);
            std::exit(1);
        }
        std::vector<double> ts;
        volatile u64 sink = 0;
        for (int k = 0; k < 41; ++k) {
            auto a = std::chrono::steady_clock::now();
            sink = sink + f();
            auto b = std::chrono::steady_clock::now();
            ts.push_back(std::chrono::duration<double, std::nano>(b - a).count() / (n / 256.0));
        }
        std::nth_element(ts.begin(), ts.begin() + 20, ts.end());
        std::printf("{\"k\":%u,\"layout\":\"%s\",\"arm\":\"%s\",\"ns_per_256_rows\":%.3f,\"ok\":%d}\n", K, name, arm, ts[20], int(ok));
    };
    time("fused", [&] { return digest(fused()); }, fused() == want);
    time("separate", [&] { return digest(separate()); }, separate() == want);
    time("max_scan", [&] { return max_scan(); }, max_scan() == want.max);
    time("native", [&] { return digest(native()); }, native() == want);
}
int main() {
    const std::size_t n = 65536;
    run<3, sp::preset::compact>(n, "compact");
    run<7, sp::preset::compact>(n, "compact");
    run<8, sp::preset::compact>(n, "compact");
    run<12, sp::preset::compact>(n, "compact");
#if defined(__aarch64__)
    run<3, sp::preset::bulk_arm>(n, "bulk");
    run<7, sp::preset::bulk_arm>(n, "bulk");
    run<12, sp::preset::bulk_x86>(n, "bulk");
    run<14, sp::preset::bulk_arm>(n, "bulk");
#else
    run<3, sp::preset::bulk_x86>(n, "bulk");
    run<7, sp::preset::bulk_x86>(n, "bulk");
    run<12, sp::preset::bulk_x86>(n, "bulk");
#endif
}
