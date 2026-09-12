#include <ikea/tuplepack/detail/native/packet.h>
#include <ikea/tuplepack/detail/packet_plan.h>

namespace ikea::tuplepack::detail {
std::array<byte, 64> read_packet_buffered(const packet_read& p, unsigned rows, const byte* first,
                                          std::size_t stride, std::uint64_t active) {
    std::array<byte, 64> result{};
#if defined(__aarch64__) || defined(__AVX2__)
    native::store_packet(result.data(), native::read(p, rows, first, stride, active));
#else
    const unsigned slots = 64 / rows;
    for (unsigned r = 0; r < rows; ++r) {
        if (!(active & (std::uint64_t(1) << r)))
            continue;
        for (unsigned i = 0; i < slots; ++i) {
            const auto c = p.codes[i];
            if (c.width)
                result[r * slots + i] =
                    (first[r * stride + c.offset] >> c.shift) & ((1u << c.width) - 1);
        }
    }
#endif
    return result;
}
void write_packet_buffered(const packet_write& p, unsigned rows, byte* first, std::size_t stride,
                           const std::array<byte, 64>& input, std::uint64_t active) {
#if defined(__aarch64__) || defined(__AVX2__)
    native::write(p, rows, first, stride, native::load_packet(input.data()), active);
#else
    const unsigned slots = 64 / rows;
    for (unsigned r = 0; r < rows; ++r) {
        if (!(active & (std::uint64_t(1) << r)))
            continue;
        auto row = first + r * stride;
        for (unsigned i = 0; i < p.place.count; ++i) {
            const unsigned offset = p.place.offsets[i];
            byte mask = 0, value = 0;
            for (unsigned j = 0; j < p.count; ++j) {
                const auto& b = p.stores[j];
                if (b.offset != offset)
                    continue;
                mask = b.mask;
                for (unsigned k = 0; k < b.count; ++k)
                    value |= input[r * slots + b.input[k]] << b.shift[k];
            }
            // Also reissue any preserved bytes inside the admitted compact span.
            row[offset] = (mask == 255 ? 0 : row[offset] & byte(~mask)) | value;
        }
    }
#endif
}
} // namespace ikea::tuplepack::detail

#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native {
IKEA_TUPLE_CC packet read(const detail::packet_read& p, unsigned rows, const byte* first,
                          std::size_t stride, std::uint64_t active) {
    switch (rows) {
    case 2:
        return read_body<2>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    case 4:
        return read_body<4>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    case 8:
        return read_body<8>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    case 16:
        return read_body<16>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    case 32:
        return read_body<32>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    case 64:
        return read_body<64>(p, [&](unsigned r) { return first + r * stride; }, active, stride);
    default:
        __builtin_unreachable();
    }
}
IKEA_TUPLE_CC void write(const detail::packet_write& p, unsigned rows, byte* first,
                         std::size_t stride, packet input, std::uint64_t active) {
    switch (rows) {
    case 2:
        write_body<2>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    case 4:
        write_body<4>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    case 8:
        write_body<8>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    case 16:
        write_body<16>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    case 32:
        write_body<32>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    case 64:
        write_body<64>(p, [&](unsigned r) { return first + r * stride; }, input, active, stride);
        return;
    default:
        __builtin_unreachable();
    }
}
} // namespace ikea::tuplepack::native
#endif
