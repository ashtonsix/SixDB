#include <algorithm>
#include <ikea/boundlist/description.h>
#include <ikea/boundlist/detail/bytes.h>

namespace ikea::boundlist::sidecar {
std::expected<detail::geometry_sizes, error> detail::geometry_sizes::prepare(description d,
                                                                             bool legacy) {
    if ((d.family != geometry::absolute_ends && d.family != geometry::sizes &&
         (d.family != geometry::relative_ends || legacy)) ||
        d.extent_limit > slot_capacity * d.maximum || (d.fixed_size && *d.fixed_size > d.maximum))
        return std::unexpected(error::description);
    geometry_sizes g;
    g.legacy = legacy;
    g.fixed = d.fixed_size.has_value() || d.maximum == 0 || (!legacy && !d.extent_limit);
    const auto carrier = [](unsigned n) { return n <= 255 ? 1 : n <= 65535 ? 2 : 4; };
    const auto maximum = g.fixed ? d.fixed_size.value_or(0) : d.maximum;
    g.width_bytes = carrier(legacy ? maximum : std::min(unsigned(maximum), d.extent_limit));
    if (!g.fixed && d.family == geometry::relative_ends)
        g.width_bytes = carrier(std::min(16u * d.maximum, d.extent_limit));
    g.offset_bytes = d.extent_limit <= 255 ? 1 : d.extent_limit <= 65535 ? 2 : 4;
    g.tombstones = d.immortal ? 0 : slot_capacity / 8;
    g.directory =
        g.tombstones +
        (g.fixed ? 0
                 : (d.family == geometry::absolute_ends ? slot_capacity : slot_capacity / 16) *
                       g.offset_bytes);
    g.control_bytes = !g.fixed && d.family != geometry::absolute_ends ? 6 : 2;
    g.stride = !g.fixed && d.family != geometry::absolute_ends ? 16 * g.width_bytes : 0;
    return g;
}
std::expected<layout, error> layout::prepare(description d) { return prepare_impl(d, false); }
std::expected<layout, error> layout::prepare_impl(description d, bool legacy) {
    auto geometry = detail::geometry_sizes::prepare(d, legacy);
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
std::array<byte, 8> layout::encode() const noexcept {
    std::array<byte, 8> out{};
    out[0] = byte((geometry_.legacy ? 0x60 : 0x80) | unsigned(description_.family) |
                  (unsigned(description_.immortal) << 3) |
                  (unsigned(description_.fixed_size.has_value()) << 4));
    boundlist::detail::store(out.data() + 1, description_.maximum);
    // 256 reservations of at most 65535 bytes fit in 24 bits. This is cold
    // schema storage; hot geometry retains native one/two/four-byte lanes.
    boundlist::detail::store(out.data() + 3, std::uint16_t(description_.extent_limit));
    out[5] = byte(description_.extent_limit >> 16);
    boundlist::detail::store(out.data() + 6, description_.fixed_size.value_or(0));
    return out;
}
std::expected<layout, error> layout::decode(bytes in) {
    if (in.size() != 8)
        return std::unexpected(error::description);
    const auto flags = std::to_integer<unsigned>(in[0]);
    const bool legacy = (flags >> 5) == 3;
    if ((!legacy && (flags >> 5) != 4) || (flags & 4) || (flags & 3) > (legacy ? 1u : 2u))
        return std::unexpected(error::description);
    const auto fixed = boundlist::detail::load<std::uint16_t>(in.data() + 6);
    if (!(flags & 16) && fixed != 0)
        return std::unexpected(error::description);
    description d{geometry(flags & 3), boundlist::detail::load<std::uint16_t>(in.data() + 1),
                  boundlist::detail::load<std::uint16_t>(in.data() + 3) |
                      (std::to_integer<unsigned>(in[5]) << 16),
                  (flags & 16) ? std::optional(fixed) : std::nullopt, (flags & 8) != 0};
    return prepare_impl(d, legacy);
}
} // namespace ikea::boundlist::sidecar
