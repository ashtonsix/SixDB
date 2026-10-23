#include <ikea/seriespack.h>
#include <cassert>

int main() {
    namespace sp = ikea::seriespack;
    using L = sp::preset_layout<20, sp::preset::bulk_x86, 8>;
    constexpr std::size_t count = 512;
    constexpr auto stride = (L::tile_bytes + L::tile_rows + 63) & ~std::size_t{63};
    alignas(64) std::array<ikea::byte, count / L::tile_rows * stride> partition;
    partition.fill(ikea::byte{0xa5});
    // Payload followed by its head, with unrelated stride gaps preserved.
    auto destination = sp::view<L>::attach(
        count, {{{partition, stride}, {std::span(partition).subspan(L::tile_bytes), stride}, {}}});
    assert(destination);
    const auto writer = sp::bind_writer(*destination);
    assert(writer);
    const auto write = writer->erase<std::uint32_t, sp::author::sum_change>();
    std::array<ikea::journal::entry, 256> records;
    ikea::journal effects{records};
    std::array<std::uint32_t, count> input;
    for (std::size_t i = 0; i < count; ++i)
        input[i] = i;
    assert(write.initialize(input, effects));
    effects.used = 0;
    sp::author::sum_change summary;
    assert(write.set(7, 1000, effects, summary));
    ikea::journal no_space{{}};
    const auto delta_before = summary.finish();
    const auto rejected = write.set(7, 123, no_space, summary);
    assert(!rejected && rejected.error() == sp::error::effects);
    assert(summary.finish() == delta_before && !no_space.used);
    assert(write.replace(16, std::span<const std::uint32_t>(input).first(32),
                         effects, sp::selection::all(), summary));
    const auto typed_reader = sp::bind_reader(*destination);
    const auto read = typed_reader.erase<std::uint32_t>();
    assert(read.get(7) == 1000 && read.get(16) == 0);
    assert(!read.get(count));
    std::array<std::uint32_t, 4> output{9, 9, 9, 9};
    assert(!read.read(count - 1, output) && output[0] == 9);
    assert(read.read(16, output) && output[3] == 3);
    std::uint64_t actual = 0;
    for (std::size_t i = 0; i < count; ++i)
        actual += typed_reader.get_unchecked(i);
    assert(actual == count * (count - 1) / 2 + summary.finish());
    // Persist this law once; the parent retains count and plane placement.
    std::array<ikea::byte, sp::layout_value::encoded_size()> encoded{};
    assert(sp::layout_value::of<L>().encode(encoded));
    const auto recovered = sp::layout_value::decode(encoded);
    assert(recovered && sp::view<L>::attach(*recovered, count,
        {{{partition, stride}, {std::span(partition).subspan(L::tile_bytes), stride}, {}}}));
    for (std::size_t tile = 0; tile < count / L::tile_rows; ++tile)
        for (std::size_t offset = L::tile_bytes + L::tile_rows; offset < stride; ++offset)
            assert(partition[tile * stride + offset] == ikea::byte{0xa5});
}
