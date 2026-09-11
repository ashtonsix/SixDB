#include <ikea/seriespack/operations.h>

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
#include <vector>

namespace {

namespace sp = ikea::seriespack;
using image = std::array<std::vector<std::byte>, 3>;
sp::execution_target execution = sp::execution_target::scalar;
const char* target_name = "scalar";

[[noreturn]] void fail(const char* what, sp::description description,
                       std::size_t length, std::size_t index = 0) {
    std::fprintf(stderr, "SeriesPack public wire: %s, k=%u h=%u geometry=%s "
                         "n=%zu index=%zu\n", what, description.width,
                 description.head_bits,
                 description.storage == sp::geometry::local8 ? "local8" : "striped",
                 length, index);
    std::abort();
}

[[maybe_unused]] std::uint64_t random_bits() {
    static std::uint64_t state = 0x803bf1ce97462da5ULL;
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint64_t mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

// Independently derive payload positions from ikea/seriespack/reference.md. This test never
// calls physical kernels, production residual maps or placement-offset helpers.
struct location { std::size_t byte; unsigned bit; };

unsigned residual_position(unsigned tail, unsigned group, unsigned bit) {
    constexpr unsigned three[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 14}, {8, 9, 10},
        {11, 12, 13}, {22, 23, 15}, {16, 17, 18}, {19, 20, 21},
    };
    constexpr unsigned six[4][6] = {
        {0, 1, 2, 3, 4, 5}, {8, 9, 10, 11, 6, 7},
        {12, 13, 14, 15, 22, 23}, {16, 17, 18, 19, 20, 21},
    };
    if (tail == 3) return three[group][bit];
    if (tail == 6) return six[group][bit];
    const unsigned start = group * tail;
    const unsigned room = 8 - start % 8;
    if (tail <= room) return start + bit;
    const unsigned low_bits = tail - room;
    return bit < low_bits ? (start / 8 + 1) * 8 + bit : start + bit - low_bits;
}

location payload_location(unsigned width, sp::geometry storage,
                          std::size_t value, unsigned bit) {
    const unsigned q = width / 8, r = width % 8;
    if (storage == sp::geometry::local8) {
        if (bit < r) return {8 * q + bit, unsigned(value)};
        return {value * q + (bit - r) / 8, (bit - r) % 8};
    }
    const auto group = unsigned(value / 32), lane = unsigned(value % 32);
    if (bit >= r) {
        std::size_t begin = 0;
        switch (width) {
        case 10: {
            constexpr unsigned starts[] = {0, 32, 96, 128};
            begin = starts[group];
            break;
        }
        case 12: begin = 32 * group; break;
        case 14:
        case 15: begin = 64 * group; break;
        case 20: begin = 96 * group; break;
        default: std::abort();
        }
        return {begin + lane * q + (bit - r) / 8, (bit - r) % 8};
    }
    const unsigned position = residual_position(r, group, bit);
    const unsigned stripe = position / 8;
    const unsigned start = width < 8 ? 32 * stripe
                           : (width == 14 || width == 15) ? 32 + 64 * stripe : 64;
    return {start + lane, position % 8};
}

[[maybe_unused]] bool striped_width(unsigned width) {
    return (width >= 1 && width <= 7) || width == 10 || width == 12 ||
           width == 14 || width == 15 || width == 20;
}

struct fixture {
    sp::description description;
    std::size_t length;
    std::size_t values_per_tile;
    std::size_t tiles;
    std::array<std::size_t, 3> tile_bytes{};
    std::array<std::size_t, 3> stride{};
    std::array<std::size_t, 3> envelope{};
    std::array<std::size_t, 3> base{};
    image bytes;

    fixture(sp::description desc, std::size_t n, bool strided)
        : description(desc), length(n),
          values_per_tile(desc.storage == sp::geometry::local8 ? 8
              : 32 * (8 / std::gcd((desc.width - desc.head_bits) % 8, 8U))),
          tiles(n / values_per_tile + (n % values_per_tile != 0)) {
        const unsigned payload = desc.width - desc.head_bits;
        tile_bytes[0] = values_per_tile * payload / 8;
        for (unsigned h = 0; h < desc.head_bits / 8; ++h) tile_bytes[1 + h] = values_per_tile;
        for (std::size_t plane = 0; plane < 3; ++plane) {
            stride[plane] = tile_bytes[plane] + (strided ?
                (plane == 0 && desc.storage == sp::geometry::striped ? 32 : 11 + plane * 2) : 0);
            envelope[plane] = tiles == 0 || tile_bytes[plane] == 0 ? 0
                : (tiles - 1) * stride[plane] + tile_bytes[plane];
            bytes[plane].assign(envelope[plane] + 128, std::byte{0xd3});
            const auto address = reinterpret_cast<std::uintptr_t>(bytes[plane].data());
            base[plane] = std::size_t((64 - address % 64) % 64);
            if (plane != 0 || desc.storage == sp::geometry::local8) base[plane] += desc.width % 8;
            // Owner-provided initialized tiles. Gaps and outside bytes retain
            // distinct canaries; expected images include all of them.
            for (std::size_t tile = 0; tile < tiles; ++tile) {
                std::fill_n(bytes[plane].begin() + std::ptrdiff_t(base[plane] + tile * stride[plane]),
                            tile_bytes[plane], std::byte{0});
            }
        }
    }

    std::span<std::byte> plane(std::size_t which) {
        if (envelope[which] == 0) return {};
        return {bytes[which].data() + base[which], envelope[which]};
    }

    sp::basic_placement<std::byte> placement() {
        return {{plane(0), stride[0]}, {{{plane(1), stride[1]}, {plane(2), stride[2]}}}};
    }

    sp::mutable_view view(std::size_t n) {
        auto result = sp::mutable_view::attach(description, n, placement());
        if (!result) fail("attach", description, n);
        return *result;
    }

    sp::mutable_view view() { return view(length); }

    void oracle_set(image& expected, std::size_t index, std::uint64_t value) const {
        const auto tile = index / values_per_tile, lane = index % values_per_tile;
        const unsigned payload = description.width - description.head_bits;
        for (unsigned bit = 0; bit < payload; ++bit) {
            const auto location = payload_location(payload, description.storage, lane, bit);
            auto& byte = expected[0][base[0] + tile * stride[0] + location.byte];
            const auto bit_mask = std::byte(1U << location.bit);
            byte = (byte & ~bit_mask) | (((value >> bit) & 1) ? bit_mask : std::byte{0});
        }
        for (unsigned head = 0; head < description.head_bits / 8; ++head) {
            expected[1 + head][base[1 + head] + tile * stride[1 + head] + lane] =
                std::byte((value >> (description.width - 8 * (head + 1))) & 255);
        }
    }

    void compare(const image& expected, const char* what) const {
        for (std::size_t plane = 0; plane < 3; ++plane) {
            if (bytes[plane] != expected[plane]) fail(what, description, length, plane);
        }
    }

    bool owned(std::size_t plane, std::size_t offset) const {
        if (tile_bytes[plane] == 0 || offset < base[plane]) return false;
        const auto relative = offset - base[plane];
        return relative < envelope[plane] && relative % stride[plane] < tile_bytes[plane];
    }
};

}
