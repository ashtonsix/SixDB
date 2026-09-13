#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/window.h>

namespace ikea::tuplepack::detail {
template <unsigned Bytes> [[gnu::always_inline]] inline std::uint64_t load_word(const byte* p) {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8);
    using value_type = std::conditional_t<
        Bytes == 1, byte,
        std::conditional_t<Bytes == 2, std::uint16_t,
                           std::conditional_t<Bytes == 4, std::uint32_t, std::uint64_t>>>;
    value_type value;
    __builtin_memcpy(&value, p, Bytes);
    return value;
}
/// Exact 0..8-byte loads/stores: a tuple may end at the allocation boundary.
[[gnu::always_inline]] inline std::uint64_t load_short_word(const byte* p, unsigned bytes) {
    if (bytes == 8)
        return load_word<8>(p);
    std::uint64_t value = 0;
    unsigned offset = 0;
    if (bytes & 4) {
        value = load_word<4>(p);
        offset = 4;
    }
    if (bytes & 2) {
        value |= load_word<2>(p + offset) << (8 * offset);
        offset += 2;
    }
    if (bytes & 1)
        value |= std::uint64_t(p[offset]) << (8 * offset);
    return value;
}
[[gnu::always_inline]] inline void store_short_word(byte* p, std::uint64_t value, unsigned bytes) {
    if (bytes == 8) {
        __builtin_memcpy(p, &value, 8);
        return;
    }
    if (bytes & 4) {
        __builtin_memcpy(p, &value, 4);
        p += 4;
        value >>= 32;
    }
    if (bytes & 2) {
        __builtin_memcpy(p, &value, 2);
        p += 2;
        value >>= 16;
    }
    if (bytes & 1)
        *p = byte(value);
}
[[gnu::always_inline]] inline void write_gpr_single(const gpr_write& p, byte* row,
                                                    std::uint64_t input) {
    const auto& b = p.stores[0];
    row[b.offset] = (b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask)) |
                    (byte(input >> (8 * b.input[0])) << b.shift[0]);
}
/// Same point body serves every shape. Word assembly avoids one read/modify/write
/// per contribution; stores still touch only the declared selected bytes.
template <unsigned Slots, class GetByte>
[[gnu::always_inline]] inline void write_gpr_row(const gpr_write& p, byte* row, GetByte get) {
    if (p.word.selected == 1) {
        const auto& b = p.stores[0];
        row[b.offset] = (b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask)) |
                        (get(b.input[0]) << b.shift[0]);
    } else if (p.word.bytes && p.word.selected > 1) {
        const auto& w = p.word;
        auto next = load_short_word(row + w.offset, w.bytes) & ~w.changed;
        // Shape-bounded assembly shares physical traversal between grouped and
        // single-group packets. Only the source-byte extraction differs.
        for (unsigned i = 0; i < Slots; ++i)
            if (i < w.selected)
                next |= std::uint64_t(get(w.input_shift[i] / 8)) << w.output_shift[i];
        for (unsigned i = 0; i < p.count; ++i)
            row[p.stores[i].offset] = byte(next >> (8 * (p.stores[i].offset - w.offset)));
    } else {
        for (unsigned i = 0; i < p.count; ++i) {
            const auto& b = p.stores[i];
            byte value = b.mask == 255 ? 0 : row[b.offset] & byte(~b.mask);
            for (unsigned j = 0; j < b.count; ++j)
                value |= get(b.input[j]) << b.shift[j];
            row[b.offset] = value;
        }
    }
}
template <unsigned Rows, unsigned Count, class Address>
[[gnu::always_inline]] inline std::uint64_t read_gpr_rows(const gpr_read& p, Address&& address,
                                                          std::uint64_t active) {
    if constexpr (Rows == 1)
        return active ? read_body<8, Count>(p, address(0)) : 0;
    std::uint64_t value = 0;
    if (!p.ordering.single_group()) {
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r)) {
                const auto row = address(r);
                for (unsigned i = 0; i < Count; ++i) {
                    const auto c = p.codes[i];
                    if (c.width)
                        value |= std::uint64_t((row[c.offset] >> c.shift) & ((1u << c.width) - 1))
                                 << (8 * p.ordering.template offset<Rows>(r, i));
                }
            }
    } else
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                value |= read_body<8, Count>(p, address(r)) << (r * p.slots * 8);
    return value;
}
template <unsigned Rows, unsigned Bytes, class Address>
[[gnu::always_inline]] inline std::uint64_t
read_gpr_transfer(const gpr_read& p, Address&& address, std::uint64_t active, std::size_t stride) {
    constexpr unsigned B = 8 / Rows;
    const auto& t = p.transfer;
    const unsigned bytes = Bytes ? Bytes : t.bytes;
    // Preparation proves this slot budget. Retain it here so narrow shapes do
    // not compile unreachable wide-tail loads and stores.
    __builtin_assume(bytes > 0 && bytes <= B);
    std::uint64_t value = 0;
    if (bytes == B && active == all_rows<Rows> && (Rows == 1 || stride == B))
        value = load_word<8>(address(0) + t.offset);
    else
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                value |= (bytes == B ? load_word<B>(address(r) + t.offset)
                                     : load_short_word(address(r) + t.offset, bytes))
                         << (r * p.slots * 8);
    return (value >> t.shift) & t.mask;
}
/// Row addresses are requested only for active rows. A nonzero stride promises
/// one strided allocation, enabling coalescing of a complete tight window.
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline std::uint64_t
read_gpr_body(const gpr_read& p, Address&& address, std::uint64_t active, std::size_t stride = 0) {
    constexpr unsigned B = 8 / Rows;
    if constexpr (Rows == 1)
        if (p.slots <= 1)
            return read_gpr_rows<Rows, 1>(p, address, active);
    if (p.transfer.bytes)
        return read_gpr_transfer<Rows, 0>(p, address, active, stride);
    // The ordinary endpoint chooses its slot bucket during preparation. Inline
    // point authors still avoid walking eight slots for short maps; multirow
    // bodies use the already-small shape bound without another hot dispatch.
    if constexpr (Rows == 1) {
        if (p.slots <= 2)
            return read_gpr_rows<Rows, 2>(p, address, active);
        if (p.slots <= 4)
            return read_gpr_rows<Rows, 4>(p, address, active);
    }
    return read_gpr_rows<Rows, B>(p, address, active);
}
template <unsigned Rows, unsigned Bytes, class Address>
[[gnu::always_inline]] inline void write_gpr_transfer(const gpr_write& p, Address&& address,
                                                      std::uint64_t input, std::uint64_t active,
                                                      std::size_t stride) {
    constexpr unsigned B = 8 / Rows;
    const auto& t = p.transfer;
    const unsigned bytes = Bytes ? Bytes : t.bytes;
    // Preparation proves this slot budget. Retain it here so narrow shapes do
    // not compile unreachable wide-tail loads and stores.
    __builtin_assume(bytes > 0 && bytes <= B);
    const auto changed = t.mask << t.shift;
    const auto encoded = (input & t.mask) << t.shift;
    if (bytes == B && active == all_rows<Rows> && (Rows == 1 || stride == B)) {
        auto row = address(0) + t.offset;
        const auto next =
            (changed == ~std::uint64_t(0) ? 0 : load_word<8>(row) & ~changed) | encoded;
        __builtin_memcpy(row, &next, 8);
    } else
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r)) {
                auto row = address(r) + t.offset;
                const auto mask = changed >> (r * p.read.slots * 8);
                const auto full = ~std::uint64_t(0) >> (64 - 8 * bytes);
                const auto old = (mask & full) == full ? 0 : load_short_word(row, bytes) & ~mask;
                const auto next = old | (encoded >> (r * p.read.slots * 8));
                if (bytes == B)
                    __builtin_memcpy(row, &next, B);
                else
                    store_short_word(row, next, bytes);
            }
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline void write_gpr_body(const gpr_write& p, Address&& address,
                                                  std::uint64_t input, std::uint64_t active,
                                                  std::size_t stride = 0) {
    constexpr unsigned B = 8 / Rows;
    if constexpr (Rows == 1)
        if (p.word.selected == 1) {
            if (active)
                write_gpr_single(p, address(0), input);
            return;
        }
    if (p.transfer.bytes) {
        write_gpr_transfer<Rows, 0>(p, address, input, active, stride);
        return;
    }
    if constexpr (Rows > 1) {
        if (!p.read.ordering.single_group()) {
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (std::uint64_t(1) << r))
                    write_gpr_row<B>(
                        p, address(r), [&](unsigned slot) __attribute__((always_inline)) {
                            return byte(input >>
                                        (8 * p.read.ordering.template offset<Rows>(r, slot)));
                        });
            return;
        }
    }
    for (unsigned r = 0; r < Rows; ++r)
        if (active & (std::uint64_t(1) << r)) {
            const auto row_input = input >> (r * p.read.slots * 8);
            write_gpr_row<B>(p, address(r), [&](unsigned slot) __attribute__((always_inline)) {
                return byte(row_input >> (8 * slot));
            });
        }
}
} // namespace ikea::tuplepack::detail
