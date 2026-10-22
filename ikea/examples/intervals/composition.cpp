#include <ikea/intervals.h>
#include <ikea/intervals/author/search.h>
#include <ikea/seriespack.h>
#include <array>
#include <cassert>

namespace bl = ikea::intervals;
template <class Decoder> struct packed_bounds {
    Decoder decoder;
    unsigned count;
    unsigned size() const { return count; }
    auto at(unsigned rank) const { return decoder.get_unchecked(rank); }
};
int main() {
    // Compare an already-bound packed reader directly, without decoding a
    // temporary endpoint array or reattaching/checking a pack at every probe.
    namespace sp = ikea::seriespack;
    using F = sp::format<12, sp::geometry::local>;
    constexpr unsigned n = 64;
    alignas(64) std::array<std::uint8_t, n / F::tile_rows * F::tile_bytes> payload{};
    const auto storage = sp::view<F, std::uint8_t>::attach(n, {{{payload, F::tile_bytes}, {}, {}}});
    assert(storage);
    const auto writer = sp::bind_mutation(*storage);
    assert(writer);
    std::array<std::uint16_t, n> endpoints;
    for (unsigned r = 0; r < n; ++r)
        endpoints[r] = r * 3;
    const auto operation = writer->template erase<std::uint16_t, sp::sum_change>();
    std::array<sp::composition::owner_write, 32> records;
    sp::composition::source_write_journal damage{records};
    assert(operation.initialize(endpoints, damage));
    const packed_bounds source{sp::bind_decoder<std::uint16_t>(*storage), n};
    assert(bl::lower_bound(source, 70u) == 24 && bl::upper_bound(source, 72u) == 25);

    // Header/checkpoints are already available; one local width group remains.
    alignas(64) std::array<std::uint8_t, 32> widths;
    widths.fill(7);
    const std::array<std::uint64_t, 2> checkpoints{1ull << 40, (1ull << 40) + 16 * 7};
    const bl::delta_source<std::uint8_t, std::uint64_t> bounds{widths.data(), checkpoints.data(),
                                                               32, 16, checkpoints[0] + 32 * 7};
    const auto pending = bl::author::prepare_lower_bound(bounds, checkpoints[0] + 130);
    const auto window = pending.local_window();
    assert(window.data() == reinterpret_cast<const std::byte *>(widths.data() + 16) &&
           window.size() == 16);
    // An owner may hint this exact window and yield to Loom here. It retains
    // coherent backing and any borrowed bindings; hints do not grant reads.
    assert(pending.finish() == 18);
    assert(bl::lower_bound(bounds, checkpoints[0] + 130) == pending.finish());

    // Selection stays independent of the endpoint encoding.
    const std::array<std::byte, 4> live{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};
    const bl::selected_source eligible{bounds, bl::rank_selection{live}};
    const auto selected = bl::author::prepare_lower_bound(eligible, checkpoints[0] + 130);
    assert(selected.finish() == 24);

    // Cache source-level search preparation across repeated queries of this
    // immutable version. The same boundary/range interface applies; rebuild the
    // binding after endpoint/count/placement changes.
    std::array<std::uint32_t, 4096> large;
    for (unsigned i = 0; i < large.size(); ++i)
        large[i] = 1000 + i * 3;
    const auto searched =
        bl::author::bind_search(bl::direct_source{large.data(), unsigned(large.size())});
    assert(bl::lower_bound(searched, 4000u) == 1000);
    assert(bl::upper_bound(searched, 4000u) == 1001);
    const auto ranks =
        bl::author::lower_bounds(std::array{searched, searched}, std::array{4000u, 4001u});
    assert((ranks == std::array{1000u, 1001u}));
}
