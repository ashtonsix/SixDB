#pragma once
// Provisional GPR packet experiment. No supported Ikea interface is established.
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <cassert>

namespace gpr_probe {
namespace tp = ikea::tuplepack;
using word = std::uint64_t;

template <unsigned Bytes>
[[gnu::always_inline]] inline word load(const tp::byte* p) {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8);
    if constexpr (Bytes == 1)
        return *p;
    else if constexpr (Bytes == 2) {
        std::uint16_t v;
        __builtin_memcpy(&v, p, 2);
        return v;
    } else if constexpr (Bytes == 4) {
        std::uint32_t v;
        __builtin_memcpy(&v, p, 4);
        return v;
    } else {
        word v;
        __builtin_memcpy(&v, p, 8);
        return v;
    }
}
template <unsigned Bytes>
[[gnu::always_inline]] inline void store(tp::byte* p, word value) {
    __builtin_memcpy(p, &value, Bytes);
}
[[gnu::always_inline]] inline word short_load(const tp::byte* p, unsigned bytes) {
    if (bytes == 8)
        return load<8>(p);
    word value = 0;
    unsigned offset = 0;
    if (bytes & 4) {
        value = load<4>(p);
        offset = 4;
    }
    if (bytes & 2) {
        value |= load<2>(p + offset) << (8 * offset);
        offset += 2;
    }
    if (bytes & 1)
        value |= word(p[offset]) << (8 * offset);
    return value;
}
template <unsigned Count>
[[gnu::always_inline]] inline void write_row(const tp::detail::scalar_write<8>& p,
                                             tp::byte* row, word input) {
    if constexpr (Count == 1) {
        const auto& b = p.stores[0];
        row[b.offset] = (b.mask == 255 ? 0 : row[b.offset] & tp::byte(~b.mask)) |
                        (tp::byte(input >> (8 * b.input[0])) << b.shift[0]);
    } else if (p.word.bytes) {
        const auto& w = p.word;
        auto next = short_load(row + w.offset, w.bytes) & ~w.changed;
        for (unsigned i = 0; i < Count; ++i)
            next |= word(tp::byte(input >> w.input_shift[i])) << w.output_shift[i];
        for (unsigned i = 0; i < p.count; ++i)
            row[p.stores[i].offset] = tp::byte(next >> (8 * (p.stores[i].offset - w.offset)));
    } else
        tp::detail::write_body<8>(p, row, input);
}

