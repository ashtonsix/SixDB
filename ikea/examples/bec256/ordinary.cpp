#include <ikea/bec256.h>
#include <cassert>

int main() {
    namespace bc = ikea::bec256;
    bc::plain_block bits{};
    bits[3] = bc::byte{0x20}; // Original bit position 29.
    std::array<bc::byte, 128> storage{};
    bc::destination plane{storage};
    std::array<ikea::owner_write, 4> entries;
    ikea::source_write_journal effects{entries};
    unsigned population = 1; // Caller-owned metadata, never inserted into the body.
    auto size = bc::encode(bits, population, plane, 17, effects);
    assert(size && *size == 1);
    assert(storage[17] == bc::byte{0xa7});
    auto input = bc::source::admit(std::span(storage).subspan(17), *size, population);
    assert(input);
    bc::plain_block decoded;
    bc::decode(*input, decoded);
    assert(decoded == bits);
    assert(effects.entries()[0].source == &plane && effects.entries()[0].bytes.offset == 17);
    assert(effects.entries()[0].bytes.size == 1);
    // Analysis estimates exclude the caller's directory and are not bounds.
    const auto estimate = bc::estimate_bytes(bits);
    assert(estimate <= bc::max_bytes);

    // A caller-selected heuristic can avoid an unpromising encode. This value
    // illustrates the mechanism; choosing/calibrating a cutoff is owner policy.
    bc::plain_block dense;
    dense.fill(bc::byte{0x55});
    constexpr unsigned cutoff = 144; // Enumerative bits, not total body bytes.
    const auto before = storage;
    const auto used = effects.used;
    assert(bc::enum_bits(dense) == 224);
    auto attempt = bc::encode_if_promising(dense, 128, cutoff, plane, 17, effects);
    assert(attempt && !*attempt); // No error; encoding was declined.
    assert(storage == before && effects.used == used);
    // The owner may now choose a raw bitset or another representation, recording
    // that choice and its effects itself. Ikea has emitted no implicit raw form.
    // The encoder neither overwrites neighbouring bodies nor clears unused
    // capacity. Replacing this body requires keeping its metadata coherent too.
}
