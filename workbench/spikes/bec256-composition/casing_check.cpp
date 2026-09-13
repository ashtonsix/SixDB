#include "casing.h"
#include "../../../ikea/test/bec256/reference.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

namespace bc = ikea::bec256;
int main() {
#if defined(IKEA_BEC256_AVX512)
    // Exercise the assembly law independently of which group widths Bec256's
    // current trees happen to produce, especially runs of zero-width groups.
    std::mt19937_64 group_rng(0xbec8);
    for (unsigned trial = 0; trial < 10000; ++trial) {
        alignas(64) std::uint64_t values[8], widths[8];
        std::array<bc::byte, 64> reference{}, output{};
        unsigned bit = 0;
        for (unsigned i = 0; i < 8; ++i) {
            widths[i] = trial < 8 ? (i == trial ? 56 : 0) : group_rng() % 57;
            values[i] = group_rng() & ((std::uint64_t{1} << widths[i]) - 1);
            for (unsigned j = 0; j < widths[i]; ++j, ++bit)
                reference[bit / 8] |= bc::byte(((values[i] >> j) & 1) << (bit % 8));
        }
        _mm512_storeu_si512(output.data(), bc::detail::assemble(_mm512_load_si512(values),
                                                                _mm512_load_si512(widths)));
        assert(output == reference);
    }
#endif
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    auto *memory = static_cast<bc::byte *>(
        mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0);
    bc::destination target{{memory, page}};
    std::array<ikea::owner_write, 1> entries;
    ikea::source_write_journal effects{entries};
    auto bound = bec_study::bound_encoder::bind(target, effects);
    assert(bound);
    std::mt19937_64 rng(0xbece);
    unsigned cases = 0;
    for (unsigned population = 0; population <= 256; ++population) {
        for (unsigned trial = 0; trial < 16; ++trial) {
            std::array<unsigned, 256> order;
            for (unsigned i = 0; i < 256; ++i)
                order[i] = i;
            std::shuffle(order.begin(), order.end(), rng);
            bc::plain_block input{};
            for (unsigned i = 0; i < population; ++i)
                input[order[i] / 8] |= bc::byte(1u << (order[i] % 8));
            std::array<bc::byte, 64> reference{};
            const auto size = bec_reference::encode(input.data(), reference.data());
            for (unsigned method = 0; method < 4; ++method) {
                auto run = [&](std::span<const bc::byte, 32> value, unsigned pop,
                               std::size_t offset) {
                    if (method == 0)
                        return bc::encode(value, pop, target, offset, effects);
                    if (method == 1)
                        return bec_study::staged_encode(value, pop, target, offset, effects);
                    if (method == 3)
                        return bc::native::encode(bc::native::load(value.data()), pop, target,
                                                  offset, effects);
                    return bound->encode(value, pop, offset);
                };
                auto check = [&](std::span<const bc::byte, 32> value, std::size_t offset) {
                    effects.used = 0;
                    const auto n = run(value, population, offset);
                    assert(n && *n == size);
                    assert(
                        std::equal(reference.begin(), reference.begin() + size, memory + offset));
                    assert(effects.used == unsigned(size != 0));
                    if (size) {
                        assert(effects.entries()[0].source == &target);
                        assert(effects.entries()[0].bytes.offset == offset);
                        assert(effects.entries()[0].bytes.size == size);
                    }
                };
                std::fill(memory, memory + page, bc::byte{0xa5});
                check(input, page - size);
                for (unsigned i = 0; i < page - size; ++i)
                    assert(memory[i] == bc::byte{0xa5});
                auto source = bc::source::admit({memory + page - size, size}, size, population);
                assert(source);
                bc::plain_block decoded;
                bc::decode(*source, decoded);
                assert(decoded == input);
                bc::native::store(decoded.data(), bc::native::read(*source));
                assert(decoded == input);
                // Input/destination byte overlap remains supported by all three.
                std::copy(input.begin(), input.end(), memory + 7);
                check(std::span<const bc::byte, 32>(memory + 7, 32), 13);
                std::array<bc::byte, 64> before;
                std::copy(memory, memory + 64, before.begin());
                const auto used = effects.used;
                assert(!run(input, population ^ 1u, 13));
                assert(effects.used == used && std::equal(before.begin(), before.end(), memory));
                if (size) {
                    effects.used = 0;
                    assert(!run(input, population, page - size + 1));
                    assert(effects.used == 0);
                    effects.used = effects.storage.size();
                    assert(!run(input, population, 13));
                    assert(effects.used == effects.storage.size());
                    assert(std::equal(before.begin(), before.end(), memory));
                }
                ++cases;
            }
        }
    }
    munmap(memory, 2 * page);
    std::printf(
        "Bec256 casing: %u exact guarded writes, overlap, failure and bounded-read cases passed\n",
        cases);
}
