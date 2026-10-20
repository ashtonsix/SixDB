#include <ikea/streampack/description.h>
#include <ikea/boundlist/detail/bytes.h>

namespace ikea::streampack {
std::expected<layout, error> layout::prepare(description d) {
    if (d.lengths != placement::grouped && d.lengths != placement::separated)
        return std::unexpected(error::description);
    auto g = boundlist::sidecar::detail::geometry_sizes::prepare(
        {d.family, d.maximum, d.byte_limit, d.fixed_reservation, d.immortal});
    if (!g)
        return std::unexpected(g.error());
    layout l(d, *g);
    l.used_ = !d.full && d.maximum != 0 && (!l.fixed() || l.fixed_width() != 0);
    const bool width = !l.fixed() && d.family == geometry::width;
    const unsigned group = 16 * l.width_bytes();
    l.used_offset_ =
        width ? (d.lengths == placement::grouped ? group : slot_capacity * l.width_bytes()) : 0;
    l.stride_ = width     ? group * (1 + (l.used_ && d.lengths == placement::grouped))
                : l.used_ ? group
                          : 0;
    l.alignment_ = width && d.lengths == placement::grouped ? l.stride_ : l.width_bytes();
    return l;
}
unsigned layout::lengths_bytes(unsigned n) const noexcept {
    if (!n)
        return 0;
    const auto groups = (n + 15) / 16;
    if (!fixed() && description_.family == geometry::width)
        return groups * stride_ +
               (used_ && description_.lengths == placement::separated ? used_offset_ : 0);
    return used_ ? groups * stride_ : 0;
}
std::array<byte, 8> layout::encode() const noexcept {
    std::array<byte, 8> out{};
    boundlist::detail::store(out.data() + 1, description_.maximum);
    boundlist::detail::store(out.data() + 3, std::uint16_t(description_.byte_limit));
    out[5] = byte(description_.byte_limit >> 16);
    boundlist::detail::store(out.data() + 6, description_.fixed_reservation.value_or(0));
    out[0] = byte(0x40 | unsigned(description_.family) | (unsigned(description_.lengths) << 1) |
                  (unsigned(description_.full) << 2) | (unsigned(description_.immortal) << 3) |
                  (unsigned(description_.fixed_reservation.has_value()) << 4));
    return out;
}
std::expected<layout, error> layout::decode(bytes in) {
    if (in.size() != 8 || (std::to_integer<unsigned>(in[0]) >> 5) != 2)
        return std::unexpected(error::description);
    const auto flags = std::to_integer<unsigned>(in[0]);
    const auto fixed = boundlist::detail::load<std::uint16_t>(in.data() + 6);
    if (!(flags & 16) && fixed)
        return std::unexpected(error::description);
    return prepare({geometry(flags & 1), placement((flags >> 1) & 1),
                    boundlist::detail::load<std::uint16_t>(in.data() + 1),
                    boundlist::detail::load<std::uint16_t>(in.data() + 3) |
                        (std::to_integer<unsigned>(in[5]) << 16),
                    flags & 16 ? std::optional(fixed) : std::nullopt, bool(flags & 4),
                    bool(flags & 8)});
}
} // namespace ikea::streampack
