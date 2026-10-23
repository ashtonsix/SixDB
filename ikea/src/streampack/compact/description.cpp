#include <algorithm>
#include <ikea/description.h>
#include <ikea/streampack/detail/compact/description.h>
#include <ikea/intervals/detail/bytes.h>

namespace ikea::streampack::detail::compact {
std::expected<detail::geometry_sizes, error> detail::geometry_sizes::prepare(description d) {
    if ((d.family != geometry::absolute_ends && d.family != geometry::sizes &&
         d.family != geometry::relative_ends) ||
        d.extent_limit > interval_capacity * d.maximum || (d.fixed_size && *d.fixed_size > d.maximum))
        return std::unexpected(error::description);
    geometry_sizes g;
    g.fixed = d.fixed_size.has_value() || d.maximum == 0 || !d.extent_limit;
    const auto carrier = [](unsigned n) { return n <= 255 ? 1 : n <= 65535 ? 2 : 4; };
    const auto maximum = g.fixed ? d.fixed_size.value_or(0) : d.maximum;
    g.width_bytes = carrier(std::min(unsigned(maximum), d.extent_limit));
    if (!g.fixed && d.family == geometry::relative_ends)
        g.width_bytes = carrier(std::min(16u * d.maximum, d.extent_limit));
    g.offset_bytes = d.extent_limit <= 255 ? 1 : d.extent_limit <= 65535 ? 2 : 4;
    g.tombstones = d.immortal ? 0 : interval_capacity / 8;
    g.directory =
        g.tombstones +
        (g.fixed ? 0
                 : (d.family == geometry::absolute_ends ? interval_capacity : interval_capacity / 16) *
                       g.offset_bytes);
    g.control_bytes = !g.fixed && d.family != geometry::absolute_ends ? 6 : 2;
    g.stride = !g.fixed && d.family != geometry::absolute_ends ? 16 * g.width_bytes : 0;
    return g;
}
std::expected<layout, error> layout::make(description d) {
    auto geometry = detail::geometry_sizes::prepare(d);
    if (!geometry)
        return std::unexpected(geometry.error());
    layout l;
    l.description_ = d;
    l.geometry_ = *geometry;
    return l;
}
unsigned layout::lengths_bytes(unsigned n) const noexcept {
    if (!n)
        return 0;
    return ((n + 15) / 16) * group_stride();
}
std::expected<std::size_t, error> layout::encode(ikea::mutable_bytes output) const {
    if (!ikea::description::write_header(output, {'S', 'G'}, 1, 8))
        return std::unexpected(error::capacity);
    auto out = output.subspan(ikea::description::header_size, 8);
    out[0] = byte(unsigned(description_.family) | (unsigned(description_.immortal) << 3) |
                  (unsigned(description_.fixed_size.has_value()) << 4));
    compact::detail::store(out.data() + 1, description_.maximum);
    compact::detail::store(out.data() + 3, std::uint16_t(description_.extent_limit));
    out[5] = byte(description_.extent_limit >> 16);
    compact::detail::store(out.data() + 6, description_.fixed_size.value_or(0));
    return encoded_size();
}
std::expected<layout, error> layout::decode(bytes input) {
    const auto framed = ikea::description::read_body(input, {'S', 'G'}, 1);
    if (!framed || framed->size() != 8)
        return std::unexpected(error::description);
    const auto in = *framed;
    const auto flags = std::to_integer<unsigned>(in[0]);
    if ((flags & 0xe4) || (flags & 3) > 2)
        return std::unexpected(error::description);
    const auto fixed = compact::detail::load<std::uint16_t>(in.data() + 6);
    if (!(flags & 16) && fixed)
        return std::unexpected(error::description);
    auto result = make({geometry(flags & 3), compact::detail::load<std::uint16_t>(in.data() + 1),
                           compact::detail::load<std::uint16_t>(in.data() + 3) |
                               (std::to_integer<unsigned>(in[5]) << 16),
                           flags & 16 ? std::optional(fixed) : std::nullopt, bool(flags & 8)});
    if (result) {
        std::array<byte, encoded_size()> canonical;
        (void)result->encode(canonical);
        if (!std::equal(canonical.begin(), canonical.end(), input.begin()))
            return std::unexpected(error::description);
    }
    return result;
}
} // namespace ikea::streampack::detail::compact
