#include <ikea/seriespack.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace pack = ikea::seriespack;

int main() {
    // Values fit 12 bits (0..4095); uint16_t is their input/output carrier.
    using Format = pack::format<12>;
    std::array<std::uint16_t, 10> values{7, 19, 42, 0, 4095, 3, 8, 91, 12, 6};
    // Two complete tiles: 24 bytes, with six unused positions initialized to zero.
    alignas(64) std::array<std::byte, 2 * Format::payload::tile_bytes> bytes{};
    auto attached = pack::static_mutable_view<Format>::attach(
        values.size(), {{bytes, Format::payload::tile_bytes}, {}});
    if (!attached) return 1;
    // Both views alias bytes; readable observes later writes through writable.
    auto writable = attached->as_dynamic();
    auto readable = writable.as_const();
    if (!pack::encode(writable, std::span{values})) return 2;

    // In [2,10), mask bits 0,3,6 name original positions 2,5,8. Replacement
    // slots 0,3,6 supply 100,200,300; the input is not compacted to three values.
    constexpr pack::index_range rows{2, 10};
    const std::array<std::uint64_t, 1> mask{0b01001001};
    const auto selected = pack::selection::bitmap(rows.begin, rows.size(), mask);
    const std::array<std::uint16_t, 8> replacements{100, 0, 0, 200, 0, 0, 300, 0};
    const auto capacity = pack::write_effect_capacity(readable, rows, selected);
    if (!capacity) return 3;
    // Effects cover physical writes, including shared tail bytes; spans may overlap.
    std::vector<pack::byte_span> coverage(*capacity);
    pack::effect_output effects{coverage};
    if (!pack::write(writable, rows, std::span{replacements}, selected, &effects)) return 4;

    auto reader = pack::bind_reader(readable);
    if (!reader) return 5;
    std::array<std::uint16_t, 10> decoded{};
    // Trusted call: valid range, sufficient/disjoint output, and live stable bytes.
    reader->decode({0, values.size()}, std::span{decoded});
    if (decoded[2] != 100 || decoded[5] != 200 || decoded[8] != 300 ||
        decoded[4] != values[4]) return 6;
    std::printf("SeriesPack: %zu values, %zu stored bytes, %zu mutation spans\n",
                decoded.size(), bytes.size(), effects.size);
}
