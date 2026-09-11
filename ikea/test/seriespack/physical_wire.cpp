#include <ikea/seriespack/detail/physical.h>
#include <ikea/seriespack/layout.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace {

using ikea::seriespack::geometry;
namespace physical = ikea::seriespack::detail;

struct extent {
    std::size_t values;
    std::size_t bytes;
};

// This oracle follows the physical-layout maps in ikea/seriespack/reference.md. It does
// not use production maps, byte offsets, masks or decoding helpers.
constexpr extent wire_extent(unsigned width, geometry storage) {
    if (storage == geometry::local8) return {8, width};
    const unsigned groups = 8 / std::gcd(width % 8, 8U);
    return {32 * groups, 4 * groups * width};
}

struct bit_location {
    std::size_t byte;
    unsigned bit;
};

unsigned residual_position(unsigned width, unsigned group, unsigned bit) {
    constexpr unsigned three[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 14}, {8, 9, 10},
        {11, 12, 13}, {22, 23, 15}, {16, 17, 18}, {19, 20, 21},
    };
    constexpr unsigned six[4][6] = {
        {0, 1, 2, 3, 4, 5}, {8, 9, 10, 11, 6, 7},
        {12, 13, 14, 15, 22, 23}, {16, 17, 18, 19, 20, 21},
    };
    if (width == 3) return three[group][bit];
    if (width == 6) return six[group][bit];
    const unsigned start = group * width;
    const unsigned stripe = start / 8;
    const unsigned offset = start % 8;
    const unsigned room = 8 - offset;
    if (width <= room) return start + bit;
    const unsigned low_bits = width - room;
    if (bit < low_bits) return 8 * (stripe + 1) + bit;
    return 8 * stripe + offset + bit - low_bits;
}

std::size_t body_offset(unsigned width, unsigned group) {
    switch (width) {
    case 10: {
        constexpr unsigned starts[] = {0, 32, 96, 128};
        return starts[group];
    }
    case 12: return 32 * group;
    case 14:
    case 15: return 64 * group;
    case 20: return 96 * group;
    default: std::abort();
    }
}

std::size_t stripe_offset(unsigned width, unsigned stripe) {
    if (width < 8) return 32 * stripe;
    switch (width) {
    case 10:
    case 12:
    case 20: return 64;
    case 14:
    case 15: return 32 + 64 * stripe;
    default: std::abort();
    }
}

bit_location locate(unsigned width, geometry storage, std::size_t value,
                    unsigned bit) {
    const unsigned body = width / 8;
    const unsigned tail = width % 8;
    if (storage == geometry::local8) {
        if (bit < tail) return {8 * body + bit, unsigned(value)};
        return {value * body + (bit - tail) / 8, (bit - tail) % 8};
    }
    const unsigned group = unsigned(value / 32);
    const unsigned lane = unsigned(value % 32);
    if (bit >= tail) {
        return {body_offset(width, group) + lane * body + (bit - tail) / 8,
                (bit - tail) % 8};
    }
    const unsigned position = residual_position(tail, group, bit);
    return {stripe_offset(width, position / 8) + lane, position % 8};
}

std::uint64_t oracle_get(std::span<const std::uint8_t> bytes, unsigned width,
                         geometry storage, std::size_t index) {
    std::uint64_t value = 0;
    for (unsigned bit = 0; bit < width; ++bit) {
        const auto location = locate(width, storage, index, bit);
        value |= std::uint64_t((bytes[location.byte] >> location.bit) & 1) << bit;
    }
    return value;
}

void oracle_set(std::span<std::uint8_t> bytes, unsigned width, geometry storage,
                std::size_t index, std::uint64_t value) {
    for (unsigned bit = 0; bit < width; ++bit) {
        const auto location = locate(width, storage, index, bit);
        const auto mask = std::uint8_t(1U << location.bit);
        bytes[location.byte] = std::uint8_t(
            (bytes[location.byte] & ~mask) |
            (((value >> bit) & 1) ? mask : 0));
    }
}

std::vector<std::uint8_t> oracle_encode(std::span<const std::uint64_t> values,
                                       unsigned width, geometry storage) {
    std::vector<std::uint8_t> bytes(wire_extent(width, storage).bytes, 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        oracle_set(bytes, width, storage, i, values[i]);
    }
    return bytes;
}

constexpr std::uint64_t value_mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

