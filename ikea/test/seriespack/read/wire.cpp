#include <ikea/seriespack/author/expression.h>
#include <ikea/seriespack/read.h>
#include "../reference/physical.h"
#include <array>
#include "../support.h"
#include <iostream>
#include <random>
#include <vector>

namespace sp = ikea::seriespack;
namespace old = ikea2_reference::seriespack;
template <unsigned K, sp::geometry G = sp::geometry::local> void check() {
    using F = sp::format<K, G>;
    ikea_test::format_scope<F> format_context{"independent wire read"};
    constexpr auto OG = G == sp::geometry::local ? old::geometry::local8 : old::geometry::striped;
    constexpr std::size_t N = 512;
    alignas(64) std::array<std::uint8_t, N * K / 8> encoded{};
    std::array<std::uint64_t, N> expected{};
    std::mt19937_64 rng(K);
    for (auto& x : expected) {
        x = rng();
        if constexpr (K < 64)
            x &= (std::uint64_t{1} << K) - 1;
    }
    // Existing independently checked wire implementation supplies the input;
    // Ikea neither encodes this oracle nor shares its native reader.
    for (std::size_t i = 0; i < N; i += F::tile_rows)
        old::detail::encode_tile<K, OG>(expected.data() + i, encoded.data() + i * K / 8);
    auto v = sp::view<F>::attach(N, {{{encoded, F::tile_bytes}, {}, {}}});
    IKEA_CHECK(v);
    auto dense = sp::dense(*v);
    IKEA_CHECK(dense);
    auto decoder = sp::bind_decoder<std::uint64_t>(*dense);
    std::array<std::uint64_t, N + 2> output;
    for (std::size_t i = 0; i < N; i += 16) {
        output.fill(0xbad);
        decoder.read16_unchecked(i, output.data() + 1);
        IKEA_CHECK(output[0] == 0xbad && output[17] == 0xbad);
        for (unsigned j = 0; j < 16; ++j)
            IKEA_CHECK(output[j + 1] == expected[i + j]);
    }
    for (std::size_t first = 0; first < 64; ++first)
        for (std::size_t n = 0; n < 70; ++n) {
            ikea_test::scope scenario{"arbitrary read", first, n};
            output.fill(0xbad);
            decoder.read_unchecked(first, n, output.data() + 1);
            IKEA_CHECK(output[0] == 0xbad && output[n + 1] == 0xbad);
            for (std::size_t j = 0; j < n; ++j)
                IKEA_CHECK(output[j + 1] == expected[first + j]);
        }
    for (std::size_t i = 0; i < N; ++i) {
        IKEA_CHECK(sp::get_unchecked(*v, i) == expected[i]);
        IKEA_CHECK(decoder.get_unchecked(i) == expected[i]);
    }
#if defined(__aarch64__) || defined(__AVX2__)
    const auto expression = sp::composition::describe(*dense);
    for (auto count : {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{511}, N})
        for (auto cutoff :
             {std::uint64_t{0}, std::uint64_t{123}, std::uint64_t{65535}, std::uint64_t(-1)})
            for (auto bits : {std::uint16_t{0}, std::uint16_t{0xffff}, std::uint16_t{0xb6db}}) {
                std::uint64_t want = 0;
                for (std::size_t i = 0; i < count; ++i)
                    if ((bits & (1u << (i % 16))) && expected[i] < cutoff)
                        want += expected[i];
                const auto got = sp::composition::sum_regions(expression, count, cutoff,
                                                              [&](std::size_t) { return bits; });
                IKEA_CHECK(want == got);
            }
#endif
}
void check_composition();
void check_mutation();
void check_composed_mutation();
void check_chain();
void check_construction();
void check_representations();
int main() {
    check_composition();
    check_mutation();
    check_composed_mutation();
    check_chain();
    check_construction();
    check_representations();
    sp::detail::each<64>([](auto k) { check<k + 1>(); });
    sp::detail::each<7>([](auto k) { check<k + 1, sp::geometry::striped>(); });
    check<10, sp::geometry::striped>();
    check<12, sp::geometry::striped>();
    check<14, sp::geometry::striped>();
    check<15, sp::geometry::striped>();
    check<20, sp::geometry::striped>();
    std::cout << "Ikea: 76 headless reads; 206 placed read/composition/mutation formats; "
                 "substitution, coverage and rejection guards passed\n";
}
