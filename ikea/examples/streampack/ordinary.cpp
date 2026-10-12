#include <array>
#include <cassert>
#include <ikea/streampack.h>

int main() {
    namespace sp = ikea::streampack;
    const auto format = sp::layout::prepare(
        {sp::geometry::width, sp::placement::grouped, 128, 255, 4096, {}, false, false});
    assert(format);
    std::array<std::byte, 4096> payload;
    std::array<std::byte, 32> directory;            // 16 tombstone bytes + eight u16 checkpoints
    alignas(64) std::array<std::byte, 256> lengths; // Room for 128 u8 R/U pairs
    std::array<std::byte, 6> control;               // Count + occupied extent, both little endian
    auto bound = sp::view::bind(*format, {payload, directory, lengths, control});
    assert(bound);
    auto& block = *bound; // Stable identity for the source-qualified journal.
    std::array<ikea::owner_write, 32> writes;
    ikea::source_write_journal effects{writes};
    assert(sp::initialize(block, effects));
    const std::array greeting{std::byte{'h'}, std::byte{'i'}};
    assert(sp::append(block, greeting, 4, effects));
    assert(sp::append(block, {}, 0, effects)); // A live empty value still gets a rank.
    assert(block.size() == 2 && sp::value(block, 1)->empty());
    assert(sp::reverse(block, 3)->rank == 0); // Slack belongs to the reservation.
    effects.used = 0; // A real owner resolves/publishes the preceding afterimages first.
    assert(sp::retire(block, 0, effects));
    assert(sp::reverse(block, 3)->tombstone);
    assert(sp::soft_pack(block, false, effects));
    assert(block.size() == 2 && block.extent() == 0 && !sp::reverse(block, 0));
    assert(block.validate());
}
