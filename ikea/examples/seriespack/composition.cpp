#include <ikea/seriespack.h>
#include <ikea/seriespack/author/read.h>
#include <cassert>
#include <vector>

int main() {
    namespace sp = ikea::seriespack;
    namespace cp = sp::composition;
    using Parent = sp::format<12, sp::geometry::local, 8>;
    using Child = sp::format<4, sp::geometry::striped>;
    constexpr std::size_t count = 512;
    alignas(64) std::array<std::uint8_t, count / 2> parent_payload{}, child_payload{};
    std::array<std::uint8_t, count> heads{};
    const auto parent = sp::view<Parent, std::uint8_t>::attach(
        count, {{{parent_payload, Parent::tile_bytes}, {heads, Parent::tile_rows}, {}}});
    const auto child = sp::view<Child, std::uint8_t>::attach(
        count, {{{child_payload, Child::tile_bytes}, {}, {}}});
    assert(parent && child);

    // A complete nested value expression replaces the parent's residual child.
    // Its physical tiling changes from Local8 to Striped64; heads keep theirs.
    const auto original = cp::describe(*parent);
    const auto expression =
        cp::with_payload(original, cp::with_tail(original.payload, cp::describe(*child)));
    const auto writer = cp::prepare_mutation(expression, count);
    assert(writer);
    const auto needed = writer->construction_effect_capacity();
    assert(needed);
    std::vector<cp::owner_write> records(*needed);
    cp::write_journal effects{records};
    std::array<std::uint16_t, count> values;
    for (std::size_t i = 0; i < count; ++i)
        values[i] = i;
    assert(writer->initialize(std::span<const std::uint16_t>(values), effects));
    for (const auto& effect : effects.entries())
        assert((effect.source == &*parent && effect.bytes.plane == 1) || effect.source == &*child);
    // The retired residual bytes have no role in reads, writes or effects.
    for (auto byte : parent_payload)
        assert(byte == 0);
    const auto reader = cp::prepare(expression, count);
    assert(reader);
    const auto sum = reader->sum(0, count, 100, [](std::size_t) { return 0xffff; });
    assert(sum == 99 * 100 / 2);
}
