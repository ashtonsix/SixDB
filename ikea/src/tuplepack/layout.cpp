#include <ikea/tuplepack/layout.h>
#include <algorithm>
#include <ikea/description.h>

namespace ikea::tuplepack {
std::expected<layout, error> layout::make(unsigned bytes, std::span<const code> codes) {
    if (!bytes || bytes > 64 || codes.empty() || codes.size() > 128)
        return std::unexpected(error::description);
    std::array<std::uint8_t, 64> occupied{};
    for (auto c : codes) {
        if (c.offset >= bytes || !c.width || c.width > 8 || c.shift + c.width > 8)
            return std::unexpected(error::description);
        const std::uint8_t mask = ((1u << c.width) - 1) << c.shift;
        if (occupied[c.offset] & mask)
            return std::unexpected(error::overlap);
        occupied[c.offset] |= mask;
    }
    layout result;
    result.bytes_ = bytes;
    result.count_ = codes.size();
    std::copy(codes.begin(), codes.end(), result.codes_.begin());
    // FNV-1a over the canonical law, then a splitmix64 finalizer.
    std::uint64_t h = 0xcbf29ce484222325;
    auto mix = [&](unsigned value) { h = (h ^ value) * 0x100000001b3; };
    mix(bytes);
    mix(unsigned(codes.size()));
    for (auto c : codes) {
        mix(c.offset);
        mix(c.shift);
        mix(c.width);
    }
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9;
    h = (h ^ (h >> 27)) * 0x94d049bb133111eb;
    result.identity_ = h ^ (h >> 31);
    return result;
}
std::expected<std::size_t, error> layout::encode(ikea::mutable_bytes out) const {
    const auto body_size = 1 + codes().size() * 3;
    if (!ikea::description::write_header(out, {'T', 'P'}, 2, body_size))
        return std::unexpected(error::capacity);
    out[5] = ikea::byte(bytes_);
    for (unsigned i = 0; i < count_; ++i) {
        out[6 + 3 * i] = ikea::byte(codes_[i].offset);
        out[7 + 3 * i] = ikea::byte(codes_[i].shift);
        out[8 + 3 * i] = ikea::byte(codes_[i].width);
    }
    return encoded_size();
}
std::expected<layout, error> layout::decode(ikea::bytes in) {
    const auto body = ikea::description::read_body(in, {'T', 'P'}, 2);
    if (!body || body->empty() || (body->size() - 1) % 3)
        return std::unexpected(error::description);
    const auto count = (body->size() - 1) / 3;
    if (!count || count > 128)
        return std::unexpected(error::description);
    std::array<code, 128> codes{};
    for (unsigned i = 0; i < count; ++i)
        codes[i] = {std::to_integer<std::uint8_t>((*body)[1 + 3 * i]),
                    std::to_integer<std::uint8_t>((*body)[2 + 3 * i]),
                    std::to_integer<std::uint8_t>((*body)[3 + 3 * i])};
    return make(std::to_integer<unsigned>((*body)[0]), std::span(codes).first(count));
}
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::description:
        return "invalid or unsupported physical description";
    case error::map:
        return "map length, code rank or packet groups are invalid";
    case error::duplicate:
        return "a writer selects the same code more than once";
    case error::value:
        return "a selected input byte exceeds its code width";
    case error::range:
        return "row range or selection coverage is invalid";
    case error::stride:
        return "row stride is shorter than the unit extent";
    case error::capacity:
        return "borrowed storage capacity is insufficient";
    case error::effects:
        return "effect journal is too small";
    case error::overflow:
        return "byte extent arithmetic overflows";
    case error::overlap:
        return "semantic destinations overlap";
    }
    return "unknown TuplePack error";
}
} // namespace ikea::tuplepack