[[noreturn]] void fail(const char* what, unsigned width, geometry storage,
                       std::size_t index = 0, std::uint64_t actual = 0,
                       std::uint64_t expected = 0) {
    std::fprintf(stderr,
                 "physical wire: %s, w=%u geometry=%s index=%zu actual=%llu "
                 "expected=%llu\n",
                 what, width, storage == geometry::local8 ? "local8" : "striped",
                 index, static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    std::abort();
}

std::uint64_t random_bits() {
    static std::uint64_t state = 0x2e47bd981a3506cfULL;
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

struct counts {
    std::size_t descriptions = 0;
    std::size_t basis_tiles = 0;
    std::size_t random_tiles = 0;
    std::size_t point_updates = 0;
    std::size_t protected_cases = 0;
} checked;

void compare_bytes(std::span<const std::uint8_t> actual,
                   std::span<const std::uint8_t> expected, unsigned width,
                   geometry storage) {
    if (actual.size() != expected.size()) fail("byte count", width, storage);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            fail("canonical encoded byte", width, storage, i, actual[i], expected[i]);
        }
    }
}

// Check the oracle itself for complete, nonoverlapping bit coverage. The line
// checks concern required bytes; actual accesses are separately bounded below
// with guard pages. They are not a measurement of the compiled instructions.
void check_geometry(unsigned width, geometry storage) {
    const auto shape = wire_extent(width, storage);
    std::vector<unsigned> bit_uses(shape.bytes * 8, 0);
    for (std::size_t i = 0; i < shape.values; ++i) {
        for (unsigned bit = 0; bit < width; ++bit) {
            const auto location = locate(width, storage, i, bit);
            if (location.byte >= shape.bytes || location.bit >= 8) {
                fail("oracle bit outside tile", width, storage, i);
            }
            ++bit_uses[location.byte * 8 + location.bit];
        }
    }
    if (std::any_of(bit_uses.begin(), bit_uses.end(), [](unsigned count) {
            return count != 1;
        })) fail("oracle bit map is not a bijection", width, storage);

    for (unsigned residue = 0; residue < 64; ++residue) {
        if (storage == geometry::striped && residue % 32 != 0) continue;
        for (std::size_t begin = 0; begin < shape.values; ++begin) {
            for (const unsigned count : {1U, 16U}) {
                if (width == 0 || (count == 16 && begin % 16 != 0)) continue;
                // Local sixteen-value reads cover two tiles from an aligned
                // dense origin. Enumerate every possible dense group phase.
                if (storage == geometry::local8 && count == 16 && residue != 0) {
                    continue;
                }
                const std::size_t groups = storage == geometry::local8 && count == 16
                                               ? 32 / std::gcd(width, 32U)
                                               : 1;
                for (std::size_t group = 0; group < groups; ++group) {
                    std::size_t first = std::numeric_limits<std::size_t>::max();
                    std::size_t last = 0;
                    for (std::size_t i = begin; i < begin + count; ++i) {
                        for (unsigned bit = 0; bit < width; ++bit) {
                            const auto location = locate(width, storage,
                                                         i % shape.values, bit);
                            const auto byte = residue + group * 2 * shape.bytes +
                                              (i / shape.values) * shape.bytes +
                                              location.byte;
                            first = std::min(first, byte / 64);
                            last = std::max(last, byte / 64);
                        }
                    }
                    const bool required = storage == geometry::striped || count == 1 ||
                                          width <= 32 + std::gcd(width, 32U);
                    if (required && last - first > 1) {
                        fail("required-byte locality", width, storage, begin,
                             last - first + 1, 2);
                    }
                }
            }
        }
    }
}

template<unsigned W, geometry G, class UInt>
void check_values(std::span<const std::uint64_t> expected, unsigned offset) {
    constexpr auto shape = wire_extent(W, G);
    constexpr std::size_t margin = 64;
    std::vector<std::uint8_t> bytes(shape.bytes + 2 * margin, 0xa5);
    auto* tile = W == 0 ? nullptr : bytes.data() + offset;
    constexpr unsigned scalar_residues = 64 / sizeof(UInt);
    std::vector<UInt> input_storage(shape.values + scalar_residues);
    std::vector<UInt> output_storage(shape.values + scalar_residues,
                                     std::numeric_limits<UInt>::max());
    auto* input = input_storage.data() + offset % scalar_residues;
    auto* output = output_storage.data() + offset % scalar_residues;
    for (std::size_t i = 0; i < shape.values; ++i) {
        input[i] = static_cast<UInt>(expected[i]);
    }
    physical::encode_tile<W, G, UInt>(input, tile);
    const auto canonical = oracle_encode(expected, W, G);
    compare_bytes({tile, shape.bytes}, canonical, W, G);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if ((i < offset || i >= offset + shape.bytes) && bytes[i] != 0xa5) {
            fail("encode writes outside tile", W, G, i);
        }
    }
    physical::decode_tile<W, G, UInt>(tile, output);
    for (std::size_t i = 0; i < shape.values; ++i) {
        const auto point = physical::get<W, G>(tile, i);
        if (point != expected[i]) fail("get", W, G, i, point, expected[i]);
        const auto arithmetic = physical::get_arithmetic<W, G>(tile, i);
        if (arithmetic != expected[i]) fail("arithmetic get", W, G, i, arithmetic, expected[i]);
        if (output[i] != expected[i]) {
            fail("decode", W, G, i, output[i], expected[i]);
        }
    }
    for (std::size_t i = 0; i < output_storage.size(); ++i) {
        const auto begin = offset % scalar_residues;
        if ((i < begin || i >= begin + shape.values) &&
            output_storage[i] != std::numeric_limits<UInt>::max()) {
            fail("decode writes outside output", W, G, i);
        }
    }
}

