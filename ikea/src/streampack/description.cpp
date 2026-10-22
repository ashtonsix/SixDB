#include <ikea/streampack/description.h>
#include <ikea/intervals/detail/bytes.h>
#include <algorithm>

namespace ikea::streampack {
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::description:
        return "unknown, inconsistent or unsupported value layout";
    case error::capacity:
        return "borrowed storage or slot capacity is insufficient";
    case error::alignment:
        return "length storage violates the layout's alignment requirement";
    case error::alias:
        return "storage planes, input or command/effect storage overlap";
    case error::contents:
        return "encoded geometry, lengths or control state are inconsistent";
    case error::rank:
        return "rank or rank range is outside assigned slots";
    case error::retired:
        return "the selected value has been retired";
    case error::value:
        return "value length or reservation violates the layout's bounds";
    case error::effects:
        return "effect journal has insufficient available entries";
    case error::range:
        return "operation range or selection coverage is invalid";
    }
    return "unknown StreamPack error";
}

std::expected<layout, error> layout::prepare(description d) { return prepare_impl(d, false); }
std::expected<layout, error> layout::prepare_impl(description d, bool legacy) {
    if (d.lengths != placement::grouped && d.lengths != placement::separated)
        return std::unexpected(error::description);
    auto g = ikea::streampack::detail::compact::detail::geometry_sizes::prepare(
        {d.family, d.maximum, d.byte_limit, d.fixed_reservation, d.immortal}, legacy);
    if (!g)
        return std::unexpected(g.error());
    layout l(d, *g);
    l.used_ = !d.full && d.maximum != 0 && (!l.fixed() || l.fixed_width() != 0);
    l.used_bytes_ =
        legacy ? l.width_bytes()
               : (std::min(unsigned(l.fixed() ? l.fixed_width() : d.maximum), d.byte_limit) <= 255
                      ? 1
                      : 2);
    const bool width = l.has_extent();
    const unsigned group = 16 * l.width_bytes();
    if (!legacy && (!width || !l.used_)) {
        d.lengths = placement::grouped;
        l.description_.lengths = d.lengths;
    }
    l.used_offset_ =
        width ? (d.lengths == placement::grouped ? group : slot_capacity * l.width_bytes()) : 0;
    l.stride_ = width
                    ? group + (l.used_ && d.lengths == placement::grouped ? 16 * l.used_bytes_ : 0)
                : l.used_ ? 16 * l.used_bytes_
                          : 0;
    l.alignment_ = legacy && width && d.lengths == placement::grouped
                       ? l.stride_
                       : std::max(l.width_bytes(), unsigned(l.used_bytes_));
    return l;
}
unsigned layout::lengths_bytes(unsigned n) const noexcept {
    if (!n)
        return 0;
    const auto groups = (n + 15) / 16;
    if (has_extent()) {
        if (used_ && description_.lengths == placement::separated)
            return used_offset_ + groups * 16 * used_bytes_;
        return groups * stride_;
    }
    return used_ ? groups * stride_ : 0;
}
std::array<byte, 8> layout::encode() const noexcept {
    std::array<byte, 8> out{};
    ikea::streampack::detail::compact::detail::store(out.data() + 1, description_.maximum);
    ikea::streampack::detail::compact::detail::store(out.data() + 3, std::uint16_t(description_.byte_limit));
    out[5] = byte(description_.byte_limit >> 16);
    ikea::streampack::detail::compact::detail::store(out.data() + 6, description_.fixed_reservation.value_or(0));
    const auto &d = description_;
    out[0] = byte(geometry_.legacy ? 0x40 | unsigned(d.family) | (unsigned(d.lengths) << 1) |
                                         (unsigned(d.full) << 2) | (unsigned(d.immortal) << 3) |
                                         (unsigned(d.fixed_reservation.has_value()) << 4)
                                   : 0xc0 | unsigned(d.family) | (unsigned(d.lengths) << 2) |
                                         (unsigned(d.full) << 3) | (unsigned(d.immortal) << 4) |
                                         (unsigned(d.fixed_reservation.has_value()) << 5));
    return out;
}
std::expected<layout, error> layout::decode(bytes in) {
    if (in.size() != 8)
        return std::unexpected(error::description);
    const auto flags = std::to_integer<unsigned>(in[0]);
    const bool legacy = (flags >> 5) == 2;
    if ((!legacy && (flags >> 6) != 3) || (!legacy && (flags & 3) == 3))
        return std::unexpected(error::description);
    const auto fixed_bit = legacy ? 16u : 32u;
    const auto fixed = ikea::streampack::detail::compact::detail::load<std::uint16_t>(in.data() + 6);
    if (!(flags & fixed_bit) && fixed)
        return std::unexpected(error::description);
    auto result = prepare_impl({geometry(flags & (legacy ? 1 : 3)),
                                placement((flags >> (legacy ? 1 : 2)) & 1),
                                ikea::streampack::detail::compact::detail::load<std::uint16_t>(in.data() + 1),
                                ikea::streampack::detail::compact::detail::load<std::uint16_t>(in.data() + 3) |
                                    (std::to_integer<unsigned>(in[5]) << 16),
                                flags & fixed_bit ? std::optional(fixed) : std::nullopt,
                                bool(flags & (legacy ? 4 : 8)), bool(flags & (legacy ? 8 : 16))},
                               legacy);
    if (result && !legacy) {
        const auto canonical = result->encode();
        if (!std::equal(canonical.begin(), canonical.end(), in.begin()))
            return std::unexpected(error::description);
    }
    return result;
}
} // namespace ikea::streampack
