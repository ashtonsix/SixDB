#include <ikea/bec256/author/write.h>
#include <ikea/bec256/author/analysis.h>
#include <cassert>
#include <cstdio>

int main() {
    namespace bc = ikea::bec256;
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    bc::plain_block a{}, b{};
    a[3] = bc::byte{0x20}; // Position 29.
    b[1] = bc::byte{2};
    b[25] = bc::byte{1}; // Positions 9 and 200.
    std::array<bc::byte, 128> input_storage{};
    bc::destination input_plane{input_storage};
    std::array<ikea::owner_write, 2> records;
    ikea::source_write_journal effects{records};
    auto a_size = bc::encode(a, 1, input_plane, 0, effects);
    auto b_size = bc::encode(b, 2, input_plane, 64, effects);
    assert(a_size && b_size);
    auto source_a = bc::source::admit(std::span(input_storage).first(*a_size), *a_size, 1);
    auto source_b = bc::source::admit(std::span(input_storage).subspan(64, *b_size), *b_size, 2);
    assert(source_a && source_b);

    std::array<bc::byte, 64> mask{};
    mask[3] = bc::byte{0x20};
    mask[32 + 25] = bc::byte{1};
    auto bits = bc::native::intersection(bc::native::read_pair(*source_a, *source_b),
                                         bc::native::load_pair(mask.data()));
    const auto pa = bc::native::population(bc::native::part<0>(bits));
    const auto pb = bc::native::population(bc::native::part<1>(bits));
    // Analysis consumes the same register values. Each half has its own estimate;
    // these exclude metadata and cannot be used as exact capacity grants.
    const auto estimates = bc::native::estimate_bytes(bits);
    assert(estimates[0] <= bc::max_bytes && estimates[1] <= bc::max_bytes);

    // Independent owner grants cover any encoded body; no materialized plain
    // bitset is needed between reading, filtering, population and exact writing.
    std::array<bc::byte, bc::max_bytes> storage_a{}, storage_b{};
    bc::destination output_a{storage_a}, output_b{storage_b};
    // Input construction above was private scratch; its records need no owner
    // publication. Discard those records before using this journal for outputs.
    effects.used = 0; // Two disjoint grants and two effect slots are already held.
    auto lengths =
        bc::native::encode_pair_exact_unchecked(bits, pa, output_a, 0, pb, output_b, 0, effects);
    assert(pa == 1 && pb == 1 && lengths.first == 1 && lengths.second == 1);
    assert(effects.used == 2 && effects.entries()[0].source == &output_a &&
           effects.entries()[1].source == &output_b);
    // The owner retains each population/length/address and publishes them with
    // the body and dependent summaries under its enclosing integration protocol.
    auto result = bc::source::admit(storage_b, lengths.second, pb);
    assert(result);
    bc::plain_block decoded;
    bc::decode(*result, decoded);
    b[1] = bc::byte{0};
    assert(decoded == b);
#else
    std::puts("Bec256 native example requires AVX-512 or NEON.");
#endif
}
