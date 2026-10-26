#include <ikea/seriespack/layout_value.h>
#include <ikea/description.h>
#include <limits>
namespace ikea::seriespack {
std::expected<storage_requirements, error> layout_value::required_storage(std::size_t rows) const noexcept {
    if (!valid(*this)) return std::unexpected(error::description);
    const unsigned payload = width - heads;
    storage_requirements result;
    result.tile_rows = storage == geometry::local ? 8 : 32 * (8 / std::gcd(payload % 8, 8u));
    result.tile_bytes = {result.tile_rows * payload / 8,
                         heads >= 8 ? result.tile_rows : 0, heads == 16 ? result.tile_rows : 0};
    const auto tiles = rows / result.tile_rows + (rows % result.tile_rows != 0);
    for (unsigned p = 0; p < 3; ++p) {
        const auto bytes = result.tile_bytes[p];
        if (bytes && tiles > std::numeric_limits<std::size_t>::max() / bytes)
            return std::unexpected(error::overflow);
        result.plane_bytes[p] = tiles * bytes;
    }
    return result;
}
std::expected<std::size_t, error> layout_value::encode(ikea::mutable_bytes output) const {
    if (!valid(*this))
        return std::unexpected(error::description);
    if (!ikea::description::write_header(output, {'S', 'P'}, 2, 3))
        return std::unexpected(error::capacity);
    output[5] = ikea::byte(width);
    output[6] = ikea::byte(heads);
    output[7] = ikea::byte(storage == geometry::striped);
    return encoded_size();
}
std::expected<layout_value, error> layout_value::decode(ikea::bytes input) {
    const auto body = ikea::description::read_body(input, {'S', 'P'}, 2);
    if (!body || body->size() != 3 || std::to_integer<unsigned>((*body)[2]) > 1)
        return std::unexpected(error::description);
    const layout_value law{std::to_integer<unsigned>((*body)[0]),
                           std::to_integer<unsigned>((*body)[1]),
                           (*body)[2] == ikea::byte{0} ? geometry::local : geometry::striped};
    if (!valid(law))
        return std::unexpected(error::description);
    return law;
}
} // namespace ikea::seriespack