template<unsigned W, geometry G>
void check_raw_and_updates(unsigned offset) {
    constexpr auto shape = wire_extent(W, G);
    std::vector<std::uint8_t> bytes(shape.bytes + 128, 0x3c);
    auto* tile = W == 0 ? nullptr : bytes.data() + offset;
    for (std::size_t b = 0; b < shape.bytes; ++b) tile[b] = std::uint8_t(random_bits());
    std::vector<std::uint8_t> expected(shape.bytes);
    if constexpr (W != 0) std::memcpy(expected.data(), tile, shape.bytes);
    std::array<std::uint64_t, shape.values> decoded;
    physical::decode_tile<W, G, std::uint64_t>(tile, decoded.data());
    for (std::size_t i = 0; i < shape.values; ++i) {
        const auto oracle = oracle_get(expected, W, G, i);
        if (decoded[i] != oracle) fail("raw decode", W, G, i, decoded[i], oracle);
        const auto point = physical::get<W, G>(tile, i);
        if (point != oracle) fail("raw get", W, G, i, point, oracle);
        const auto arithmetic = physical::get_arithmetic<W, G>(tile, i);
        if (arithmetic != oracle) fail("raw arithmetic get", W, G, i, arithmetic, oracle);
        for (const auto value : {std::uint64_t{0}, value_mask(W),
                                 random_bits() & value_mask(W)}) {
            physical::set<W, G>(tile, i, value);
            oracle_set(expected, W, G, i, value);
            compare_bytes({tile, shape.bytes}, expected, W, G);
            ++checked.point_updates;
        }
    }
    for (std::size_t b = 0; b < bytes.size(); ++b) {
        if ((b < offset || b >= offset + shape.bytes) && bytes[b] != 0x3c) {
            fail("set writes outside tile", W, G, b);
        }
    }
}

template<unsigned W, geometry G>
void check_write_coverage() {
    constexpr auto shape = wire_extent(W, G);
    for (std::size_t i = 0; i < shape.values; ++i) {
        std::array<bool, shape.bytes> required{};
        std::array<bool, shape.bytes> reported{};
        for (unsigned bit = 0; bit < W; ++bit) {
            required[locate(W, G, i, bit).byte] = true;
        }
        physical::point_write_spans<W, G>(i, [&](std::size_t begin, std::size_t size) {
            if (begin > shape.bytes || size > shape.bytes - begin || size == 0) {
                fail("write span outside tile or empty", W, G, i);
            }
            for (std::size_t b = begin; b < begin + size; ++b) reported[b] = true;
        });
        if (required != reported) fail("point write coverage", W, G, i);
    }
}

class guarded_bytes {
    std::uint8_t* mapping_;
    std::size_t page_;
    std::uint8_t* data_;

public:
    guarded_bytes(std::size_t bytes, bool at_end) {
        const long page = ::sysconf(_SC_PAGESIZE);
        if (page <= 0 || bytes > std::size_t(page)) std::abort();
        page_ = std::size_t(page);
        void* mapping = ::mmap(nullptr, 3 * page_, PROT_NONE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) std::abort();
        mapping_ = static_cast<std::uint8_t*>(mapping);
        if (::mprotect(mapping_ + page_, page_, PROT_READ | PROT_WRITE) != 0) {
            std::abort();
        }
        data_ = mapping_ + page_ + (at_end ? page_ - bytes : 0);
    }
    ~guarded_bytes() { ::munmap(mapping_, 3 * page_); }
    guarded_bytes(const guarded_bytes&) = delete;
    guarded_bytes& operator=(const guarded_bytes&) = delete;
    std::uint8_t* data() { return data_; }
};

