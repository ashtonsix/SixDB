#pragma once
#include <ikea/tuplepack/description.h>
#include <type_traits>

namespace ikea::tuplepack {
template <unsigned N> using packet = std::conditional_t<N == 8, std::uint64_t, std::array<byte, N>>;
namespace detail {
template <unsigned N> struct scalar_read {
    static_assert(N == 8 || N == 64);
    std::array<code, N> codes{};
    unsigned slots = 0, bytes = 0;
    std::uint64_t reads = 0;
};
struct write_byte {
    byte offset = 0, mask = 0, count = 0;
    std::array<byte, 8> input{}, shift{};
};
struct byte_run {
    byte offset = 0, size = 0;
};
struct point_word {
    std::uint64_t changed = 0;
    std::array<byte, 8> input_shift{}, output_shift{};
    byte offset = 0, bytes = 0, selected = 0;
};
struct empty_control {};
template <unsigned N> struct scalar_write {
    scalar_read<N> read;
    std::array<write_byte, N> stores{};
    std::array<byte, N> invalid{};
    std::array<byte_run, N> footprint{};
    [[no_unique_address]] std::conditional_t<N == 8, point_word, empty_control> word;
    unsigned count = 0, runs = 0;
    std::uint64_t writes = 0;
};
/// Only controls for this build's ISA are retained. These are instruction
/// operands, not a portable physical description or an abstract vector ISA.
struct alignas(64) shuffle {
    std::array<byte, 64> index{}, mask{};
#if defined(__aarch64__)
    std::array<std::int8_t, 64> shift{};
#elif defined(__AVX512VBMI__)
    std::array<byte, 64> bit_index{};
#elif defined(__AVX2__)
    std::array<std::array<byte, 64>, 4> avx2_index{};
    std::array<std::uint16_t, 32> even_factor{}, odd_factor{};
#endif
    unsigned routes = 0;
    bool shifting = false, masking = false;
};
struct chunk {
    byte offset = 0, bytes = 0;
};
struct shuffle_description {
    std::array<byte, 64> index{}, mask{};
    std::array<std::int8_t, 64> shift{};
};
shuffle compile_shuffle(const shuffle_description&, bool left);
struct read64 : scalar_read<64> {
    shuffle operation;
    std::array<chunk, 4> chunks{};
    unsigned count = 0;
    std::uint64_t native_reads = 0;
};
struct write64 : scalar_write<64> {
    std::array<shuffle, 8> rounds{};
    std::array<byte, 64> preserve{};
    unsigned round_count = 0;
    bool dense = false, needs_old = false;
};
template <unsigned N> using read_control = std::conditional_t<N == 8, scalar_read<8>, read64>;
template <unsigned N> using write_control = std::conditional_t<N == 8, scalar_write<8>, write64>;

std::expected<scalar_read<8>, error> prepare_read8(const layout&, std::span<const byte>);
std::expected<read64, error> prepare_read64(const layout&, std::span<const byte>);
std::expected<scalar_write<8>, error> prepare_write8(const layout&, std::span<const byte>);
std::expected<write64, error> prepare_write64(const layout&, std::span<const byte>);

template <unsigned N>
[[gnu::always_inline]] inline byte input_byte(const packet<N>& input, unsigned i) {
    if constexpr (N == 8)
        return byte(input >> (i * 8));
    else
        return input[i];
}
template <unsigned N>
[[gnu::always_inline]] inline bool fits(const scalar_write<N>& p, const packet<N>& input) {
    if constexpr (N == 8) {
        // A word test is important for small point operations; no per-code
        // branch or physical-byte traversal belongs in this admission path.
        std::uint64_t invalid;
        __builtin_memcpy(&invalid, p.invalid.data(), 8);
        return !(input & invalid);
    } else {
        // Fixed reduction vectorizes; an early-return loop over runtime map
        // length made ordinary wide mutation admission dominate the kernel.
        byte invalid = 0;
        for (unsigned i = 0; i < N; ++i)
            invalid |= input[i] & p.invalid[i];
        return invalid == 0;
    }
}
template <unsigned N, unsigned Count = N>
[[gnu::always_inline]] inline packet<N> read_body(const scalar_read<N>& p, const byte* row) {
    packet<N> out{};
    for (unsigned i = 0; i < Count; ++i) {
        const auto c = p.codes[i];
        if (!c.width)
            continue;
        const byte value = (row[c.offset] >> c.shift) & ((1u << c.width) - 1);
        if constexpr (N == 8)
            out |= std::uint64_t(value) << (i * 8);
        else
            out[i] = value;
    }
    return out;
}
template <unsigned N>
[[gnu::always_inline]] inline void write_body(const scalar_write<N>& p, byte* row,
                                              const packet<N>& input) {
    for (unsigned i = 0; i < p.count; ++i) {
        const auto& b = p.stores[i];
        byte value = b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask);
        for (unsigned j = 0; j < b.count; ++j)
            value |= input_byte<N>(input, b.input[j]) << b.shift[j];
        row[b.offset] = value;
    }
}
using read8_function = std::uint64_t (*)(const scalar_read<8>&, const byte*);
using write8_function = void (*)(const scalar_write<8>&, byte*, std::uint64_t);
read8_function select_read8(unsigned slots);
write8_function select_write8(const scalar_write<8>&);
std::array<byte, 64> read_buffered(const read64&, const byte*);
void write_buffered(const write64&, byte*, const std::array<byte, 64>&);
} // namespace detail
} // namespace ikea::tuplepack
