#include <ikea/seriespack/native_avx2.h>
#include <ikea/seriespack/native_avx512.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#if defined(__AVX2__)
namespace {

using ikea::seriespack::geometry;
constexpr std::uint8_t canary = 0xa9;
std::uint64_t random_state = 0x823964fabc7015edULL;
std::size_t checked_cases = 0;
unsigned checked_configurations = 0;

std::uint64_t random_value() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

[[noreturn]] void fail(const char* operation, unsigned w, geometry g,
                       unsigned input_bytes, unsigned isa, std::size_t index) {
    std::fprintf(stderr, "%s: W=%u geometry=%u input=%u ISA=%u at %zu\n",
                 operation, w, static_cast<unsigned>(g), input_bytes, isa, index);
    std::abort();
}

class GuardedPage {
public:
    const std::size_t size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    std::uint8_t* const mapping = static_cast<std::uint8_t*>(
        ::mmap(nullptr, 3 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    std::uint8_t* const bytes = mapping == MAP_FAILED ? nullptr : mapping + size;
    GuardedPage() {
        if (mapping == MAP_FAILED || ::mprotect(bytes, size, PROT_READ | PROT_WRITE) != 0) {
            std::perror("guarded page");
            std::abort();
        }
    }
    ~GuardedPage() { ::munmap(mapping, 3 * size); }
    GuardedPage(const GuardedPage&) = delete;
    GuardedPage& operator=(const GuardedPage&) = delete;
};

struct BitAddress { std::size_t byte; unsigned bit; };

// Direct transcription of the wire's individual bit coordinates. No native
// helper, residual-field plan or scalar production encoder supplies the oracle.
template<unsigned W, geometry G>
BitAddress bit_address(unsigned index, unsigned bit) {
    constexpr unsigned q = W / 8, r = W % 8;
    if (bit >= r) {
        std::size_t offset = index * q;
        if constexpr (G == geometry::striped) {
            const auto group = index / 32, lane = index % 32;
            if constexpr (W == 10) offset = (group + (group >= 2)) * 32 + lane;
            else if constexpr (W == 12) offset = group * 32 + lane;
            else if constexpr (W == 14 || W == 15) offset = group * 64 + lane;
            else if constexpr (W == 20) offset = group * 96 + lane * 2;
        }
        return {offset + (bit - r) / 8, (bit - r) % 8};
    }
    if constexpr (G == geometry::local8) return {8 * q + bit, index};
    else {
        const auto group = index / 32, lane = index % 32;
        unsigned position;
        if constexpr (r == 3) {
            constexpr unsigned map[8][3] = {
                {0,1,2}, {3,4,5}, {6,7,14}, {8,9,10},
                {11,12,13}, {22,23,15}, {16,17,18}, {19,20,21}};
            position = map[group][bit];
        } else if constexpr (r == 6) {
            constexpr unsigned map[4][6] = {
                {0,1,2,3,4,5}, {8,9,10,11,6,7},
                {12,13,14,15,22,23}, {16,17,18,19,20,21}};
            position = map[group][bit];
        } else {
            const auto start = group * r, room = 8 - start % 8;
            if (r <= room) position = start + bit;
            else {
                const unsigned low = r - room;
                position = bit < low ? (start / 8 + 1) * 8 + bit : start + bit - low;
            }
        }
        const auto stripe = position / 8;
        std::size_t offset;
        if constexpr (W < 8) offset = stripe * 32;
        else if constexpr (W == 14 || W == 15) offset = 32 + stripe * 64;
        else offset = 64;
        return {offset + lane, position % 8};
    }
}

template<unsigned W, geometry G, class Input, class Output, unsigned Isa>
struct Codec {
    static constexpr unsigned vector_bytes = Isa / 8;
    static constexpr unsigned lanes = G == geometry::local8
        ? std::min(8u, vector_bytes / static_cast<unsigned>(sizeof(Output)))
        : vector_bytes / sizeof(Output);
    static constexpr unsigned count = ikea::seriespack::payload_layout<W, G>::tile_values;
    static constexpr unsigned bytes = ikea::seriespack::payload_layout<W, G>::tile_bytes;

    [[gnu::noinline]] static void encode(const Input* input, std::uint8_t* output) {
        if constexpr (Isa == 256) ikea::seriespack::avx2::encode_low_tile<W, G>(input, output);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else ikea::seriespack::avx512::encode_low_tile<W, G>(input, output);
#endif
    }
    [[gnu::noinline]] static void decode(const std::uint8_t* input, Output* output) {
        if constexpr (Isa == 256) ikea::seriespack::avx2::decode_tile<W, G>(input, output);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else ikea::seriespack::avx512::decode_tile<W, G>(input, output);
#endif
    }
    [[gnu::noinline]] static void fragments(const std::uint8_t* input, std::uint8_t* output) {
        [&]<std::size_t... Part>(std::index_sequence<Part...>) {
            ([&] {
                if constexpr (Isa == 256) {
                    const auto v = ikea::seriespack::avx2::read_fragment<W, G, sizeof(Output), Part * lanes>(input);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + Part * vector_bytes), v);
                }
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                else {
                    const auto v = ikea::seriespack::avx512::read_fragment<W, G, sizeof(Output), Part * lanes>(input);
                    _mm512_storeu_si512(output + Part * vector_bytes, v);
                }
#endif
            }(), ...);
        }(std::make_index_sequence<count / lanes>{});
    }
    [[gnu::noinline]] static void mixed_fragments(
        const std::uint8_t* body, const std::uint8_t* tail, std::uint8_t* output) {
        [&]<std::size_t... Part>(std::index_sequence<Part...>) {
            ([&] {
                if constexpr (Isa == 256) {
                    const auto v = ikea::seriespack::avx2::join<sizeof(Output), W % 8>(
                        ikea::seriespack::avx2::read_body<W, G, sizeof(Output), Part * lanes>(body),
                        ikea::seriespack::avx2::read_tail<W, G, sizeof(Output), Part * lanes>(tail));
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + Part * vector_bytes), v);
                }
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                else {
                    const auto v = ikea::seriespack::avx512::join<sizeof(Output), W % 8>(
                        ikea::seriespack::avx512::read_body<W, G, sizeof(Output), Part * lanes>(body),
                        ikea::seriespack::avx512::read_tail<W, G, sizeof(Output), Part * lanes>(tail));
                    _mm512_storeu_si512(output + Part * vector_bytes, v);
                }
#endif
            }(), ...);
        }(std::make_index_sequence<count / lanes>{});
    }
    [[gnu::noinline]] static void groups(const std::uint8_t* input, unsigned lane, std::uint8_t* output)
        requires (G == geometry::striped) {
        [&]<std::size_t... Group>(std::index_sequence<Group...>) {
            ([&] {
                if constexpr (Isa == 256) {
                    const auto v = ikea::seriespack::avx2::read_group<W, sizeof(Output), Group>(input, lane);
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output + Group * vector_bytes), v);
                }
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                else {
                    const auto v = ikea::seriespack::avx512::read_group<W, sizeof(Output), Group>(input, lane);
                    _mm512_storeu_si512(output + Group * vector_bytes, v);
                }
#endif
            }(), ...);
        }(std::make_index_sequence<count / 32>{});
    }
    [[gnu::noinline]] static void encode_run(const Input* input, std::uint8_t* output, std::size_t tiles) {
        if constexpr (Isa == 256) ikea::seriespack::avx2::encode_low_tiles<W, G>(input, output, tiles);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else ikea::seriespack::avx512::encode_low_tiles<W, G>(input, output, tiles);
#endif
    }
    [[gnu::noinline]] static void decode_run(const std::uint8_t* input, Output* output, std::size_t tiles) {
        if constexpr (Isa == 256) ikea::seriespack::avx2::decode_tiles<W, G>(input, output, tiles);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else ikea::seriespack::avx512::decode_tiles<W, G>(input, output, tiles);
#endif
    }
};

