#include <ikea/bec256.h>
#include "reference.h"
#include <algorithm>
#include <array>
#include <cassert>

void check_block_values() {
    namespace bc = ikea::bec256;
    using byte = std::byte;
    // Placement admission must not pretend malformed contents were validated.
    const std::array malformed{byte{3}, byte{0}};
    const auto placed = bc::const_view::attach(malformed, 2, 2);
    assert(placed);
    const auto invalid = placed->validate();
    const auto refused = bc::bind_reader(*placed);
    assert(!invalid && invalid.error() == bc::error::split);
    assert(!refused && refused.error() == bc::error::split);
    assert(!bc::const_view::attach(malformed, 3, 2));
    for (unsigned seed = 0; seed < 272; ++seed) {
        ikea::bitset256 input, opposite;
        std::array<byte, 32> canonical{};
        unsigned population = 0;
        for (unsigned position = 0; position < 256; ++position) {
            const bool set = seed < 256 ? position == seed
                              : seed == 256 ? false
                              : seed == 257 ? true
                              : (position * 37 + seed * 19) % 29 < seed - 257;
            if (set) {
                input.words[position / 64] |= std::uint64_t{1} << (position % 64);
                canonical[position / 8] |= byte(1u << (position % 8));
                ++population;
            }
            if (!set)
                opposite.words[position / 64] |= std::uint64_t{1} << (position % 64);
        }
        std::array<byte, 47> expected;
        const auto expected_size = bec_reference::encode(canonical.data(), expected.data());
        const auto prepared = bc::encoding::make(input, population);
        assert(prepared && prepared->bytes() == expected_size);
        assert(std::equal(prepared->body().begin(), prepared->body().end(), expected.begin()));
        assert(prepared->effect_capacity() == unsigned(expected_size != 0));
        assert(!bc::encoding::make(input, population == 256 ? 255 : population + 1));

        std::array<byte, 160> memory;
        memory.fill(byte{0xa5});
        bc::view destination{memory};
        std::array<ikea::journal::entry, 2> records{};
        ikea::journal effects{records};
        const auto a = bc::encode(input, population, destination, 13, effects);
        const auto b = bc::encode(opposite, 256 - population, destination, 83, effects);
        assert(a && b && *a == expected_size);
        for (unsigned i = 0; i < memory.size(); ++i)
            if (!(i >= 13 && i < 13 + *a) && !(i >= 83 && i < 83 + *b))
                assert(memory[i] == byte{0xa5});
        for (const auto& record : effects.entries())
            assert(record.source == &destination && record.plane == 0);

        const auto first = bc::bind_reader(std::span(memory).subspan(13, *a), *a, population);
        const auto second = bc::bind_reader(std::span(memory).subspan(83, *b), *b, 256 - population);
        assert(first && second);
        ikea::bitset256 decoded;
        bc::decode(*first, decoded);
        assert(decoded == input);
        std::array<ikea::bitset256, 2> pair;
        bc::decode_pair(*first, *second, pair);
        assert(pair[0] == input && pair[1] == opposite);

        // Consume numeric position windows across word boundaries. The oracle
        // reads canonical input bits directly, without the block's mapping API.
        for (unsigned origin : {0u, 63u, 127u, 192u}) {
            const auto selected = decoded.mask(origin, 64);
            unsigned actual = 0, expected_sum = 0;
            for (unsigned r = 0; r < 64; ++r) {
                actual += unsigned((selected >> r) & 1) * (r + 1);
                const unsigned position = origin + r;
                expected_sum += ((std::to_integer<unsigned>(canonical[position / 8]) >>
                                  (position % 8)) & 1) * (r + 1);
            }
            assert(actual == expected_sum);
        }
    }
}
