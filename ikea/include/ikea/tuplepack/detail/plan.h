#pragma once
#include <bit>
#include <ikea/tuplepack/packet_layout.h>
#include <type_traits>
#include <ikea/tuplepack/packet.h>

namespace ikea::tuplepack {
namespace detail {
template <unsigned N> struct scalar_read {
    static_assert(N == 8 || N == 64);
    std::array<code, N> codes{};
    packet_layout<N> ordering;
    unsigned slots = 0, bytes = 0;
    std::uint64_t reads = 0;
};
struct write_byte {
    std::uint8_t offset = 0, mask = 0, count = 0;
    std::array<std::uint8_t, 8> input{}, shift{};
};
struct byte_run {
    std::uint8_t offset = 0, size = 0;
};
struct point_word {
    std::uint64_t changed = 0;
    std::array<std::uint8_t, 8> input_shift{}, output_shift{};
    std::uint8_t offset = 0, bytes = 0, selected = 0;
};
struct empty_control {};
template <unsigned N> struct scalar_write {
    scalar_read<N> read;
    std::array<write_byte, N> stores{};
    std::array<std::uint8_t, N> invalid{};
    std::array<byte_run, N> footprint{};
    [[no_unique_address]] std::conditional_t<N == 8, point_word, empty_control> word;
    unsigned count = 0, runs = 0;
    std::uint64_t writes = 0;
};
/// Only controls for this build's ISA are retained. These are instruction
/// operands, not a portable physical description or an abstract vector ISA.
struct alignas(64) shuffle {
    std::array<std::uint8_t, 64> index{}, mask{};
#if defined(__aarch64__)
    std::array<std::int8_t, 64> shift{};
#elif defined(__AVX512VBMI__)
    std::array<std::uint8_t, 64> bit_index{};
#elif defined(__AVX2__)
    std::array<std::array<std::uint8_t, 64>, 4> avx2_index{};
    std::array<std::uint16_t, 32> even_factor{}, odd_factor{};
#endif
    unsigned routes = 0;
    bool shifting = false, masking = false;
};
struct chunk {
    std::uint8_t offset = 0, bytes = 0;
};
struct shuffle_description {
    std::array<std::uint8_t, 64> index{}, mask{};
    std::array<std::int8_t, 64> shift{};
};
shuffle compile_shuffle(const shuffle_description &, bool left);
struct read64 : scalar_read<64> {
    shuffle operation;
    std::array<chunk, 4> chunks{};
    unsigned count = 0;
    std::uint64_t native_reads = 0;
};
struct write64 : scalar_write<64> {
    std::array<shuffle, 8> rounds{};
    std::array<std::uint8_t, 64> preserve{};
    unsigned round_count = 0;
    bool dense = false, needs_old = false;
};
// A consecutive, equally shifted byte projection admits a bounded word
// transfer for a single group. Short maps leave trailing packet capacity zero.
struct word_transfer {
    std::uint64_t mask = 0;
    std::uint8_t offset = 0, shift = 0, bytes = 0;
};
struct gpr_read : scalar_read<8> {
    word_transfer transfer;
};
struct gpr_write : scalar_write<8> {
    word_transfer transfer;
    std::uint64_t invalid_word = 0, native_reads = 0;
};
std::expected<gpr_read, error> prepare_read8(const layout &, std::span<const std::uint8_t>,
                                             unsigned rows = 1,
                                             std::span<const unsigned> groups = {});
std::expected<read64, error> prepare_read64(const layout &, std::span<const std::uint8_t>);
std::expected<gpr_write, error> prepare_write8(const layout &, std::span<const std::uint8_t>,
                                               unsigned rows = 1,
                                               std::span<const unsigned> groups = {});
std::expected<write64, error> prepare_write64(const layout &, std::span<const std::uint8_t>);

template <unsigned Rows>
[[gnu::always_inline]] inline std::uint64_t gpr_row_mask(std::uint64_t active) {
    static_assert(Rows <= 8 && std::has_single_bit(Rows));
    constexpr unsigned bits = 64 / Rows;
    constexpr auto part = ~std::uint64_t(0) >> (64 - bits);
    std::uint64_t mask = 0;
    for (unsigned r = 0; r < Rows; ++r)
        mask |= (std::uint64_t(0) - ((active >> r) & 1)) & (part << (bits * r));
    return mask;
}

template <unsigned N>
[[gnu::always_inline]] inline std::uint8_t input_byte(const packet<N> &input, unsigned i) {
    if constexpr (N == 8)
        return std::uint8_t(input >> (i * 8));
    else
        return input[i];
}
template <unsigned N>
[[gnu::always_inline]] inline bool fits(const scalar_write<N> &p, const packet<N> &input) {
    if constexpr (N == 8) {
        // A word test is important for small point operations; no per-code
        // branch or physical-byte traversal belongs in this admission path.
        std::uint64_t invalid;
        __builtin_memcpy(&invalid, p.invalid.data(), 8);
        return !(input & invalid);
    } else {
        // Fixed reduction vectorizes; an early-return loop over runtime map
        // length made ordinary wide mutation admission dominate the kernel.
        std::uint8_t invalid = 0;
        for (unsigned i = 0; i < N; ++i)
            invalid |= input[i] & p.invalid[i];
        return invalid == 0;
    }
}
template <unsigned N, unsigned Count = N>
[[gnu::always_inline]] inline packet<N> read_body(const scalar_read<N> &p, const std::uint8_t *row) {
    packet<N> out{};
    for (unsigned i = 0; i < Count; ++i) {
        const auto c = p.codes[i];
        if (!c.width)
            continue;
        const std::uint8_t value = (row[c.offset] >> c.shift) & ((1u << c.width) - 1);
        if constexpr (N == 8)
            out |= std::uint64_t(value) << (i * 8);
        else
            out[i] = value;
    }
    return out;
}
template <unsigned N>
[[gnu::always_inline]] inline void write_body(const scalar_write<N> &p, std::uint8_t *row,
                                              const packet<N> &input) {
    for (unsigned i = 0; i < p.count; ++i) {
        const auto &b = p.stores[i];
        std::uint8_t value = b.mask == 255 ? 0 : row[b.offset] & std::uint8_t(~b.mask);
        for (unsigned j = 0; j < b.count; ++j)
            value |= input_byte<N>(input, b.input[j]) << b.shift[j];
        row[b.offset] = value;
    }
}
// A point endpoint has no traversal stride or active-row argument. Keeping
// those unused arguments out preserves registers for the surrounding caller.
template <unsigned Rows>
using gpr_reader = std::conditional_t<Rows == 1, std::uint64_t (*)(const gpr_read &, const std::uint8_t *),
                                      std::uint64_t (*)(const gpr_read &, const std::uint8_t *, std::size_t,
                                                        std::uint64_t)>;
template <unsigned Rows>
using gpr_writer = std::conditional_t<Rows == 1, void (*)(const gpr_write &, std::uint8_t *, std::uint64_t),
                                      void (*)(const gpr_write &, std::uint8_t *, std::size_t,
                                               std::uint64_t, std::uint64_t)>;
template <unsigned Rows> gpr_reader<Rows> select_gpr_reader(const gpr_read &);
template <unsigned Rows> gpr_writer<Rows> select_gpr_writer(const gpr_write &);
template <unsigned Rows>
std::uint64_t read_gpr(const gpr_read &, const std::uint8_t *, std::size_t stride, std::uint64_t active);
template <unsigned Rows>
void write_gpr(const gpr_write &, std::uint8_t *, std::size_t stride, std::uint64_t input,
               std::uint64_t active);
std::array<std::uint8_t, 64> read_buffered(const read64 &, const std::uint8_t *);
void write_buffered(const write64 &, std::uint8_t *, const std::array<std::uint8_t, 64> &);
} // namespace detail
} // namespace ikea::tuplepack
