#include <ikea/boundlist.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>

int main() {
    namespace bl = ikea::boundlist;
    // Cuts [0,8,18,26,35,46,56] define six contiguous intervals.
    // Omit the zero origin: each stored end's index is its interval rank.
    const std::array<unsigned, 6> end_values{8, 18, 26, 35, 46, 56};
    const bl::direct_source ends{end_values.data(), unsigned(end_values.size())};
    assert(bl::lower_bound(ends, 18u) == 1); // the end at 18
    assert(bl::upper_bound(ends, 18u) == 2); // containing interval [18,26)
    assert(bl::upper_bound(ends, 56u) == ends.size()); // final cut excluded
    const bl::rank_window ranks{bl::upper_bound(ends, 9u), bl::lower_bound(ends, 36u) + 1};
    assert((ranks == bl::rank_window{1, 5}));
    // An exact-width source does not narrow an out-of-domain query.
    assert(bl::lower_bound(ends, UINT64_MAX) == 6);

    // The same geometry contract accepts the complete cut array, with an
    // explicit origin and no packed-format or retirement requirement.
    const std::array cuts{0u, 8u, 18u, 26u, 35u, 46u, 56u};
    const auto geometry = bl::from_cuts(std::span(cuts));
    assert(geometry.size() == 6 && bl::reverse(geometry, 18u) == 2);
    assert(bl::overlap(geometry, 9u, 36u) == ranks);

    // Encode the same cuts as mutable geometry. Only metadata storage is needed.
    const auto format = bl::layout::prepare({.maximum = 255, .extent_limit = 4096});
    assert(format);
    std::array<std::byte, 64> directory{};
    alignas(64) std::array<std::byte, 512> lengths{};
    std::array<std::byte, 6> control{};
    auto source = bl::view::bind(*format, {directory, lengths, control});
    assert(source);
    std::array<ikea::owner_write, 32> records;
    ikea::source_write_journal damage{records};
    assert(bl::initialize(*source, damage));
    for (unsigned size : std::array{8u, 10u, 8u, 9u, 11u, 10u})
        assert(bl::append(*source, size, damage));
    assert(bl::retire(*source, 2, damage));
    assert(source->validate());
    // Raw geometry preserves every interval, including a retired rank.
    assert(bl::reverse(*source, 20) == 2);
    assert(bl::reverse(*source, 26) == 3);
    assert(bl::reverse(*source, 56) == source->size());
    const bl::selected_source live{*source, source->live_ranks()};
    assert(bl::reverse(live, 20) == source->size()); // no later-owner fallback
    assert((bl::overlap(live, 9, 36) == ranks));
    std::array<unsigned, 3> visited{};
    unsigned count = 0;
    assert(bl::range(live, ranks.first, ranks.limit - ranks.first,
                     [&](unsigned rank, bl::interval<std::uint32_t> bounds) {
                         assert(bounds.first < bounds.limit);
                         visited[count++] = rank;
                     }));
    assert((visited == std::array{1u, 3u, 4u}) && count == visited.size());
    for (const auto& e : damage.entries())
        assert(e.source == &*source && e.bytes.plane < 3);
    // The enclosing owner resolves these ranges, retains readers and publishes
    // geometry with any associated data. Ikea does not publish it.
}
