#include <ikea/bec.h>
#include <cassert>

int main() {
    namespace bc = ikea::bec4096;
    ikea::bitset4096 bits;
    for (unsigned position : {29u, 300u, 301u, 4095u})
        bits.set(position);
    std::array<bc::byte, 64> storage{};
    bc::view plane{storage};
    std::array<ikea::journal::entry, 1> entries;
    ikea::journal effects{entries};
    unsigned population = 4; // Caller-owned, as with Bec256: one per 4096 positions.
    auto size = bc::encode(bits, population, plane, 3, effects);
    // Six bytes: the head, the length table and three short block streams.
    assert(size && *size == 6);
    assert(effects.entries()[0].offset == 3 && effects.entries()[0].size == *size);

    // Binding validates once and keeps the decoded head: every block's
    // population and stream position. Later reads never reread it.
    auto input = bc::bind_reader(std::span(storage).subspan(3), *size, population);
    assert(input);
    assert(input->population(0) == 1 && input->population(1) == 2 && input->population(15) == 1);

    // One 256-position block, without touching the other fifteen streams.
    ikea::bitset256 block;
    bc::decode(*input, 1, block);
    assert(block.contains(300 - 256) && block.contains(301 - 256) && block.count() == 2);

    // Counts within a block come from its count tree alone; byte 5 holds 300, 301.
    std::array<std::uint8_t, 32> counts;
    bc::counts(*input, 1, counts);
    assert(counts[5] == 2 && counts[4] == 0);
    assert(bc::rank(*input, 301) == 2);              // 29 and 300
    assert(bc::population(*input, 256, 256) == 2);  // block 1
    assert(bc::population(*input, 4000, 96) == 1);  // 4095

    ikea::bitset4096 decoded;
    bc::decode(*input, decoded);
    assert(decoded == bits);
}