template<unsigned W, geometry G, class Input, unsigned Isa>
void check() {
    using Natural = ikea::seriespack::scalar_for_width<W>;
    using Output = std::conditional_t<(sizeof(Input) > sizeof(Natural)), Input, Natural>;
    using C = Codec<W, G, Input, Output, Isa>;
    constexpr unsigned T = C::count, B = C::bytes;
    constexpr auto value_mask = [] {
        if constexpr (W == 64) return ~std::uint64_t{0};
        else return (std::uint64_t{1} << W) - 1;
    }();
    ++checked_configurations;

    const auto reference = [](const Input* values, std::uint8_t* packed) {
        std::fill_n(packed, B, 0);
        for (unsigned i = 0; i < T; ++i) {
            for (unsigned bit = 0; bit < W; ++bit) {
                const auto where = bit_address<W, G>(i, bit);
                packed[where.byte] |= static_cast<std::uint8_t>(
                    ((static_cast<std::uint64_t>(values[i]) >> bit) & 1) << where.bit);
            }
        }
    };
    const auto verify_values = [&](const Input* expected, const Output* actual, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            if (static_cast<std::uint64_t>(actual[i]) != (static_cast<std::uint64_t>(expected[i]) & value_mask))
                fail("decode value", W, G, sizeof(Input), Isa, i);
        ++checked_cases;
    };
    const auto verify_encoded = [&](const std::uint8_t* expected, std::span<const std::uint8_t> actual,
                                    unsigned start, unsigned bytes) {
        for (unsigned i = 0; i < actual.size(); ++i) {
            const auto value = i >= start && i < start + bytes ? expected[i - start] : canary;
            if (actual[i] != value) fail("encode value/extent", W, G, sizeof(Input), Isa, i);
        }
        ++checked_cases;
    };

    alignas(64) std::array<Input, T> values;
    std::array<Input, T> tail_values;
    std::array<std::uint8_t, B + 1> expected;
    std::array<std::uint8_t, B + 1> tail_encoded;
    std::array<std::uint8_t, B + 128> packed;
    std::array<Output, T + 32> decoded;
    std::array<std::uint8_t, C::count / C::lanes * C::vector_bytes> fragments;
    // Check all byte starts, full source ranges (including bits above W), and
    // low/high alternating patterns. Decoding reads the independent oracle.
    for (unsigned offset = 0; offset < 64; ++offset) {
        for (unsigned i = 0; i < T; ++i) {
            if (offset == 0) values[i] = 0;
            else if (offset == 1) values[i] = static_cast<Input>(~std::uint64_t{0});
            else if (offset == 2) values[i] = static_cast<Input>(std::uint64_t{1} << (i % (8 * sizeof(Input))));
            else values[i] = static_cast<Input>(random_value());
            tail_values[i] = static_cast<Input>(random_value());
        }
        reference(values.data(), expected.data());
        reference(tail_values.data(), tail_encoded.data());
        packed.fill(canary);
        C::encode(values.data(), packed.data() + offset);
        verify_encoded(expected.data(), packed, offset, B);
        std::copy_n(expected.data(), B, packed.data() + offset);
        std::memset(decoded.data(), canary, sizeof decoded);
        C::decode(packed.data() + offset, decoded.data() + 16);
        verify_values(values.data(), decoded.data() + 16, T);
        const auto* output_bytes = reinterpret_cast<const std::uint8_t*>(decoded.data());
        for (unsigned i = 0; i < 16 * sizeof(Output); ++i) {
            if (output_bytes[i] != canary || output_bytes[(16 + T) * sizeof(Output) + i] != canary)
                fail("decode output extent", W, G, sizeof(Input), Isa, i);
        }
        C::fragments(packed.data() + offset, fragments.data());
        for (unsigned part = 0; part < T / C::lanes; ++part) {
            for (unsigned lane = 0; lane < C::vector_bytes / sizeof(Output); ++lane) {
                Output actual;
                std::memcpy(&actual, fragments.data() + part * C::vector_bytes + lane * sizeof(Output), sizeof actual);
                const auto wanted = lane < C::lanes
                    ? static_cast<std::uint64_t>(values[part * C::lanes + lane]) & value_mask : 0;
                if (actual != wanted) fail("fragment lane/zero", W, G, sizeof(Input), Isa, part * C::lanes + lane);
            }
        }
        ++checked_cases;
        // Equal formats do not make two actual sources interchangeable.
        C::mixed_fragments(packed.data() + offset, tail_encoded.data(), fragments.data());
        for (unsigned part = 0; part < T / C::lanes; ++part) {
            for (unsigned lane = 0; lane < C::vector_bytes / sizeof(Output); ++lane) {
                Output actual;
                std::memcpy(&actual, fragments.data() + part * C::vector_bytes + lane * sizeof(Output), sizeof actual);
                constexpr unsigned tail_mask = (1u << (W % 8)) - 1;
                const auto index = part * C::lanes + lane;
                const auto wanted = lane < C::lanes
                    ? ((static_cast<std::uint64_t>(values[index]) & value_mask & ~std::uint64_t{tail_mask}) |
                       (static_cast<std::uint64_t>(tail_values[index]) & tail_mask)) : 0;
                if (actual != wanted) fail("independent body/tail sources", W, G, sizeof(Input), Isa, index);
            }
        }
        ++checked_cases;
    }

    if constexpr (G == geometry::striped) {
        constexpr unsigned group_lanes = std::min(32u, C::vector_bytes / static_cast<unsigned>(sizeof(Output)));
        std::array<std::uint8_t, T / 32 * C::vector_bytes> groups;
        for (unsigned start = 0; start + group_lanes <= 32; ++start) {
            C::groups(expected.data(), start, groups.data());
            for (unsigned group = 0; group < T / 32; ++group) {
                for (unsigned lane = 0; lane < C::vector_bytes / sizeof(Output); ++lane) {
                    Output actual;
                    std::memcpy(&actual, groups.data() + group * C::vector_bytes + lane * sizeof(Output), sizeof actual);
                    const auto wanted = lane < group_lanes
                        ? static_cast<std::uint64_t>(values[group * 32 + start + lane]) & value_mask : 0;
                    if (actual != wanted) fail("runtime group lane", W, G, sizeof(Input), Isa, group * 32 + start + lane);
                }
            }
            ++checked_cases;
        }
    }

    GuardedPage source;
    GuardedPage destination;
    GuardedPage encoded_source;
    GuardedPage value_destination;
    for (unsigned side = 0; side < 2; ++side) {
        const auto source_offset = side == 0 ? 0 : source.size - sizeof(values);
        const auto destination_offset = side == 0 ? 0 : destination.size - B;
        const auto output_offset = side == 0 ? 0 : value_destination.size - T * sizeof(Output);
        std::memcpy(source.bytes + source_offset, values.data(), sizeof(values));
        std::memset(destination.bytes, canary, destination.size);
        const auto* input = reinterpret_cast<const Input*>(source.bytes + source_offset);
        if constexpr (B == 0) C::encode(reinterpret_cast<const Input*>(source.mapping), destination.mapping);
        else C::encode(input, destination.bytes + destination_offset);
        verify_encoded(expected.data(), {destination.bytes, destination.size}, destination_offset, B);
        if constexpr (B != 0) std::memcpy(encoded_source.bytes + destination_offset, expected.data(), B);
        const auto* encoded = B == 0 ? encoded_source.mapping : encoded_source.bytes + destination_offset;
        auto* output = reinterpret_cast<Output*>(value_destination.bytes + output_offset);
        C::decode(encoded, output);
        verify_values(values.data(), output, T);
        C::fragments(encoded, fragments.data());
        ++checked_cases;
    }

    // Tile counts surround both dense-region grains, retain exact final tiles,
    // and include the width-56 four-packet encoder region.
    for (unsigned tiles : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 17u}) {
        std::vector<Input> input(tiles * T);
        std::vector<Output> output(tiles * T);
        std::vector<std::uint8_t> oracle(tiles * B + 1);
        std::vector<std::uint8_t> encoded(tiles * B + 32, canary);
        for (auto& value : input) value = static_cast<Input>(random_value());
        for (unsigned tile = 0; tile < tiles; ++tile)
            reference(input.data() + tile * T, oracle.data() + tile * B);
        C::encode_run(input.data(), encoded.data() + 13, tiles);
        verify_encoded(oracle.data(), encoded, 13, tiles * B);
        C::decode_run(encoded.data() + 13, output.data(), tiles);
        verify_values(input.data(), output.data(), tiles * T);
        if constexpr (G == geometry::local8 && W >= 1 && W <= 7) {
            // Wide-source narrowing now spans multiple local packets. Check
            // both ends of that exact region, including zero and remainder
            // counts, against guard pages rather than only heap canaries.
            for (unsigned side = 0; side < 2; ++side) {
                const unsigned input_bytes = tiles * T * sizeof(Input);
                const unsigned packed_bytes = tiles * B;
                const auto source_offset = side == 0 ? 0 : source.size - input_bytes;
                const auto packed_offset = side == 0 ? 0 : destination.size - packed_bytes;
                if (input_bytes) std::memcpy(source.bytes + source_offset, input.data(), input_bytes);
                std::memset(destination.bytes, canary, destination.size);
                C::encode_run(reinterpret_cast<const Input*>(tiles ? source.bytes + source_offset : source.mapping),
                              tiles ? destination.bytes + packed_offset : destination.mapping, tiles);
                verify_encoded(oracle.data(), {destination.bytes, destination.size}, packed_offset, packed_bytes);

                // Dense Local1 decode now performs bit work in byte lanes and
                // widens to the caller's carrier. Bound both the source and
                // the larger destination, including partial-region remainders.
                const unsigned decoded_bytes = tiles * T * sizeof(Output);
                const auto output_offset = side == 0 ? 0 : value_destination.size - decoded_bytes;
                if (packed_bytes) std::memcpy(encoded_source.bytes + packed_offset, oracle.data(), packed_bytes);
                std::memset(value_destination.bytes, canary, value_destination.size);
                auto* actual = reinterpret_cast<Output*>(tiles ? value_destination.bytes + output_offset
                                                               : value_destination.mapping);
                C::decode_run(tiles ? encoded_source.bytes + packed_offset : encoded_source.mapping, actual, tiles);
                verify_values(input.data(), actual, tiles * T);
                for (unsigned byte = 0; byte < value_destination.size; ++byte)
                    if ((byte < output_offset || byte >= output_offset + decoded_bytes) &&
                        value_destination.bytes[byte] != canary)
                        fail("dense decode guard", W, G, sizeof(Input), Isa, byte);
                ++checked_cases;
            }
        }
    }
}

template<unsigned W, geometry G, unsigned Isa>
void check_inputs() {
    check<W, G, std::uint8_t, Isa>();
    check<W, G, std::uint16_t, Isa>();
    check<W, G, std::uint32_t, Isa>();
    check<W, G, std::uint64_t, Isa>();
}

template<unsigned Isa>
void check_isa() {
    []<std::size_t... W>(std::index_sequence<W...>) {
        (check_inputs<W, geometry::local8, Isa>(), ...);
        ([&] {
            if constexpr (ikea::seriespack::supports_stripes(W)) check_inputs<W, geometry::striped, Isa>();
        }(), ...);
    }(std::make_index_sequence<65>{});
}

} // namespace

int main() {
    check_isa<256>();
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    check_isa<512>();
#endif
    std::printf("SeriesPack x86 payloads: %u configurations, %zu cases passed"
                " (W=0..64, selected stripes, u8/u16/u32/u64 input, native fragments, guards, dense runs).\n",
                checked_configurations, checked_cases);
}
#else
int main() {
    std::puts("SeriesPack x86 payloads: native checks skipped (AVX2 is not enabled).");
}
#endif
