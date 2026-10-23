#include <ikea/streampack/layout.h>
#include <ikea/intervals/detail/bytes.h>
#include <algorithm>
#include <ikea/description.h>

namespace ikea::streampack {
namespace wire = ikea::streampack::detail::compact::detail;
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::description:
        return "unknown, inconsistent or unsupported value layout";
    case error::capacity:
        return "borrowed storage or slot capacity is insufficient";
    case error::alignment:
        return "length storage violates the layout's alignment requirement";
    case error::overlap:
        return "storage planes, input or command/effect storage overlap";
    case error::contents:
        return "encoded geometry, lengths or control state are inconsistent";
    case error::retired:
        return "the selected value has been retired";
    case error::value:
        return "value length or reservation violates the layout's bounds";
    case error::effects:
        return "effect journal has insufficient available entries";
    case error::range:
        return "rank, range or selection coverage is invalid";
    }
    return "unknown StreamPack error";
}

std::expected<layout, error> layout::make(options d) {
    if (d.lengths != placement::grouped && d.lengths != placement::separated)
        return std::unexpected(error::description);
    auto g = ikea::streampack::detail::compact::detail::geometry_sizes::prepare(
        {d.family, d.maximum, d.byte_limit, d.fixed_reservation, d.immortal});
    if (!g)
        return std::unexpected(g.error());
    layout l(d, *g);
    l.used_ = !d.full && d.maximum != 0 && (!l.fixed() || l.fixed_width() != 0);
    l.used_bytes_ =
        std::min(unsigned(l.fixed() ? l.fixed_width() : d.maximum), d.byte_limit) <= 255 ? 1 : 2;
    const bool width = l.has_extent();
    const unsigned group = 16 * l.width_bytes();
    if (!width || !l.used_) {
        d.lengths = placement::grouped;
        l.description_.lengths = d.lengths;
    }
    l.used_offset_ =
        width ? (d.lengths == placement::grouped ? group : slot_capacity * l.width_bytes()) : 0;
    l.stride_ = width
                    ? group + (l.used_ && d.lengths == placement::grouped ? 16 * l.used_bytes_ : 0)
                : l.used_ ? 16 * l.used_bytes_
                          : 0;
    l.alignment_ = std::max(l.width_bytes(), unsigned(l.used_bytes_));
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
std::expected<std::size_t, error> layout::encode(ikea::mutable_bytes output) const {
    if (!ikea::description::write_header(output, {'S', 'K'}, 1, 8))
        return std::unexpected(error::capacity);
    auto out = output.subspan(ikea::description::header_size, 8);
    const auto &d = description_;
    out[0] = byte(unsigned(d.family) | (unsigned(d.lengths) << 2) |
                  (unsigned(d.full) << 3) | (unsigned(d.immortal) << 4) |
                  (unsigned(d.fixed_reservation.has_value()) << 5));
    wire::store(out.data() + 1, d.maximum);
    wire::store(out.data() + 3, std::uint16_t(d.byte_limit));
    out[5] = byte(d.byte_limit >> 16);
    wire::store(out.data() + 6, d.fixed_reservation.value_or(0));
    return encoded_size();
}
std::expected<layout, error> layout::decode(bytes input) {
    const auto framed = ikea::description::read_body(input, {'S', 'K'}, 1);
    if (!framed || framed->size() != 8)
        return std::unexpected(error::description);
    const auto in = *framed;
    const auto flags = std::to_integer<unsigned>(in[0]);
    if ((flags & 0xc0) || (flags & 3) == 3)
        return std::unexpected(error::description);
    const auto fixed = wire::load<std::uint16_t>(in.data() + 6);
    if (!(flags & 32) && fixed)
        return std::unexpected(error::description);
    auto result = make({geometry(flags & 3), placement((flags >> 2) & 1),
                        wire::load<std::uint16_t>(in.data() + 1),
                        wire::load<std::uint16_t>(in.data() + 3) |
                            (std::to_integer<unsigned>(in[5]) << 16),
                        flags & 32 ? std::optional(fixed) : std::nullopt,
                        bool(flags & 8), bool(flags & 16)});
    if (result) {
        std::array<byte, encoded_size()> canonical;
        (void)result->encode(canonical);
        if (!std::equal(canonical.begin(), canonical.end(), input.begin()))
            return std::unexpected(error::description);
    }
    return result;
}
} // namespace ikea::streampack
