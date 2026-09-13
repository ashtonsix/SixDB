#include <ikea/bec256.h>
#include <ikea/bec256/author/write.h>
#include "reference.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

namespace bc = ikea::bec256;
unsigned cases = 0;
void check(const bc::plain_block &input) {
    const unsigned population = bec_reference::population(input.data());
    std::array<bc::byte, 64> reference{};
    const unsigned size = bec_reference::encode(input.data(), reference.data());
    auto encoded = bc::prepare(input, population);
    assert(encoded && encoded->bytes() == size);
    assert(std::equal(encoded->body().begin(), encoded->body().end(), reference.begin()));
    std::array<bc::byte, 160> memory;
    memory.fill(bc::byte{0xa5});
    bc::destination target{memory};
    std::array<ikea::owner_write, 2> journal_storage{};
    ikea::source_write_journal journal{journal_storage};
    assert(encoded->write(target, 37, journal));
    assert(std::equal(memory.begin() + 37, memory.begin() + 37 + size, reference.begin()));
    for (unsigned i = 0; i < memory.size(); ++i)
        if (i < 37 || i >= 37 + size)
            assert(memory[i] == bc::byte{0xa5});
    assert(journal.used == (size != 0));
    if (size) {
        assert(journal.entries()[0].source == &target);
        assert(journal.entries()[0].bytes.offset == 37 && journal.entries()[0].bytes.size == size);
    }
    memory.fill(bc::byte{0xa5});
    journal.used = 0;
    const auto direct = bc::encode(input, population, target, 37, journal);
    assert(direct && *direct == size);
    assert(std::equal(memory.begin() + 37, memory.begin() + 37 + size, reference.begin()));
    for (unsigned i = 0; i < memory.size(); ++i)
        if (i < 37 || i >= 37 + size)
            assert(memory[i] == bc::byte{0xa5});
    for (unsigned extent : {size, 64u}) {
        auto source = bc::source::admit(std::span(memory).subspan(37, extent), size, population);
        assert(source);
        bc::plain_block output;
        bc::decode(*source, output);
        assert(output == input);
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
        bc::native::store(output.data(), bc::native::read(*source));
        assert(output == input);
        auto native_encoded =
            bc::native::prepare_unchecked(bc::native::load(input.data()), population);
        assert(std::equal(native_encoded.body().begin(), native_encoded.body().end(),
                          reference.begin()));
#endif
    }
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    memory.fill(bc::byte{0xa5});
    journal.used = 0;
    const auto native_size = bc::native::encode_exact_unchecked(bc::native::load(input.data()),
                                                                population, target, 37, journal);
    assert(native_size == size && journal.used == unsigned(size != 0));
    assert(std::equal(reference.begin(), reference.begin() + size, memory.begin() + 37));
    for (unsigned i = 0; i < memory.size(); ++i)
        if (i < 37 || i >= 37 + size)
            assert(memory[i] == bc::byte{0xa5});
#endif
    if (size && cases % 127 == 0) {
        auto before = memory;
        const auto used = journal.used;
        assert(!encoded->write(target, memory.size() - size + 1, journal));
        assert(memory == before && journal.used == used);
        ikea::source_write_journal empty{};
        assert(!encoded->write(target, 37, empty));
        assert(memory == before && empty.used == 0);
        for (unsigned n = 0; n < size; ++n)
            assert(!bc::validate(std::span(reference).first(n), population));
        assert(!bc::validate(std::span(reference).first(size + 1), population));
    }
    ++cases;
}
void check_analysis();
int main() {
    check_analysis();
    std::mt19937_64 random(0xbec25613);
    bc::plain_block input{};
    for (unsigned position = 0; position < 256; ++position) {
        input.fill(bc::byte{0});
        input[position / 8] = bc::byte{static_cast<unsigned char>(1 << (position % 8))};
        check(input);
        for (auto &b : input)
            b = ~b;
        check(input);
    }
    for (unsigned value = 0; value < 65536; ++value) {
        input.fill(bc::byte{0});
        input[15] = bc::byte(value);
        input[16] = bc::byte(value >> 8);
        check(input);
    }
    for (unsigned p = 0; p <= 256; ++p) {
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
            std::array<unsigned, 256> permutation;
            for (unsigned i = 0; i < 256; ++i)
                permutation[i] = i;
            std::shuffle(permutation.begin(), permutation.end(), random);
            input.fill(bc::byte{0});
            for (unsigned i = 0; i < p; ++i)
                input[permutation[i] / 8] |=
                    bc::byte{static_cast<unsigned char>(1 << (permutation[i] % 8))};
            check(input);
        }
    }
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto *memory = static_cast<bc::byte *>(
        mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0);
    for (auto pattern : {0u, 255u, 15u, 1u}) {
        input.fill(bc::byte(pattern));
        const unsigned population = bec_reference::population(input.data());
        auto encoded = bc::prepare(input, population);
        assert(encoded);
        const auto size = encoded->bytes();
        bc::destination target{{memory, page}};
        std::array<ikea::owner_write, 1> entries;
        ikea::source_write_journal journal{entries};
        assert(encoded->write(target, page - size, journal));
        auto source = bc::source::admit({memory + page - size, size}, size, population);
        assert(source);
        bc::plain_block output;
        bc::decode(*source, output);
        assert(output == input);
    }
    // Analysis and the shortcut read exactly one plain block at a page end.
    std::copy(input.begin(), input.end(), memory + page - 32);
    auto bounded = std::span<const bc::byte, 32>(memory + page - 32, 32);
    assert(bc::estimate_bytes(bounded) == bc::estimate_bytes(input));
    assert(bc::enum_bits(bounded) == bc::enum_bits(input));
    std::array<bc::byte, 64> encoded_memory{};
    bc::destination encoded_target{encoded_memory};
    std::array<ikea::owner_write, 1> encoded_entries{};
    ikea::source_write_journal encoded_effects{encoded_entries};
    assert(bc::encode_if_promising(bounded, bec_reference::population(input.data()), 225,
                                   encoded_target, 0, encoded_effects));
    munmap(memory, page * 2);
    input.fill(bc::byte{0});
    assert(!bc::prepare(input, 1));
    assert(!bc::validate({}, 257));
    auto expect_error = [](const auto &result, bc::error error) {
        assert(!result && result.error() == error);
    };
    std::array<bc::byte, 2> bad{bc::byte{3}, bc::byte{0}};
    expect_error(bc::validate(bad, 2), bc::error::split);
    // Four low bits in one byte: five 3-bit splits followed by one 7-bit
    // rank, hence 22 bits. Rank 127 is invalid among the 70 population-4 bytes.
    input.fill(bc::byte{0});
    input[0] = bc::byte{15};
    auto four = bc::prepare(input, 4);
    assert(four && four->bytes() == 3);
    std::array<bc::byte, 3> malformed;
    std::copy(four->body().begin(), four->body().end(), malformed.begin());
    malformed[2] |= bc::byte{0x40};
    expect_error(bc::validate(malformed, 4), bc::error::padding);
    std::copy(four->body().begin(), four->body().end(), malformed.begin());
    malformed[1] |= bc::byte{0x80};
    malformed[2] |= bc::byte{0x3f};
    expect_error(bc::validate(malformed, 4), bc::error::rank);

    input.fill(bc::byte{0});
    input[0] = bc::byte{1};
    auto singleton = bc::prepare(input, 1);
    assert(singleton);
    std::array<ikea::owner_write, 1> records{};
    ikea::source_write_journal effects{records};
    bc::destination aliases_effects{std::as_writable_bytes(std::span(records))};
    expect_error(bc::encode(input, 1, aliases_effects, 0, effects), bc::error::overlap);
    expect_error(bc::encode_if_promising(input, 1, 225, aliases_effects, 0, effects),
                 bc::error::overlap);
    assert(effects.used == 0 && records[0].source == nullptr);
    bc::destination aliases_self{};
    aliases_self.storage = std::as_writable_bytes(std::span(&aliases_self, 1));
    const auto *original = aliases_self.storage.data();
    expect_error(bc::encode(input, 1, aliases_self, 0, effects), bc::error::overlap);
    assert(effects.used == 0 && aliases_self.storage.data() == original);
    bc::destination aliases_candidate{{const_cast<bc::byte *>(singleton->body().data()), 1}};
    expect_error(singleton->write(aliases_candidate, 0, effects), bc::error::overlap);
    assert(effects.used == 0);
    std::printf("Bec256: %u independent wire/replacement cases and exact guard boundaries passed\n",
                cases);
}
