#include <ikea/seriespack/layout_value.h>
#include <ikea/description.h>
namespace ikea::seriespack {
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
