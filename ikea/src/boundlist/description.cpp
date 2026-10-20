#include <algorithm>
#include <ikea/boundlist/description.h>
#include <ikea/boundlist/detail/bytes.h>

namespace ikea::boundlist::sidecar {
std::expected<layout, error> layout::prepare(description d) {
    if ((d.family != geometry::direct && d.family != geometry::width) ||
        (d.lengths != placement::grouped && d.lengths != placement::separated) ||
        d.byte_limit > slot_capacity * d.maximum ||
        (d.fixed_reservation && *d.fixed_reservation > d.maximum))
        return std::unexpected(error::description);
    layout l;
    l.description_ = d;
    l.fixed_ = d.fixed_reservation.has_value() || d.maximum == 0;
    l.used_ = !d.full && d.maximum != 0 && (!l.fixed_ || l.fixed_width() != 0);
    l.width_bytes_ = (l.fixed_ ? l.fixed_width() : d.maximum) <= 255 ? 1 : 2;
    l.offset_bytes_ = d.byte_limit <= 255 ? 1 : d.byte_limit <= 65535 ? 2 : 4;
    l.tombstones_ = d.immortal ? 0 : slot_capacity / 8;
    l.geometry_offset_ = l.tombstones_;
    l.directory_ = l.tombstones_ +
                   (l.fixed_ ? 0
                             : (d.family == geometry::direct ? slot_capacity : slot_capacity / 16) *
                                   l.offset_bytes_);
    l.control_bytes_ = !l.fixed_ && d.family == geometry::width ? 6 : 2;
    const bool width = !l.fixed_ && d.family == geometry::width;
    const unsigned group = 16 * l.width_bytes_;
    l.used_offset_ =
        width ? (d.lengths == placement::grouped ? group : slot_capacity * l.width_bytes_) : 0;
    l.stride_ = width     ? group * (1 + (l.used_ && d.lengths == placement::grouped))
                : l.used_ ? group
                          : 0;
    l.alignment_ = width && d.lengths == placement::grouped ? l.stride_ : l.width_bytes_;
    l.lengths_ = l.lengths_bytes(slot_capacity);
    return l;
}
unsigned layout::lengths_bytes(unsigned n) const noexcept {
    if (!n)
        return 0;
    const auto groups = (n + 15) / 16;
    if (!fixed_ && description_.family == geometry::width)
        return groups * stride_ +
               (used_ && description_.lengths == placement::separated ? used_offset_ : 0);
    return used_ ? groups * stride_ : 0;
}
std::array<byte, 8> layout::encode() const noexcept {
    std::array<byte, 8> out{};
    out[0] = byte(0x40 | unsigned(description_.family) | (unsigned(description_.lengths) << 1) |
                  (unsigned(description_.full) << 2) | (unsigned(description_.immortal) << 3) |
                  (unsigned(description_.fixed_reservation.has_value()) << 4));
    detail::store(out.data() + 1, description_.maximum);
    // 256 reservations of at most 65535 bytes fit in 24 bits. This is cold
    // schema storage; hot geometry retains native one/two/four-byte lanes.
    detail::store(out.data() + 3, std::uint16_t(description_.byte_limit));
    out[5] = byte(description_.byte_limit >> 16);
    detail::store(out.data() + 6, description_.fixed_reservation.value_or(0));
    return out;
}
std::expected<layout, error> layout::decode(bytes in) {
    if (in.size() != 8 || (std::to_integer<unsigned>(in[0]) >> 5) != 2)
        return std::unexpected(error::description);
    const auto flags = std::to_integer<unsigned>(in[0]);
    const auto fixed = detail::load<std::uint16_t>(in.data() + 6);
    if (!(flags & 16) && fixed != 0)
        return std::unexpected(error::description);
    description d{geometry(flags & 1),
                  placement((flags >> 1) & 1),
                  detail::load<std::uint16_t>(in.data() + 1),
                  detail::load<std::uint16_t>(in.data() + 3) |
                      (std::to_integer<unsigned>(in[5]) << 16),
                  (flags & 16) ? std::optional(fixed) : std::nullopt,
                  (flags & 4) != 0,
                  (flags & 8) != 0};
    return prepare(d);
}
} // namespace ikea::boundlist::sidecar
