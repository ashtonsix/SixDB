#include <ikea/boundlist.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>

int main() {
    namespace bl = ikea::boundlist;
    // Logical occupied extrema, including populated singleton/duplicate segments.
    const std::array<unsigned, 4> minimum{1, 2, 2, 9}, maximum{2, 2, 5, 12};
    const bl::direct_source minima{minimum.data(), 4u}, maxima{maximum.data(), 4u};
    assert(bl::lower_bound(maxima, 2u) == 0); // unique/exact-max routing needs one search
    const auto ranks = bl::overlap(maxima, minima, bl::query_range{2u, 3u});
    assert((ranks == bl::rank_window{0, 3}));
    // An exact-width source does not narrow an out-of-domain query.
    assert(bl::lower_bound(maxima, UINT64_MAX) == 4);

    // A curated reservation format. It does not require any payload allocation.
    namespace sc = bl::sidecar;
    const auto format = sc::layout::prepare({.maximum = 255, .extent_limit = 4096});
    assert(format);
    std::array<std::byte, 64> directory{};
    alignas(64) std::array<std::byte, 512> lengths{};
    std::array<std::byte, 6> control{};
    auto source = sc::view::bind(*format, {directory, lengths, control});
    assert(source);
    std::array<ikea::owner_write, 32> records;
    ikea::source_write_journal damage{records};
    assert(sc::initialize(*source, damage));
    assert(sc::append(*source, 5, damage));
    assert(sc::append(*source, 0, damage));
    assert(sc::append(*source, 8, damage));
    assert(sc::retire(*source, 0, damage));
    assert(source->validate());
    assert(sc::reverse(*source, 4)->rank == 0 && sc::reverse(*source, 4)->tombstone);
    assert(sc::reverse(*source, 5)->rank == 2);
    assert(!sc::reverse(*source, 13));
    assert((sc::overlap(*source, 1, 8, true) == bl::rank_window{2, 3}));
    for (const auto& e : damage.entries())
        assert(e.source == &*source && e.bytes.plane < 3);
    // The enclosing owner resolves these ranges, retains readers and publishes
    // metadata with any corresponding payload/children. Ikea does not publish it.
}
