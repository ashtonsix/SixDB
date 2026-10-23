#include <ikea/seriespack/author/store.h>
#include <ikea/seriespack.h>
#include <ikea/seriespack/author/chain.h>
#include <array>
#include <cassert>
#include <bit>

namespace sp = ikea::seriespack;
using C = ikea::comparison;

// Both physical widths below decode into sixteen unsigned 16-bit lanes. Every
// stage uses this one chain type; there are no function-pointer casts or arrays
// of decoded values at the boundary.
using Values = sp::native::values<16>;
using Chain = sp::author::chain<16, 4>;
struct bindings {
    const std::byte* bytes;
    std::uint64_t lower, upper;
    std::uint64_t matches = 0;
    std::uint16_t selected = 0;
};

template <unsigned K>
Chain::result decode(void* opaque, std::size_t first, std::uint16_t active, Values, unsigned) {
    static_assert(K > 8 && K <= 16);
    if (!active)
        return {Values{}, 0};
    const auto& bound = *static_cast<bindings*>(opaque);
    // The owner supplies a complete, aligned sixteen-row window in 64 rows.
    const sp::dense_source<sp::layout<K>> source{bound.bytes, 64};
    sp::author::native_ops ops;
    const auto decoded =
        sp::author::read(ops, sp::author::expression(source), first, active);
    return {Values{decoded.v}, active};
}

// This body and its CPS wrapper have no source-width specialization. Keep the
// two comparisons and their reduction together after one decode.
Chain::result range_count(void* opaque, std::size_t, std::uint16_t active, Values values,
                          unsigned) {
    auto& bound = *static_cast<bindings*>(opaque);
    const auto selected =
        std::uint16_t(sp::author::filter_values<C::greater_equal>(values, bound.lower, active) &
                      sp::author::filter_values<C::less>(values, bound.upper, active));
    bound.matches += std::popcount(selected);
    return {values, selected};
}
void finish(void* opaque, std::size_t, std::uint16_t active, Values) {
    static_cast<bindings*>(opaque)->selected = active;
}

template <unsigned K> void exercise() {
    using F = sp::layout<K>;
    alignas(64) std::array<std::uint8_t, K * 8> bytes{};
    const auto attached = sp::view<F>::attach(64, {{{std::as_writable_bytes(std::span{bytes}), F::tile_bytes},{},{}}});
    assert(attached);
    auto source = *attached;
    std::array<std::uint64_t, 64> truth{};
    for (unsigned r = 0; r < 64; ++r) {
        truth[r] = (r * 997) & ((1u << K) - 1);
        sp::author::store_unchecked(source, r, truth[r]);
    }
    const std::array<Chain::function, 2> stages{&Chain::stage<&decode<K>>,
                                                &Chain::stage<&range_count>};
    const auto plan = *Chain::make(stages, &Chain::completion<&finish>);
    for (auto upper : {std::uint64_t{0}, std::uint64_t{1000}, std::uint64_t{4096},
                       std::uint64_t{65536}, ~std::uint64_t{0}})
        for (std::uint16_t active :
             {std::uint16_t{0}, std::uint16_t{0xa53c}, std::uint16_t{65535}}) {
            bindings direct{std::as_bytes(std::span{bytes}).data(), 500, upper}, continued = direct;
            std::uint64_t expected_count = 0;
            for (unsigned first = 0; first < 64; first += 16) {
                const auto decoded = decode<K>(&direct, first, active, Values{}, 0);
                const auto reduced = range_count(&direct, first, active, decoded.values, 1);
                // CPS is for an enclosing composed operation. An ordinary
                // small filter should use the inline API, as in ordinary.cpp.
                plan.run(&continued, first, active, Values{});
                std::uint16_t expected_mask = 0;
                for (unsigned i = 0; i < 16; ++i)
                    if ((active >> i & 1) && truth[first + i] >= 500 && truth[first + i] < upper) {
                        expected_mask |= 1u << i;
                        ++expected_count;
                    }
                assert(reduced.active == expected_mask);
                assert(continued.selected == expected_mask);
            }
            assert(direct.matches == expected_count && continued.matches == expected_count);
        }
}
int main() {
    exercise<12>();
    exercise<15>();
}
