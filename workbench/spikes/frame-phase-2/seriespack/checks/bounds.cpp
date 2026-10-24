// Range reads near the end of an exactly sized buffer followed by a guard page,
// and masked sums mixing full and partial groups.
#include <ikea/seriespack.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
namespace sp = ikea::seriespack;
template <unsigned K, sp::preset P> int check(std::mt19937_64& rng) {
    using L = sp::preset_layout<K, P>;
    const std::size_t tiles = (L::tile_rows >= 64 ? 9 : 8 * L::tile_rows > 64 ? 9 : 129), n = tiles * L::tile_rows;
    const std::size_t nb = tiles * L::tile_bytes, page = sysconf(_SC_PAGESIZE);
    const std::size_t span = (nb + page - 1) / page * page;
    auto* m = static_cast<std::byte*>(mmap(nullptr, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    mprotect(m + span, page, PROT_NONE);
    std::byte* bytes = m + span - nb;
    std::vector<std::uint32_t> v(n);
    for (auto& x : v) x = rng() & ((1ull << K) - 1);
    auto view = *sp::view<L>::attach(n, {{{std::span(bytes, nb), L::tile_bytes}, {}, {}}});
    auto w = sp::bind_writer(view);
    auto e = w->template erase<std::uint32_t>();
    std::vector<ikea::journal::entry> rec(*w->effect_capacity_initialize());
    ikea::journal effects{rec};
    if (!e.initialize(v, effects)) return 1;
    auto r = sp::bind_reader(view);
    std::vector<std::uint32_t> out(n + 16);
    for (std::size_t first = n > 300 ? n - 300 : 0; first < n; ++first)
        for (std::size_t count = 0; count <= std::min<std::size_t>(n - first, 140); ++count) {
            r.read_unchecked(first, count, out.data());
            if (std::memcmp(out.data(), v.data() + first, count * 4)) { std::printf("FAIL read K=%u P=%d first %zu count %zu\n", K, int(P), first, count); return 1; }
        }
    for (std::size_t first = 0; first < std::min<std::size_t>(n, 40); first += 3)
        for (std::size_t count : {n - first, n - first - std::min<std::size_t>(n - first, 13), std::min<std::size_t>(n - first, 1100)}) {
            std::uint64_t want = 0;
            for (std::size_t i = first; i < first + count; ++i) want += v[i];
            if (r.sum_unchecked(first, count) != want) { std::printf("FAIL total K=%u P=%d first %zu count %zu\n", K, int(P), first, count); return 1; }
        }
    std::vector<std::uint16_t> m16(n / 16 + 1);
    for (auto& x : m16) { auto q = rng() % 4; x = q == 0 ? 0 : q == 1 ? 0xffff : std::uint16_t(rng()); }
    const std::uint64_t cutoffs[] = {~0ull, (1ull << K), (1ull << K) / 2};
    for (auto cut : cutoffs)
        for (std::size_t first : {std::size_t{0}, std::size_t{5}, std::size_t{64}})
            for (std::size_t count : {n - first, n - first - 7}) {
                std::uint64_t want = 0;
                for (std::size_t i = first; i < first + count; ++i)
                    if ((m16[i / 16] >> (i % 16) & 1) && v[i] < cut) want += v[i];
                auto got = r.sum_unchecked(first, count, cut, [&](std::size_t row) { return m16[row / 16]; });
                if (got != want) { std::printf("FAIL msum K=%u P=%d cut %llu first %zu\n", K, int(P), (unsigned long long)cut, first); return 1; }
            }
    munmap(m, span + page);
    return 0;
}
int main() {
    std::mt19937_64 rng(2);
    int f = 0;
    f += check<1, sp::preset::bulk_x86>(rng) + check<2, sp::preset::bulk_x86>(rng) + check<4, sp::preset::bulk_x86>(rng) + check<6, sp::preset::bulk_x86>(rng) + check<3, sp::preset::bulk_x86>(rng) + check<5, sp::preset::bulk_x86>(rng) + check<7, sp::preset::bulk_x86>(rng);
    f += check<12, sp::preset::bulk_x86>(rng) + check<20, sp::preset::bulk_x86>(rng) + check<10, sp::preset::bulk_arm>(rng) + check<14, sp::preset::bulk_arm>(rng) + check<15, sp::preset::bulk_arm>(rng);
    f += check<1, sp::preset::compact>(rng) + check<3, sp::preset::compact>(rng) + check<8, sp::preset::compact>(rng) + check<9, sp::preset::compact>(rng) + check<10, sp::preset::compact>(rng) + check<11, sp::preset::compact>(rng) + check<13, sp::preset::compact>(rng) + check<14, sp::preset::compact>(rng) + check<5, sp::preset::compact>(rng) + check<7, sp::preset::compact>(rng) + check<12, sp::preset::compact>(rng) + check<15, sp::preset::compact>(rng) + check<16, sp::preset::compact>(rng) + check<20, sp::preset::compact>(rng) + check<17, sp::preset::compact>(rng) + check<23, sp::preset::compact>(rng) + check<24, sp::preset::compact>(rng) + check<26, sp::preset::compact>(rng) + check<31, sp::preset::compact>(rng) + check<32, sp::preset::compact>(rng);
    std::printf("%d failures\n", f);
    return f;
}
