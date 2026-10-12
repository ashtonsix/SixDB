#include <algorithm>
#include <ikea/streampack/description.h>
#include <ikea/streampack/detail/bytes.h>

namespace ikea::streampack {
std::expected<layout, error> layout::prepare(description d) {
    if ((d.family != geometry::direct && d.family != geometry::width) ||
        (d.lengths != placement::grouped && d.lengths != placement::separated) ||
        (d.capacity != 128 && d.capacity != 256 && d.capacity != 512) ||
        d.byte_limit > std::uint32_t(d.capacity) * d.maximum ||
        (d.fixed_reservation && *d.fixed_reservation > d.maximum))
        return std::unexpected(error::description);
    layout l;
    l.description_ = d;
    l.fixed_ = d.fixed_reservation.has_value() || d.maximum == 0;
    l.used_ = !d.full && d.maximum != 0 && (!l.fixed_ || l.fixed_width() != 0);
    l.width_bytes_ = (l.fixed_ ? l.fixed_width() : d.maximum) <= 255 ? 1 : 2;
    l.offset_bytes_ = d.byte_limit <= 255 ? 1 : d.byte_limit <= 65535 ? 2 : 4;
    l.tombstones_ = d.immortal ? 0 : d.capacity / 8;
    l.geometry_offset_ = l.tombstones_;
    l.directory_ =
        l.tombstones_ +
        (l.fixed_ ? 0
                  : (d.family == geometry::direct ? d.capacity : (d.capacity + 15) / 16) *
                        l.offset_bytes_);
    l.control_bytes_ = !l.fixed_ && d.family == geometry::width ? 6 : 2;
    const bool width = !l.fixed_ && d.family == geometry::width;
    const unsigned group = 16 * l.width_bytes_;
    l.used_offset_ =
        width ? (d.lengths == placement::grouped ? group : d.capacity * l.width_bytes_) : 0;
    l.stride_ = width     ? group * (1 + (l.used_ && d.lengths == placement::grouped))
                : l.used_ ? group
                          : 0;
    l.alignment_ = width && d.lengths == placement::grouped ? l.stride_ : l.width_bytes_;
    l.lengths_ = l.lengths_bytes(d.capacity);
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
std::array<byte, 16> layout::encode() const noexcept {
    std::array<byte, 16> out{};
    out[0] = byte{1};
    out[1] = byte(description_.family);
    out[2] = byte(description_.lengths);
    out[3] = byte(unsigned(description_.full) | (unsigned(description_.immortal) << 1));
    detail::store(out.data() + 4, description_.capacity);
    detail::store(out.data() + 6, description_.maximum);
    detail::store(out.data() + 8, description_.byte_limit);
    detail::store(out.data() + 12, description_.fixed_reservation
                                       ? std::uint32_t(*description_.fixed_reservation)
                                       : UINT32_MAX);
    return out;
}
std::expected<layout, error> layout::decode(bytes in) {
    if (in.size() != 16 || in[0] != byte{1} || std::to_integer<unsigned>(in[3]) > 3)
        return std::unexpected(error::description);
    const auto fixed = detail::load<std::uint32_t>(in.data() + 12);
    if (fixed != UINT32_MAX && fixed > 65535)
        return std::unexpected(error::description);
    description d{geometry(in[1]),
                  placement(in[2]),
                  detail::load<std::uint16_t>(in.data() + 4),
                  detail::load<std::uint16_t>(in.data() + 6),
                  detail::load<std::uint32_t>(in.data() + 8),
                  fixed == UINT32_MAX ? std::nullopt : std::optional(std::uint16_t(fixed)),
                  (std::to_integer<unsigned>(in[3]) & 1) != 0,
                  (std::to_integer<unsigned>(in[3]) & 2) != 0};
    return prepare(d);
}
} // namespace ikea::streampack
