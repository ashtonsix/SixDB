#include <ikea2/seriespack/representation.h>
#include <ikea2/seriespack/detail/wire.h>
namespace ikea2::seriespack {
std::expected<descriptor_bytes, error> encode_descriptor(const representation& r) {
    if (!valid(r))
        return std::unexpected(error::description);
    descriptor_bytes bytes{'S',
                           'P',
                           1,
                           static_cast<std::uint8_t>(r.storage == geometry::striped),
                           static_cast<std::uint8_t>(r.width),
                           static_cast<std::uint8_t>(r.heads),
                           0,
                           0};
    detail::store<8>(bytes.data() + 8, r.count);
    for (unsigned p = 0; p < 3; ++p)
        detail::store<8>(bytes.data() + 16 + p * 8, r.strides[p]);
    return bytes;
}
std::expected<representation, error> decode_descriptor(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 40 || bytes[0] != 'S' || bytes[1] != 'P' || bytes[2] != 1 || bytes[3] > 1 ||
        bytes[6] || bytes[7])
        return std::unexpected(error::description);
    representation r{bytes[4],
                     bytes[5],
                     bytes[3] ? geometry::striped : geometry::local,
                     detail::load<8>(bytes.data() + 8),
                     {}};
    for (unsigned p = 0; p < 3; ++p)
        r.strides[p] = detail::load<8>(bytes.data() + 16 + p * 8);
    if (!valid(r))
        return std::unexpected(error::description);
    return r;
}
} // namespace ikea2::seriespack
