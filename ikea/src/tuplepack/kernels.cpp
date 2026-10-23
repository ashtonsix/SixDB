#include <ikea/tuplepack/author/native.h>

namespace ikea::tuplepack::detail {
std::array<std::uint8_t, 64> read_buffered(const read64& p, const std::uint8_t* row) {
#if defined(__aarch64__) || defined(__AVX2__)
    std::array<std::uint8_t, 64> result;
    native::store_packet(result.data(), native::read_body(p, row));
    return result;
#else
    return read_body<64>(p, row);
#endif
}
void write_buffered(const write64& p, std::uint8_t* row, const std::array<std::uint8_t, 64>& input) {
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
IKEA_TUPLE_CC packet read(const detail::read64& p, const std::uint8_t* row) {
    return read_body(p, row);
}
IKEA_TUPLE_CC void write(const detail::write64& p, std::uint8_t* row, packet input) {
    write_body(p, row, input);
}
} // namespace ikea::tuplepack::native
#endif
