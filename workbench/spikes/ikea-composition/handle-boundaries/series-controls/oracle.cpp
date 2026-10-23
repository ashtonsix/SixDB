#include "oracle.h"
#include "physical.h"
#include <cstdlib>
namespace series_controls {
namespace old = ikea2_reference::seriespack;
namespace {
template <unsigned K, sp::geometry G>
std::vector<std::byte> encode(std::span<const word> input) {
    constexpr auto geometry = G == sp::geometry::local ? old::geometry::local8 : old::geometry::striped;
    using L = sp::layout<K, G>;
    const auto tiles = input.size() / L::tile_rows + (input.size() % L::tile_rows != 0);
    std::vector<word> padded(tiles * L::tile_rows);
    std::copy(input.begin(), input.end(), padded.begin());
    std::vector<std::byte> result(tiles * L::tile_bytes);
    // Same independent prior-wire oracle used by Ikea's maintained wire check.
    // It neither calls today's initializer nor shares its native readers.
    for (std::size_t tile = 0; tile < tiles; ++tile)
        old::detail::encode_tile<K, geometry>(padded.data() + tile * L::tile_rows,
            reinterpret_cast<std::uint8_t*>(result.data()) + tile * L::tile_bytes);
    return result;
}
}
std::vector<std::byte> encode_reference(shape control, std::span<const word> input) {
    std::vector<std::byte> result;
    bool found = false;
    sp::detail::each<64>([&](auto k) {
        if (control.width() != k + 1) return;
        if (control.geometry() == sp::geometry::local) {
            result = encode<k + 1, sp::geometry::local>(input); found = true;
        } else if constexpr (sp::striped_width(k + 1)) {
            result = encode<k + 1, sp::geometry::striped>(input); found = true;
        }
    });
    if (!found) std::abort();
    return result;
}
}