template<unsigned W, geometry G, class UInt>
void check_protected_boundaries() {
    constexpr auto shape = wire_extent(W, G);
    for (bool at_end : {false, true}) {
        guarded_bytes encoded(shape.bytes, at_end);
        guarded_bytes input_bytes(shape.values * sizeof(UInt), at_end);
        guarded_bytes output_bytes(shape.values * sizeof(UInt), at_end);
        auto* input = reinterpret_cast<UInt*>(input_bytes.data());
        auto* output = reinterpret_cast<UInt*>(output_bytes.data());
        auto* tile = W == 0 ? nullptr : encoded.data();
        std::array<std::uint64_t, shape.values> expected;
        for (std::size_t i = 0; i < shape.values; ++i) {
            input[i] = static_cast<UInt>(random_bits() & value_mask(W));
            expected[i] = input[i];
        }
        physical::encode_tile<W, G, UInt>(input, tile);
        compare_bytes({tile, shape.bytes}, oracle_encode(expected, W, G), W, G);
        physical::decode_tile<W, G, UInt>(tile, output);
        for (std::size_t i = 0; i < shape.values; ++i) {
            if (output[i] != expected[i]) {
                fail("protected decode", W, G, i, output[i], expected[i]);
            }
            const auto value = physical::get<W, G>(tile, i);
            if (value != expected[i]) fail("protected get", W, G, i, value, expected[i]);
            const auto arithmetic = physical::get_arithmetic<W, G>(tile, i);
            if (arithmetic != expected[i])
                fail("protected arithmetic get", W, G, i, arithmetic, expected[i]);
            expected[i] ^= value_mask(W);
            physical::set<W, G>(tile, i, expected[i]);
        }
        compare_bytes({tile, shape.bytes}, oracle_encode(expected, W, G), W, G);
        ++checked.protected_cases;
    }
}

template<unsigned W, geometry G>
void check_description() {
    constexpr auto shape = wire_extent(W, G);
    using layout = ikea::seriespack::payload_layout<W, G>;
    static_assert(layout::tile_values == shape.values);
    static_assert(layout::tile_bytes == shape.bytes);
    check_geometry(W, G);
    check_write_coverage<W, G>();
    std::array<std::uint64_t, shape.values> values{};
    check_values<W, G, std::uint64_t>(values, 0);
    for (std::size_t i = 0; i < shape.values; ++i) {
        for (unsigned bit = 0; bit < W; ++bit) {
            values[i] = std::uint64_t{1} << bit;
            check_values<W, G, std::uint64_t>(values, unsigned(bit % 64));
            values[i] = 0;
            ++checked.basis_tiles;
        }
    }
    for (unsigned offset = 0; offset < 64; ++offset) {
        for (auto& value : values) value = random_bits() & value_mask(W);
        check_values<W, G, std::uint64_t>(values, offset);
        if constexpr (W <= 32) check_values<W, G, std::uint32_t>(values, offset);
        if constexpr (W <= 16) check_values<W, G, std::uint16_t>(values, offset);
        if constexpr (W <= 8) check_values<W, G, std::uint8_t>(values, offset);
        check_raw_and_updates<W, G>(offset);
        ++checked.random_tiles;
    }
    check_protected_boundaries<W, G, std::uint64_t>();
    if constexpr (W <= 32) check_protected_boundaries<W, G, std::uint32_t>();
    if constexpr (W <= 16) check_protected_boundaries<W, G, std::uint16_t>();
    if constexpr (W <= 8) check_protected_boundaries<W, G, std::uint8_t>();
    ++checked.descriptions;
}

template<std::size_t... Widths>
void check_local(std::index_sequence<Widths...>) {
    (check_description<unsigned(Widths), geometry::local8>(), ...);
}

} // namespace

int main() {
    check_local(std::make_index_sequence<65>{});
    check_description<1, geometry::striped>();
    check_description<2, geometry::striped>();
    check_description<3, geometry::striped>();
    check_description<4, geometry::striped>();
    check_description<5, geometry::striped>();
    check_description<6, geometry::striped>();
    check_description<7, geometry::striped>();
    check_description<10, geometry::striped>();
    check_description<12, geometry::striped>();
    check_description<14, geometry::striped>();
    check_description<15, geometry::striped>();
    check_description<20, geometry::striped>();
    std::printf("SeriesPack physical wire: %zu descriptions, %zu basis tiles, "
                "%zu random tiles, %zu point updates, %zu protected boundary "
                "cases passed\n",
                checked.descriptions, checked.basis_tiles, checked.random_tiles,
                checked.point_updates, checked.protected_cases);
}
