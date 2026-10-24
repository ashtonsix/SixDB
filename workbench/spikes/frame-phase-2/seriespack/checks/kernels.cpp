#include <ikea/seriespack.h>
#include <cstdio>
#include <vector>
#include <random>
namespace sp = ikea::seriespack;
template <unsigned K, sp::preset P> int check(std::mt19937_64& rng) {
    using L = sp::preset_layout<K, P>;
    const std::size_t n = 65536;
    std::vector<std::uint32_t> v(n);
    for (auto& x : v) x = rng() & ((1ull << K) - 1);
    std::vector<std::byte> bytes(n / L::tile_rows * L::tile_bytes + 64);
    auto view = *sp::view<L>::attach(n, {{{std::span(bytes.data(), n / L::tile_rows * L::tile_bytes), L::tile_bytes}, {}, {}}});
    auto w = sp::bind_writer(view);
    auto e = w->template erase<std::uint32_t>();
    std::vector<ikea::journal::entry> rec(*w->effect_capacity_initialize());
    ikea::journal effects{rec};
    if (!e.initialize(v, effects)) return 1;
    auto r = sp::bind_reader(view);
    std::uint64_t want = 0, cnt = 0; const std::uint64_t t = (1ull << K) / 3;
    for (auto x : v) want += x, cnt += x >= t;
    std::uint64_t got = r.sum_unchecked(0, n, ~0ull, [](std::size_t) { return std::uint16_t{0xffff}; });
    std::uint64_t c = 0;
    for (std::size_t i = 0; i < n; i += 64) c += std::popcount(r.template filter_unchecked<ikea::comparison::greater_equal>(i, 64, t));
    if (r.template count_unchecked<ikea::comparison::greater_equal>(0, n, t) != cnt) { std::printf("FAIL count_unchecked K=%u P=%d\n", K, int(P)); return 1; }
    { std::uint64_t pc = 0; for (std::size_t i = 64; i < n - 30; ++i) pc += v[i] >= t;
      if (r.template count_unchecked<ikea::comparison::greater_equal>(64, n - 94, t) != pc) { std::printf("FAIL count partial K=%u\n", K); return 1; }
      std::vector<std::uint64_t> words((n + 63) / 64);
      r.template filter_words_unchecked<ikea::comparison::less>(0, n, t, words.data());
      for (std::size_t i = 0; i < n; ++i) if (((words[i / 64] >> (i % 64)) & 1) != (v[i] < t)) { std::printf("FAIL words K=%u row %zu\n", K, i); return 1; } 
      auto ops = [&]<ikea::comparison Op>(std::uint64_t rhs) {
          std::uint64_t want = 0;
          for (std::size_t i = 0; i < n; ++i) want += ikea::compare<Op>(v[i], rhs);
          return r.template count_unchecked<Op>(0, n, rhs) == want;
      };
      for (std::uint64_t rhs : {std::uint64_t{0}, std::uint64_t{t}, std::uint64_t{(1ull << K) - 1}})
          if (!ops.template operator()<ikea::comparison::equal>(rhs) || !ops.template operator()<ikea::comparison::less>(rhs) ||
              !ops.template operator()<ikea::comparison::less_equal>(rhs) || !ops.template operator()<ikea::comparison::greater>(rhs) ||
              !ops.template operator()<ikea::comparison::greater_equal>(rhs)) { std::printf("FAIL ops K=%u rhs %llu\n", K, (unsigned long long)rhs); return 1; } }
    // unaligned / partial ranges
    std::uint64_t part = r.sum_unchecked(3, n - 100, ~0ull, [](std::size_t) { return std::uint16_t{0xffff}; }), wp = 0;
    for (std::size_t i = 3; i < n - 97; ++i) wp += v[i];
    if (got != want || c != cnt || part != wp) { std::printf("FAIL K=%u P=%d sum %llu/%llu count %llu/%llu part %llu/%llu\n", K, int(P), (unsigned long long)got, (unsigned long long)want, (unsigned long long)c, (unsigned long long)cnt, (unsigned long long)part, (unsigned long long)wp); return 1; }
    return 0;
}
int main() {
    std::mt19937_64 rng(1);
    int f = 0;
    f += check<1, sp::preset::bulk_x86>(rng) + check<3, sp::preset::bulk_x86>(rng) + check<5, sp::preset::bulk_x86>(rng) + check<6, sp::preset::bulk_x86>(rng) + check<7, sp::preset::bulk_x86>(rng);
    f += check<12, sp::preset::bulk_x86>(rng) + check<20, sp::preset::bulk_x86>(rng) + check<10, sp::preset::bulk_arm>(rng) + check<14, sp::preset::bulk_arm>(rng) + check<15, sp::preset::bulk_arm>(rng);
    f += check<1, sp::preset::compact>(rng) + check<2, sp::preset::compact>(rng) + check<3, sp::preset::compact>(rng) + check<4, sp::preset::compact>(rng) + check<5, sp::preset::compact>(rng) + check<6, sp::preset::compact>(rng) + check<7, sp::preset::compact>(rng) + check<9, sp::preset::compact>(rng) + check<10, sp::preset::compact>(rng) + check<11, sp::preset::compact>(rng) + check<12, sp::preset::compact>(rng) + check<13, sp::preset::compact>(rng) + check<14, sp::preset::compact>(rng) + check<15, sp::preset::compact>(rng) + check<20, sp::preset::compact>(rng) + check<24, sp::preset::compact>(rng) + check<31, sp::preset::compact>(rng);
    std::printf("%d failures\n", f);
    return f;
}
