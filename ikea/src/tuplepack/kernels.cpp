#include <ikea/tuplepack/author/native.h>

namespace ikea::tuplepack::detail {
namespace {
template <unsigned N> std::uint64_t read8(const scalar_read<8>& p, const byte* row) {
    return read_body<8, N>(p, row);
}
void single_write(const scalar_write<8>& p, byte* row, std::uint64_t input) {
    // Exactly one selected code, possibly after holes in the outer map. Avoid
    // the generic byte/contribution traversal on the dominant point family.
    const auto& b = p.stores[0];
    const auto value = byte(input >> (8 * b.input[0]));
    row[b.offset] = (b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask)) | (value << b.shift[0]);
}
void general_write(const scalar_write<8>& p, byte* row, std::uint64_t input) {
    write_body<8>(p, row, input);
}
template <unsigned N> void word_write(const scalar_write<8>& p, byte* row, std::uint64_t input) {
    const auto& w = p.word;
    std::uint64_t old = 0;
    // Bounded 1..8-byte load. The selected span may include unchanged bytes,
    // but never extends beyond the unit; only selected bytes are stored.
    if (w.bytes == 8)
        __builtin_memcpy(&old, row + w.offset, 8);
    else {
        unsigned n = w.bytes, offset = 0;
        if (n & 4) {
            std::uint32_t x;
            __builtin_memcpy(&x, row + w.offset, 4);
            old = x;
            offset = 4;
        }
        if (n & 2) {
            std::uint16_t x;
            __builtin_memcpy(&x, row + w.offset + offset, 2);
            old |= std::uint64_t(x) << (offset * 8);
            offset += 2;
        }
        if (n & 1)
            old |= std::uint64_t(row[w.offset + offset]) << (offset * 8);
    }
    auto next = old & ~w.changed;
    for (unsigned i = 0; i < N; ++i)
        next |= std::uint64_t(byte(input >> w.input_shift[i])) << w.output_shift[i];
    for (unsigned i = 0; i < p.count; ++i) {
        const auto offset = p.stores[i].offset;
        row[offset] = byte(next >> ((offset - w.offset) * 8));
    }
}
} // namespace
read8_function select_read8(unsigned slots) {
    switch (slots) {
    case 0:
        return read8<0>;
    case 1:
        return read8<1>;
    case 2:
        return read8<2>;
    case 3:
    case 4:
        return read8<4>;
    default:
        return read8<8>;
    }
}
write8_function select_write8(const scalar_write<8>& p) {
    const auto selected = p.word.selected;
    if (selected == 1)
        return single_write;
    if (p.word.bytes) {
        switch (selected) {
        case 2:
            return word_write<2>;
        case 3:
            return word_write<3>;
        case 4:
            return word_write<4>;
        case 5:
            return word_write<5>;
        case 6:
            return word_write<6>;
        case 7:
            return word_write<7>;
        case 8:
            return word_write<8>;
        }
    }
    return general_write;
}
std::array<byte, 64> read_buffered(const read64& p, const byte* row) {
#if defined(__aarch64__) || defined(__AVX2__)
    std::array<byte, 64> result;
    native::store_packet(result.data(), native::read_body(p, row));
    return result;
#else
    return read_body<64>(p, row);
#endif
}
void write_buffered(const write64& p, byte* row, const std::array<byte, 64>& input) {
#if defined(__aarch64__) || defined(__AVX2__)
    if (p.dense) {
        native::write_body(p, row, native::load_packet(input.data()));
        return;
    }
#endif
    write_body<64>(p, row, input);
}
} // namespace ikea::tuplepack::detail

#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native {
IKEA_TUPLE_CC packet read(const detail::read64& p, const byte* row) {
    return read_body(p, row);
}
IKEA_TUPLE_CC void write(const detail::write64& p, byte* row, packet input) {
    write_body(p, row, input);
}
} // namespace ikea::tuplepack::native
#endif
