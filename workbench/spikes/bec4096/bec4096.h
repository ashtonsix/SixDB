#pragma once
// Bec4096: one headless body for 4096 positions. The top four levels of the
// bisection tree form a head that recovers sixteen 256-position block
// populations; a length table then locates each block's Bec256 bitstream.
// Blocks are unchanged Bec256 streams, bit-packed in order. See README.md.
#include <ikea/bec256/codec.h>
#include <ikea/bec256/detail/tables.h>
#include <ikea/bitset256.h>
#include <ikea/effects.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

namespace bec4096 {
using ikea::bitset256;
using ikea::byte;
using ikea::bec256::view;
inline constexpr unsigned positions = 4096, blocks = 16, block_positions = 256;

namespace detail {
// Exact minimum and maximum Bec256 stream bits for each block population.
// A node's field width depends only on its own count, so children optimise
// independently. Leaves are bytes, whose rank widths come from Bec256.
struct block_bounds {
    std::array<std::uint16_t, 257> min{}, max{};
    consteval block_bounds() {
        std::array<unsigned, 257> lo{}, hi{};
        for (unsigned c = 0; c <= 8; ++c)
            lo[c] = hi[c] = ikea::bec256::detail::byte_width[c];
        for (unsigned size = 16; size <= 256; size *= 2) {
            const unsigned half = size / 2;
            std::array<unsigned, 257> next_lo{}, next_hi{};
            for (unsigned c = 0; c <= size; ++c) {
                unsigned best_lo = ~0u, best_hi = 0;
                for (unsigned q = c > half ? c - half : 0; q <= std::min(c, half); ++q) {
                    best_lo = std::min(best_lo, lo[q] + lo[c - q]);
                    best_hi = std::max(best_hi, hi[q] + hi[c - q]);
                }
                const unsigned width = std::bit_width(std::min(c, size - c));
                next_lo[c] = width + best_lo;
                next_hi[c] = width + best_hi;
            }
            lo = next_lo;
            hi = next_hi;
        }
        for (unsigned c = 0; c <= 256; ++c) {
            min[c] = std::uint16_t(lo[c]);
            max[c] = std::uint16_t(hi[c]);
        }
    }
};
inline constexpr block_bounds bounds;
static_assert(bounds.max[128] == 374 && bounds.min[1] == 8 && bounds.max[1] == 8);
// Length field width for a block of population c; zero for 0, 1, 255 and 256.
alignas(64) inline constexpr auto length_width = [] {
    std::array<std::uint8_t, 257> result{};
    for (unsigned c = 0; c <= 256; ++c)
        result[c] = std::uint8_t(std::bit_width(unsigned(bounds.max[c] - bounds.min[c])));
    return result;
}();
inline constexpr unsigned max_length_width =
    *std::max_element(length_width.begin(), length_width.end());
// Head splits regions of 4096, 2048, 1024 and 512 positions.
inline constexpr unsigned head_max_bits = 12 + 2 * 11 + 4 * 10 + 8 * 9;
inline constexpr unsigned max_bits = head_max_bits + 15 * max_length_width + 16 * 374;
[[gnu::always_inline]] inline unsigned lower(unsigned parent, unsigned half) {
    return parent > half ? parent - half : 0;
}
[[gnu::always_inline]] inline unsigned split_width(unsigned parent, unsigned half) {
    return std::bit_width(std::min(parent, 2 * half - parent));
}
} // namespace detail
static_assert(detail::head_max_bits == 146);
inline constexpr unsigned max_bytes = (detail::max_bits + 7) / 8, scratch_bytes = 64;

/// 4096 position bits. Position i is numeric bit i%64 of words[i/64]; block k
/// is words 4k..4k+3, the same law as bitset256.
struct bitset4096 {
    std::array<std::uint64_t, 64> words{};
    bool contains(unsigned position) const noexcept {
        return words[position / 64] >> (position % 64) & 1;
    }
    void set(unsigned position, bool value = true) noexcept {
        const auto bit = std::uint64_t{1} << (position % 64);
        auto &word = words[position / 64];
        word = value ? word | bit : word & ~bit;
    }
    unsigned count() const noexcept {
        unsigned result = 0;
        for (auto word : words)
            result += std::popcount(word);
        return result;
    }
    bitset256 block(unsigned index) const noexcept {
        bitset256 result;
        std::memcpy(result.words.data(), words.data() + 4 * index, 32);
        return result;
    }
    void assign(unsigned index, const bitset256 &value) noexcept {
        std::memcpy(words.data() + 4 * index, value.words.data(), 32);
    }
    bool operator==(const bitset4096 &) const = default;
};
static_assert(std::endian::native == std::endian::little, "spike assumes little-endian words");

enum class error {
    population,
    truncated,
    split,
    length,
    rank,
    padding,
    trailing,
    capacity,
    effects,
    overlap
};
std::string_view describe(error) noexcept;

/// Decoded head: each block's population and the bit offset of its stream.
/// 64 bytes, retained by a reader so block access never rereads the head.
struct frame {
    std::array<std::uint16_t, 16> population, start;
};

/// Admitted source. Placement and contents stay unchanged while it is used.
/// Storage beyond bytes() is initialized readable suffix, never body.
class reader {
    std::span<const byte> storage_;
    unsigned bytes_ = 0, population_ = 0;
    frame frame_{};
    friend reader bind_reader_unchecked(std::span<const byte>, unsigned, unsigned) noexcept;

  public:
    std::span<const byte> storage() const noexcept { return storage_; }
    unsigned bytes() const noexcept { return bytes_; }
    unsigned population() const noexcept { return population_; }
    unsigned population(unsigned block) const noexcept { return frame_.population[block]; }
    const struct frame &frame() const noexcept { return frame_; }
};

/// Exact framing check: head, lengths, every block stream, padding and length.
[[nodiscard]] std::expected<void, error> validate(std::span<const byte> body,
                                                  unsigned population) noexcept;
/// Checks placement and contents once, then retains the decoded frame.
[[nodiscard]] std::expected<reader, error>
bind_reader(std::span<const byte> storage, unsigned bytes, unsigned population) noexcept;
/// Caller retains a content proof for these exact bytes; decodes the frame only.
reader bind_reader_unchecked(std::span<const byte> storage, unsigned bytes,
                             unsigned population) noexcept;

/// Complete value. Output may not overlap the source.
void decode(const reader &, bitset4096 &) noexcept;
/// One 256-position block, without touching other blocks' streams.
void decode(const reader &, unsigned block, bitset256 &) noexcept;
/// Populations of one block's 32 bytes, from its count tree only.
void counts(const reader &, unsigned block, std::span<std::uint8_t, 32>) noexcept;
/// Set positions strictly before position; rank(r, 4096) is population().
unsigned rank(const reader &, unsigned position) noexcept;
/// Set positions in [first, first + count).
unsigned population(const reader &, unsigned first, unsigned count) noexcept;

/// Checked exact write. Returns body bytes; rejection leaves destination and
/// journal unchanged. Input must not overlap the destination.
[[nodiscard]] std::expected<unsigned, error> encode(const bitset4096 &, unsigned population,
                                                    view &, std::size_t offset,
                                                    ikea::journal &) noexcept;
/// Exact body size without writing; population is trusted.
unsigned encoded_bytes(const bitset4096 &, unsigned population) noexcept;
} // namespace bec4096