template <unsigned Rows> struct plan {
    static constexpr unsigned slots = 8 / Rows;
    tp::reader<8> read;
    tp::writer<8> write;
    bool direct = true;
    unsigned offset = 0, shift = 0;
    word mask = 0, invalid = 0;
    plan(const tp::layout& f, std::span<const tp::byte> map)
        : read(*tp::reader<8>::make(f, map)), write(*tp::writer<8>::make(f, map)) {
        assert(map.size() <= slots);
        const auto& p = read.controls();
        offset = p.codes[0].offset;
        shift = p.codes[0].shift;
        for (unsigned r = 0; r < Rows; ++r)
            for (unsigned i = 0; i < slots; ++i) {
                const auto c = p.codes[i];
                direct &= c.width && c.offset == offset + i && c.shift == shift;
                mask |= word((1u << c.width) - 1) << (8 * (r * slots + i));
                invalid |= word(write.controls().invalid[i]) << (8 * (r * slots + i));
            }
    }
};
template <unsigned Rows>
[[gnu::always_inline]] inline word row_mask(word active) {
    constexpr unsigned bits = 64 / Rows;
    constexpr word part = ~word(0) >> (64 - bits);
    word mask = 0;
    for (unsigned r = 0; r < Rows; ++r)
        mask |= (word(0) - ((active >> r) & 1)) & (part << (bits * r));
    return mask;
}
template <unsigned Rows>
[[gnu::always_inline]] inline word read(const plan<Rows>& p, const tp::view& v,
                                        std::size_t first, word active) {
    constexpr unsigned B = 8 / Rows;
    word value = 0;
    if (p.direct) {
        if (active == tp::detail::all_rows<Rows> && v.stride() == B)
            value = load<8>(v.row_unchecked(first) + p.offset);
        else
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (word(1) << r))
                    value |= load<B>(v.row_unchecked(first + r) + p.offset) << (r * B * 8);
        return (value >> p.shift) & p.mask;
    }
    for (unsigned r = 0; r < Rows; ++r)
        if (active & (word(1) << r))
            value |= tp::detail::read_body<8, B>(p.read.controls(), v.row_unchecked(first + r))
                     << (r * B * 8);
    return value;
}
template <unsigned Rows, unsigned Count>
[[gnu::always_inline]] inline void general_write(const plan<Rows>& p, const tp::view& v,
                                                 std::size_t first, word input, word active) {
    for (unsigned r = 0; r < Rows; ++r)
        if (active & (word(1) << r))
            write_row<Count>(p.write.controls(), v.row_unchecked(first + r),
                             input >> (r * (64 / Rows)));
}
template <unsigned Rows>
[[gnu::always_inline]] inline void write(const plan<Rows>& p, const tp::view& v,
                                         std::size_t first, word input, word active) {
    constexpr unsigned B = 8 / Rows;
    if (p.direct) {
        const word changed = p.mask << p.shift;
        const word encoded = (input & p.mask) << p.shift;
        if (active == tp::detail::all_rows<Rows> && v.stride() == B) {
            auto address = v.row_unchecked(first) + p.offset;
            store<8>(address, (changed == ~word(0) ? 0 : load<8>(address) & ~changed) | encoded);
        } else
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (word(1) << r)) {
                    auto address = v.row_unchecked(first + r) + p.offset;
                    store<B>(address, (load<B>(address) & ~(changed >> (r * B * 8))) |
                                          (encoded >> (r * B * 8)));
                }
        return;
    }
    switch (p.write.controls().word.selected) {
    case 1: return general_write<Rows, 1>(p, v, first, input, active);
    case 2:
        if constexpr (Rows <= 4) return general_write<Rows, 2>(p, v, first, input, active);
        else __builtin_unreachable();
    case 4:
        if constexpr (Rows <= 2) return general_write<Rows, 4>(p, v, first, input, active);
        else __builtin_unreachable();
    case 8:
        if constexpr (Rows == 1) return general_write<Rows, 8>(p, v, first, input, active);
        else __builtin_unreachable();
    default: __builtin_unreachable(); // The experiment uses these selected counts.
    }
}
template <unsigned Rows>
__attribute__((noinline)) word endpoint_read(const plan<Rows>& p, const tp::view& v,
                                             std::size_t first, word active) {
    return read(p, v, first, active);
}
template <unsigned Rows>
__attribute__((noinline)) void endpoint_write(const plan<Rows>& p, const tp::view& v,
                                              std::size_t first, word input, word active) {
    write(p, v, first, input, active);
}
template <unsigned Rows, bool Compiled>
struct mutation : tp::detail::mutation_commands<mutation<Rows, Compiled>, word, Rows> {
    const plan<Rows>& controls;
    const tp::view& destination;
    mutation(const plan<Rows>& p, const tp::view& v) : controls(p), destination(v) {}
    std::size_t size() const { return destination.size(); }
    unsigned effect_capacity() const { return controls.write.effect_capacity(); }
    bool accepts(word input, word active) const {
        return !(input & controls.invalid & row_mask<Rows>(active));
    }
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t first, word input, Coverage& effects,
                                              word active) const {
        tp::detail::emit_window<Rows>(destination, first, controls.write.controls(), effects, active);
        if constexpr (Compiled)
            endpoint_write(controls, destination, first, input, active);
        else
            write(controls, destination, first, input, active);
    }
};
} // namespace gpr_probe
