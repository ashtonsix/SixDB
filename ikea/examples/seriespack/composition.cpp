#include <ikea/seriespack.h>
#include <ikea/seriespack/reader.h>
#include <cassert>
#include <vector>

int main() {
    namespace sp = ikea::seriespack;
    namespace cp = sp::author;
    using Parent = sp::layout<12, sp::geometry::local, 8>;
    using Child = sp::layout<4, sp::geometry::striped>;
    constexpr std::size_t count = 512;
    alignas(64) std::array<ikea::byte, count / 2> parent_payload{}, child_payload{};
    std::array<ikea::byte, count> heads{};
    const auto parent = sp::view<Parent>::attach(
        count, {{{parent_payload, Parent::tile_bytes}, {heads, Parent::tile_rows}, {}}});
    const auto child = sp::view<Child>::attach(
        count, {{{child_payload, Child::tile_bytes}, {}, {}}});
    assert(parent && child);

    // A complete nested value expression replaces the parent's residual child.
    // Its physical tiling changes from Local8 to Striped64; heads keep theirs.
    const auto original = cp::expression(*parent);
    const auto expression =
        cp::with_payload(original, cp::with_tail(original.payload, cp::expression(*child)));
    const auto writer = sp::bind_writer(expression, count);
    assert(writer);
    const auto needed = writer->effect_capacity_initialize();
    assert(needed);
    std::vector<ikea::journal::entry> records(*needed);
    ikea::journal effects{records};
    std::array<std::uint16_t, count> values;
    for (std::size_t i = 0; i < count; ++i)
        values[i] = i;
    assert(writer->initialize(std::span<const std::uint16_t>(values), effects));
    for (const auto& effect : effects.entries())
        assert((effect.source == &*parent && effect.plane == 1) || effect.source == &*child);
    // The retired residual bytes have no role in reads, writes or effects.
    for (auto byte : parent_payload)
        assert(byte == ikea::byte{});
    const auto reader = sp::bind_reader(expression, count);
    assert(reader);
    const auto sum = reader->sum(0, count, 100, [](std::size_t) { return 0xffff; });
    assert(sum == 99 * 100 / 2);
}
