#include <ikea/seriespack.h>
#include <cassert>

int main() {
    namespace sp = ikea::seriespack;
    using F = sp::preset_format<20, sp::preset::bulk_x86, 8>;
    constexpr std::size_t count = 512;
    constexpr auto stride = (F::tile_bytes + F::tile_rows + 63) & ~std::size_t{63};
    alignas(64) std::array<std::uint8_t, count / F::tile_rows * stride> partition;
    partition.fill(0xa5);
    // Tile payload followed by its head; both planes share this partition.
    // Unoccupied stride gaps can belong to another block.
    auto destination = sp::view<F, std::uint8_t>::attach(
        count, {{{partition, stride}, {std::span(partition).subspan(F::tile_bytes), stride}, {}}});
    assert(destination);
    // These objects and byte owners outlive the borrowed operation.
    const auto prepared = sp::bind_mutation(*destination);
    assert(prepared);
    const auto write = prepared->erase<std::uint32_t, sp::sum_change>();
    std::array<sp::composition::owner_write, 256> records;
    sp::composition::write_journal effects{records};
    std::array<std::uint32_t, count> input;
    for (std::size_t i = 0; i < count; ++i)
        input[i] = i;
    assert(write.initialize(input, effects));
    effects.used = 0;
    sp::sum_change summary;
    assert(write.set(7, 1000, summary, effects));
    sp::composition::write_journal no_space{{}};
    const auto delta_before = summary.finish();
    const auto rejected = write.set(7, 123, summary, no_space);
    assert(!rejected && rejected.error() == sp::error::capacity);
    assert(summary.finish() == delta_before && !no_space.used);
    assert(write.replace(16, std::span<const std::uint32_t>(input).first(32),
                         sp::row_selection::all(), summary, effects));
    const auto reader = sp::bind_decoder<std::uint32_t>(*destination);
    assert(reader.get(7) == 1000 && reader.get(16) == 0);
    assert(!reader.get(count));
    std::array<std::uint32_t, 4> output{9, 9, 9, 9};
    assert(!reader.read(count - 1, output) && output[0] == 9);
    assert(reader.read(16, output) && output[3] == 3);
    std::uint64_t actual = 0;
    for (std::size_t i = 0; i < count; ++i)
        actual += reader.get_unchecked(i);
    assert(actual == count * (count - 1) / 2 + summary.finish());
    // Engine retains the plane-owner/offset mapping beside this descriptor.
    const auto physical = sp::encode_descriptor(sp::describe_storage(*destination));
    assert(physical);
    assert(sp::decode_descriptor(*physical));
    for (std::size_t tile = 0; tile < count / F::tile_rows; ++tile)
        for (std::size_t byte = F::tile_bytes + F::tile_rows; byte < stride; ++byte)
            assert(partition[tile * stride + byte] == 0xa5);
}
