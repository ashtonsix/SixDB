// NEON compact (Local) residual-only decode on Graviton 4: where the time
// goes against bulk and native, and whether a per-plane spread beats the
// current quad transpose. Arms per K (1..7), ns per 256 rows, warm:
//   reader_u32   the reader's read_unchecked to u32 (shipped path)
//   group_u8     native_group::read<F, true, 64> stored as bytes (reconstruct only)
//   spread_u8    each tile's plane byte spread across eight lanes (tbl, tst, and, or)
//   spread_u32   spread_u8 widened to u32 as the reader stores
//   copy_u32     u8 array widened to u32 (store-side floor)
#include <ikea/seriespack.h>
#include <arm_neon.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
namespace sp = ikea::seriespack;
using u8 = std::uint8_t;
using u32 = std::uint32_t;

template <unsigned R> [[gnu::always_inline]] inline void spread64(const u8* p, uint8x16_t out[4]) {
    // 8R bytes of 8 tiles; tables of up to four registers.
    uint8x16x4_t raw;
    u8 pad[64] = {};
    std::memcpy(pad, p, 8 * R);
    raw.val[0] = vld1q_u8(pad), raw.val[1] = vld1q_u8(pad + 16), raw.val[2] = vld1q_u8(pad + 32), raw.val[3] = vld1q_u8(pad + 48);
    static constexpr u8 bits[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const auto bit = vld1q_u8(bits);
    sp::detail::each<4>([&](auto v) __attribute__((always_inline)) {
        auto x = vdupq_n_u8(0);
        sp::detail::each<R>([&](auto b) __attribute__((always_inline)) {
            static constexpr auto index = [] {
                std::array<u8, 16> a{};
                for (unsigned j = 0; j < 16; ++j)
                    a[j] = static_cast<u8>((2 * decltype(v)::value + j / 8) * R + decltype(b)::value);
                return a;
            }();
            const auto plane = vqtbl4q_u8(raw, vld1q_u8(index.data()));
            x = vorrq_u8(x, vandq_u8(vtstq_u8(plane, bit), vdupq_n_u8(1u << b)));
        });
        out[v] = x;
    });
}
[[gnu::always_inline]] inline void widen(const uint8x16_t x, u32* o) {
    const auto lo = vmovl_u8(vget_low_u8(x)), hi = vmovl_high_u8(x);
    vst1q_u32(o, vmovl_u16(vget_low_u16(lo)));
    vst1q_u32(o + 4, vmovl_high_u16(lo));
    vst1q_u32(o + 8, vmovl_u16(vget_low_u16(hi)));
    vst1q_u32(o + 12, vmovl_high_u16(hi));
}

template <unsigned K> void run(std::size_t n) {
    using L = sp::preset_layout<K, sp::preset::compact>;
    std::mt19937_64 rng(K);
    std::vector<u32> v(n), o(n + 64);
    std::vector<u8> o8(n + 64), v8(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<u32>(rng() & ((1u << K) - 1)), v8[i] = static_cast<u8>(v[i]);
    std::vector<std::byte> bytes(n / 8 * L::tile_bytes + 64);
    auto view = *sp::view<L>::attach(n, {{{std::span(bytes.data(), n / 8 * L::tile_bytes), L::tile_bytes}, {}, {}}});
    auto w = sp::bind_writer(view);
    auto e = w->template erase<u32>();
    std::vector<ikea::journal::entry> rec(*w->effect_capacity_initialize());
    ikea::journal effects{rec};
    if (!e.initialize(v, effects)) std::abort();
    auto r = sp::bind_reader(view);
    const auto* base = reinterpret_cast<const u8*>(bytes.data());
    auto time = [&](const char* arm, auto&& f, auto&& check) {
        std::vector<double> t;
        for (int k = 0; k < 41; ++k) {
            auto a = std::chrono::steady_clock::now();
            f();
            auto b = std::chrono::steady_clock::now();
            t.push_back(std::chrono::duration<double, std::nano>(b - a).count() / (n / 256.0));
        }
        std::nth_element(t.begin(), t.begin() + 20, t.end());
        if (!check()) {
            std::fprintf(stderr, "K=%u %s: wrong result\n", K, arm);
            std::exit(1);
        }
        std::printf("{\"k\":%u,\"arm\":\"%s\",\"ns_per_256_rows\":%.3f,\"ok\":1}\n", K, arm, t[20]);
    };
    time("reader_u32", [&] { r.read_unchecked(0, n, o.data()); }, [&] { return std::memcmp(o.data(), v.data(), n * 4) == 0; });
    time("group_u8", [&] {
        for (std::size_t i = 0; i < n; i += 64) {
            const auto x = sp::neon_group::read<L, true, 64>(base, L::tile_bytes, i);
            for (unsigned b = 0; b < 4; ++b) vst1q_u8(o8.data() + i + 16 * b, x.block[b].v[0]);
        }
    }, [&] { return std::memcmp(o8.data(), v8.data(), n) == 0; });
    time("spread_u8", [&] {
        for (std::size_t i = 0; i < n; i += 64) {
            uint8x16_t x[4];
            spread64<L::tail>(base + i / 8 * L::tile_bytes, x);
            for (unsigned b = 0; b < 4; ++b) vst1q_u8(o8.data() + i + 16 * b, x[b]);
        }
    }, [&] { return std::memcmp(o8.data(), v8.data(), n) == 0; });
    time("spread_u32", [&] {
        for (std::size_t i = 0; i < n; i += 64) {
            uint8x16_t x[4];
            spread64<L::tail>(base + i / 8 * L::tile_bytes, x);
            for (unsigned b = 0; b < 4; ++b) widen(x[b], o.data() + i + 16 * b);
        }
    }, [&] { return std::memcmp(o.data(), v.data(), n * 4) == 0; });
    time("copy_u32", [&] {
        for (std::size_t i = 0; i < n; i += 16) widen(vld1q_u8(v8.data() + i), o.data() + i);
    }, [&] { return std::memcmp(o.data(), v.data(), n * 4) == 0; });
}
int main() {
    const std::size_t n = 65536;
    sp::detail::each<7>([&](auto k) { run<k + 1>(n); });
}
