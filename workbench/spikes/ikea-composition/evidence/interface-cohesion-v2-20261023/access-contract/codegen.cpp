#include <ikea/tuplepack/author/native.h>
namespace tp = ikea::tuplepack;
using Access = tp::author::read_access;
using P = tp::native::packet;
extern "C" P read_strict(const tp::detail::packet_read* p, const std::uint8_t* data,
                         std::uint64_t active, std::size_t stride) {
    return tp::native::read_body<64>(*p, [&](unsigned r) { return data + r * stride; }, active, stride);
}
extern "C" P read_window(const tp::detail::packet_read* p, const std::uint8_t* data,
                         std::uint64_t active, std::size_t stride) {
    return tp::native::read_body<64, Access::common_window>(
        *p, [&](unsigned r) { return data + r * stride; }, active, stride);
}
extern "C" P read_full_strict(const tp::detail::packet_read* p, const std::uint8_t* data,
                              std::size_t stride) {
    return tp::native::read_body<64>(*p, [&](unsigned r) { return data + r * stride; }, ~0ull, stride);
}
extern "C" P read_full_window(const tp::detail::packet_read* p, const std::uint8_t* data,
                              std::size_t stride) {
    return tp::native::read_body<64, Access::common_window>(
        *p, [&](unsigned r) { return data + r * stride; }, ~0ull, stride);
}
extern "C" void write_strict(const tp::detail::packet_write* p, std::uint8_t* data,
                             P input, std::uint64_t active, std::size_t stride) {
    tp::native::write_body<64>(*p, [&](unsigned r) { return data + r * stride; }, input, active, stride);
}
extern "C" void write_window(const tp::detail::packet_write* p, std::uint8_t* data,
                             P input, std::uint64_t active, std::size_t stride) {
    tp::native::write_body<64, Access::common_window>(
        *p, [&](unsigned r) { return data + r * stride; }, input, active, stride);
}
