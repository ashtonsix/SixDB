#include "support.h"

namespace cp = sp::composition;

// Write the Local wire format independently of SeriesPack's writer. Head rows
// and payload rows deliberately have different tile strides.
template <unsigned K> void wire(std::uint8_t* p, unsigned stride, unsigned row, std::uint64_t x) {
    auto* tile = p + row / 8 * stride;
    for (unsigned b = 0; b < K / 8; ++b)
        tile[row % 8 * (K / 8) + b] = x >> (K % 8 + b * 8);
    for (unsigned b = 0; b < K % 8; ++b)
        tile[8 * (K / 8) + b] |= ((x >> b) & 1) << (row % 8);
}

template <unsigned K> void bounded() {
    using F = sp::format<K, sp::geometry::local, 8>;
    constexpr unsigned n = 128, bytes = n * (K - 8) / 8;
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* memory = static_cast<std::uint8_t*>(
        mmap(nullptr, page * 4, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED);
    assert(mprotect(memory, page, PROT_READ | PROT_WRITE) == 0);
    assert(mprotect(memory + page * 2, page, PROT_READ | PROT_WRITE) == 0);
    auto* p = memory + page - bytes;
    auto* h = memory + page * 3 - n;
    std::array<std::uint64_t, n> truth{};
    constexpr auto limit = ~std::uint64_t{0} >> (64 - K);
    for (unsigned i = 0; i < n; ++i) {
        truth[i] = (std::uint64_t(i + 1) * 0xd6e8feb86659fd93ULL) & limit;
        wire<K - 8>(p, F::tile_bytes, i, truth[i]);
        h[i] = truth[i] >> (K - 8);
    }
    asm volatile("" : "+r"(p), "+r"(h)::"memory");
    const auto source =
        sp::view<F>::assume_valid(n, {{{{p, bytes}, F::tile_bytes}, {{h, n}, F::tile_rows}, {}}});
    const auto guarded = cp::guarded(cp::describe(source));
    [&]<C... Ops>(operations<Ops...>) {
        (
            [&] {
                for (auto q : {std::uint64_t{0}, truth[127], limit, limit + 1}) {
                    std::uint64_t want = 0;
                    for (unsigned i = 0; i < 64; ++i)
                        want |= std::uint64_t(oracle(Ops, truth[64 + i], q)) << i;
                    assert(cp::filter_unchecked<Ops>(guarded, 64, 64, q) == want);
                }
            }(),
            ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    assert(munmap(memory, page * 4) == 0);
}

void check_guarded_filters() {
    bounded<12>();
    bounded<64>();
    [&]<C... Ops>(operations<Ops...>) {
        (series_case<sp::format<20, sp::geometry::striped, 8>, Ops>(), ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    using F = sp::format<20, sp::geometry::local, 8>;
    using R = sp::format<12>;
    constexpr unsigned n = 64;
    alignas(64) std::array<std::uint8_t, 1024> payload{}, heads{};
    std::array<std::uint64_t, n> truth{};
    for (unsigned i = 0; i < n; ++i) {
        const auto high = (i * 13) & 255;
        truth[i] = (high << 12) | ((i * 157) & 4095);
        heads[i / 8 * 16 + i % 8] = high;
        wire<12>(payload.data(), 32, i, truth[i] & 4095);
    }
    auto child = *sp::view<R>::attach(n, {{{payload, 32}, {}, {}}});
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto* inaccessible = static_cast<std::uint8_t*>(
        mmap(nullptr, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(inaccessible != MAP_FAILED);
    auto parent =
        sp::view<F>::assume_valid(n, {{{{inaccessible, page}, F::tile_bytes}, {heads, 16}, {}}});
    const auto selected = cp::with_payload(cp::describe(parent), cp::describe(child));
    const auto expression = cp::guarded(selected);
    const auto prepared = *cp::prepare(expression, n);
    assert(!cp::prepare(expression, n + 1));
    cp::scalar_ops scalar;
    assert(cp::read(scalar, expression, 63, true).value == truth[63]);
    // The wrapper must retain admission and substitution, including ordinary
    // scalar edges. No read may reach the retired, inaccessible parent payload.
    [&]<C... Ops>(operations<Ops...>) {
        (
            [&] {
                for (auto rhs :
                     {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{0x7ffff},
                      std::uint64_t{0xfffff}, std::uint64_t{0x100000}, ~std::uint64_t{0}})
                    for (unsigned first : {0u, 1u, 8u, 16u, 31u})
                        for (unsigned count : {0u, 1u, 8u, 16u, 24u, 33u, 40u}) {
                            if (count > n - first)
                                continue;
                            for (auto active : {std::uint64_t{0}, ~std::uint64_t{0},
                                                std::uint64_t{0x8000000012348001}}) {
                                std::uint64_t want = 0;
                                for (unsigned i = 0; i < count; ++i)
                                    want |= std::uint64_t((active >> i & 1) &&
                                                          oracle(Ops, truth[first + i], rhs))
                                            << i;
                                assert(prepared.template filter<Ops>(first, count, rhs, active) ==
                                       want);
                            }
                        }
            }(),
            ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});

#if defined(__aarch64__) || defined(__AVX2__)
    const auto absent = sp::view<F>::assume_valid(
        n, {{{{inaccessible, page}, F::tile_bytes}, {{inaccessible, page}, F::tile_rows}, {}}});
    const auto absent_guarded = cp::guarded(cp::describe(absent));
    [&]<C... Ops>(operations<Ops...>) {
        (assert(cp::filter16_unchecked<Ops>(absent_guarded, 0, 1u << 20, 0xa53c) ==
                (oracle(Ops, 0, 1u << 20) ? 0xa53c : 0)),
         ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    assert(cp::filter16_unchecked<C::equal>(absent_guarded, 0, 0, 0) == 0);
    // Exercise the guard itself with a live but unreadable payload. Inactive
    // lanes tie the query, so rejecting only after a full-vector zero test would
    // incorrectly touch the suffix on sparse masks.
    const auto guarded_parent = cp::guarded(cp::describe(parent));
    for (unsigned i = 0; i < 64; ++i)
        heads[i / 8 * 16 + i % 8] = i % 2 ? 127 : i % 4 ? 1 : 254;
    asm volatile("" ::: "memory");
    [&]<C... Ops>(operations<Ops...>) {
        (
            [&] {
                constexpr std::uint64_t rhs = (127u << 12) | 100;
                for (std::uint64_t active : {0ULL, 0x5555555555555555ULL}) {
                    std::uint64_t want = 0;
                    for (unsigned i = 0; i < 64; ++i)
                        want |=
                            std::uint64_t(
                                (active >> i & 1) &&
                                oracle(Ops, std::uint64_t(heads[i / 8 * 16 + i % 8]) << 12, rhs))
                            << i;
                    assert(cp::filter_unchecked<Ops>(guarded_parent, 0, 64, rhs, active) == want);
                    assert(cp::filter_unchecked<Ops>(guarded_parent, 8, 8, rhs, active >> 8) ==
                           ((want >> 8) & 255));
                    assert(cp::filter_unchecked<Ops>(guarded_parent, 0, 24, rhs, active) ==
                           (want & 0xffffff));
                }
                assert(cp::filter16_unchecked<Ops>(guarded_parent, 0, 0, 0) == 0);
            }(),
            ...);
    }(operations<C::equal, C::less, C::less_equal, C::greater, C::greater_equal>{});
    for (unsigned i = 0; i < 64; ++i)
        heads[i / 8 * 16 + i % 8] = i % 2 ? 1 : 254;
    asm volatile("" ::: "memory");
    assert(cp::filter_unchecked<C::equal>(guarded_parent, 0, 64, 127u << 12) == 0);
#endif
    assert(munmap(inaccessible, page) == 0);
}
