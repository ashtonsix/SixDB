#include <ikea/tuplepack/detail/gpr.h>
namespace ikea::tuplepack::detail {
namespace {
template <unsigned Rows, unsigned Count>
std::uint64_t read_codes(const gpr_read& p, const byte* first, std::size_t stride,
                         std::uint64_t active) {
    return read_gpr_rows<Rows, Count>(p, [&](unsigned r) { return first + r * stride; }, active);
}
template <unsigned Rows, unsigned Bytes>
std::uint64_t word_read(const gpr_read& p, const byte* first, std::size_t stride,
                        std::uint64_t active) {
    return read_gpr_transfer<Rows, Bytes>(
        p, [&](unsigned r) { return first + r * stride; }, active, stride);
}
template <unsigned Rows, unsigned Bytes>
void word_write(const gpr_write& p, byte* first, std::size_t stride, std::uint64_t input,
                std::uint64_t active) {
    write_gpr_transfer<Rows, Bytes>(
        p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
}
void single_write(const gpr_write& p, byte* first, std::size_t, std::uint64_t input,
                  std::uint64_t active) {
    if (active)
        write_gpr_single(p, first, input);
}
// A common logical shape does not require a common machine ABI: point calls
// need neither stride nor active-mask arguments. Inline away this adapter when
// selecting the endpoint so repeated points retain their two-argument boundary.
template <unsigned Rows, auto Function> constexpr gpr_reader<Rows> read_entry() {
    if constexpr (Rows == 1)
        return [](const gpr_read& p, const byte* row) {
            [[clang::always_inline]] return Function(p, row, 0, 1);
        };
    else
        return Function;
}
template <unsigned Rows, auto Function> constexpr gpr_writer<Rows> write_entry() {
    if constexpr (Rows == 1)
        return [](const gpr_write& p, byte* row, std::uint64_t input) {
            [[clang::always_inline]] return Function(p, row, 0, input, 1);
        };
    else
        return Function;
}
} // namespace
// Preparation chooses the whole packet endpoint. In particular a one-code
// point call must not pay general word-length/shape selection at every access.
template <unsigned Rows> gpr_reader<Rows> select_gpr_reader(const gpr_read& p) {
    constexpr unsigned B = 8 / Rows;
    if constexpr (Rows == 1)
        if (p.slots <= 1)
            return read_entry<Rows, read_codes<Rows, 1>>();
    if (p.transfer.bytes) {
        if (p.transfer.bytes == 1)
            return read_entry<Rows, word_read<Rows, 1>>();
        if constexpr (B >= 2)
            if (p.transfer.bytes == 2)
                return read_entry<Rows, word_read<Rows, 2>>();
        if constexpr (B >= 4)
            if (p.transfer.bytes == 4)
                return read_entry<Rows, word_read<Rows, 4>>();
        if constexpr (B >= 8)
            if (p.transfer.bytes == 8)
                return read_entry<Rows, word_read<Rows, 8>>();
        if constexpr (B > 2)
            return read_entry<Rows, word_read<Rows, 0>>();
        else
            __builtin_unreachable(); // All positive lengths in this shape were handled.
    }
    if constexpr (B > 1)
        if (p.slots <= 1)
            return read_entry<Rows, read_codes<Rows, 1>>();
    if constexpr (B > 2)
        if (p.slots <= 2)
            return read_entry<Rows, read_codes<Rows, 2>>();
    if constexpr (B > 4)
        if (p.slots <= 4)
            return read_entry<Rows, read_codes<Rows, 4>>();
    return read_entry<Rows, read_codes<Rows, B>>();
}
template <unsigned Rows> gpr_writer<Rows> select_gpr_writer(const gpr_write& p) {
    constexpr unsigned B = 8 / Rows;
    if constexpr (Rows == 1)
        if (p.word.selected == 1)
            return write_entry<Rows, single_write>();
    if (p.transfer.bytes) {
        if (p.transfer.bytes == 1)
            return write_entry<Rows, word_write<Rows, 1>>();
        if constexpr (B >= 2)
            if (p.transfer.bytes == 2)
                return write_entry<Rows, word_write<Rows, 2>>();
        if constexpr (B >= 4)
            if (p.transfer.bytes == 4)
                return write_entry<Rows, word_write<Rows, 4>>();
        if constexpr (B >= 8)
            if (p.transfer.bytes == 8)
                return write_entry<Rows, word_write<Rows, 8>>();
        if constexpr (B > 2)
            return write_entry<Rows, word_write<Rows, 0>>();
        else
            __builtin_unreachable();
    }
    return write_entry<Rows, write_gpr<Rows>>();
}
template <unsigned Rows>
std::uint64_t read_gpr(const gpr_read& p, const byte* first, std::size_t stride,
                       std::uint64_t active) {
    return read_gpr_body<Rows>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
}
template <unsigned Rows>
void write_gpr(const gpr_write& p, byte* first, std::size_t stride, std::uint64_t input,
               std::uint64_t active) {
    write_gpr_body<Rows>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
}
#define IKEA_GPR_SHAPE(R)                                                                          \
    template gpr_reader<R> select_gpr_reader<R>(const gpr_read&);                                  \
    template gpr_writer<R> select_gpr_writer<R>(const gpr_write&);                                 \
    template std::uint64_t read_gpr<R>(const gpr_read&, const byte*, std::size_t, std::uint64_t);  \
    template void write_gpr<R>(const gpr_write&, byte*, std::size_t, std::uint64_t, std::uint64_t);
IKEA_GPR_SHAPE(1)
IKEA_GPR_SHAPE(2)
IKEA_GPR_SHAPE(4)
IKEA_GPR_SHAPE(8)
#undef IKEA_GPR_SHAPE
} // namespace ikea::tuplepack::detail
