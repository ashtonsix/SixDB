#include <ikea/seriespack.h>
#include <ikea/seriespack/author/read.h>
#include <ikea/tuplepack.h>
#include <array>
#include <cassert>
#include <bit>

namespace sp = ikea::seriespack;
namespace cp = sp::composition;
namespace tp = ikea::tuplepack;
using C = ikea::comparison;

int main() {
    using F = sp::format<64, sp::geometry::local, 8>;
    constexpr unsigned count = 64;
    constexpr std::uint64_t query = 0x4200000000000123ULL;
    alignas(64) std::array<std::uint8_t, 448> payload{};
    alignas(64) std::array<std::uint8_t, count> heads{};
    std::array<std::uint64_t, count> truth{};
    std::array<std::uint8_t, count * 4> flags{};
    auto source = *sp::view<F, std::uint8_t>::attach(
        count, {{{payload, F::tile_bytes}, {heads, F::tile_rows}, {}}});
    for (unsigned i = 0; i < count; ++i) {
        truth[i] = i / 16 == 2 ? query : (std::uint64_t(i + 1) << 56) | (i * 17);
        sp::set_unchecked(source, i, truth[i]);
        flags[i * 4] = i * 71;
    }

    // Select execution after any with_payload/with_tail substitutions. This
    // owns the expression nodes and borrows source; it adds no stored metadata.
    const auto expression = cp::guarded(cp::describe(source));
    const auto prepared = *cp::prepare(expression, count);
    auto layout = *tp::layout::make(1, std::array<tp::code, 1>{{{0, 0, 8}}});
    auto tuples = *tp::view::bind(layout, flags, count, 4);
    auto plan = *tp::reader<64, 16>::make(layout, std::array<tp::byte, 1>{0});
    auto reader = *tp::bind_reader(plan, tuples);
    auto second = *tp::bind_filter(reader);

    unsigned matches = 0;
    std::uint64_t expected = 0;
    for (unsigned first = 0; first < count; first += 16) {
        // The owner has established complete, aligned sixteen-row windows.
        auto selected = cp::filter16_unchecked<C::equal>(prepared.expression(), first, query);
        // Guard at a useful composition boundary. Retain the full projection
        // on survival: forwarding a sparse mask can choose a slower reader.
        if (selected)
            selected &= second.filter_unchecked<C::less>(first, 128);
        matches += std::popcount(selected);
        for (unsigned i = 0; i < 16; ++i)
            if (truth[first + i] == query)
                expected |= std::uint64_t{1} << (first + i);
    }
    unsigned wanted = 0;
    for (unsigned i = 0; i < count; ++i)
        wanted += truth[i] == query && flags[i * 4] < 128;
    assert(matches == wanted);
    assert(prepared.filter<C::equal>(0, count, query) == expected);
}
