#include <ikea/bec.h>
#include <ikea/tuplepack.h>
#include <cassert>

// A small owner adapter: actual leases, isolation, publication and UFFD COW
// belong to Loom/Engine/Orbital. No reader observes the interval inside apply.
int main() {
    namespace bc = ikea::bec256;
    namespace tp = ikea::tuplepack;
    std::array<bc::byte, 256> storage{};
    bc::view body{storage};
    // Caller-defined directory: population low byte, its ninth bit, length,
    // then a two-byte address. This schema is not part of Bec256's format.
    auto format = tp::layout::make(
        4, std::array<tp::code, 5>{{{0, 0, 8}, {1, 0, 1}, {1, 1, 6}, {2, 0, 8}, {3, 0, 8}}});
    assert(format);
    std::array<std::uint8_t, 4> metadata{};
    auto view = tp::view::attach(*format, std::as_writable_bytes(std::span{metadata}), 1, 4);
    assert(view);
    const std::array<std::uint8_t, 5> map{0, 1, 2, 3, 4};
    auto writer = tp::write_plan<8>::make(*format, map);
    assert(writer);
    auto directory = tp::bind_writer(*writer, *view);
    assert(directory);
    std::array<ikea::journal::entry, 8> entries;
    ikea::journal effects{entries};
    unsigned visible_population = 0, visible_size = 0, visible_offset = 0, generation = 0;
    std::int64_t count_delta = 0;
    std::array<std::byte, 32> replacement;
    replacement.fill(bc::byte{0x0f});
    auto candidate = bc::encoding::make(replacement, 128);
    assert(candidate);
    // This owner chooses a new region. The prepared candidate can survive the
    // acquisition wait by value; logical body length does not define capacity.
    const unsigned new_offset = 64;
    const std::uint64_t descriptor =
        128 | (std::uint64_t(candidate->bytes()) << 16) | (std::uint64_t(new_offset) << 24);
    assert(candidate->admit_write(body, new_offset, effects));
    // Reserve both children's upper bounds before either writes. Their checked
    // calls cannot reject under these fixed, stable inputs and proven geometry.
    assert(effects.remaining() >= 2);
    assert(directory->admit(0, std::span(&descriptor, 1), tp::selection::all(),
                            effects.remaining() - 1));
    candidate->write_unchecked(body, new_offset, effects);
    assert(directory->set(0, descriptor, effects));
    count_delta += 128 - visible_population;
    visible_population = 128;
    visible_size = candidate->bytes();
    visible_offset = new_offset;
    ++generation; // Models coordinated visibility of body + interpretation.
    auto read = bc::bind_reader(std::span(storage).subspan(visible_offset), visible_size,
                                  visible_population);
    assert(read && generation == 1 && count_delta == 128);
    std::array<std::byte, 32> output;
    bc::decode(*read, output);
    assert(output == replacement);
    // After this completed frontier the owner may suspend, retaining bytes,
    // named views, generation, journal and pending summary/publication state.
    assert(effects.used == 2 && effects.entries()[0].source == &body &&
           effects.entries()[1].source == &*view);
}
