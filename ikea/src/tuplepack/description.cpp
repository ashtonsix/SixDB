#include <ikea/tuplepack/description.h>
#include <algorithm>

namespace ikea::tuplepack {
std::expected<layout, error> layout::make(unsigned bytes, std::span<const code> codes) {
    if (!bytes || bytes > 64 || codes.empty() || codes.size() > 128)
        return std::unexpected(error::description);
    std::array<byte, 64> occupied{};
    for (auto c : codes) {
        if (c.offset >= bytes || !c.width || c.width > 8 || c.shift + c.width > 8)
            return std::unexpected(error::description);
        const byte mask = ((1u << c.width) - 1) << c.shift;
        if (occupied[c.offset] & mask)
            return std::unexpected(error::overlap);
        occupied[c.offset] |= mask;
    }
    layout result;
    result.bytes_ = bytes;
    result.count_ = codes.size();
    std::copy(codes.begin(), codes.end(), result.codes_.begin());
    return result;
}
std::expected<std::size_t, error> layout::encode(std::span<byte> out) const {
    if (out.size() < encoded_size())
        return std::unexpected(error::capacity);
    out[0] = 'T';
    out[1] = 'P';
    out[2] = 1;
    out[3] = bytes_;
    out[4] = count_;
    out[5] = count_ >> 8;
    for (unsigned i = 0; i < count_; ++i) {
        out[6 + 3 * i] = codes_[i].offset;
        out[7 + 3 * i] = codes_[i].shift;
        out[8 + 3 * i] = codes_[i].width;
    }
    return encoded_size();
}
std::expected<layout, error> layout::decode(std::span<const byte> in) {
    if (in.size() < 6 || in[0] != 'T' || in[1] != 'P' || in[2] != 1)
        return std::unexpected(error::description);
    const unsigned count = in[4] | unsigned(in[5]) << 8;
    if (count > 128 || in.size() != 6 + count * 3)
        return std::unexpected(error::description);
    std::array<code, 128> codes{};
    for (unsigned i = 0; i < count; ++i)
        codes[i] = {in[6 + 3 * i], in[7 + 3 * i], in[8 + 3 * i]};
    return make(in[3], std::span(codes).first(count));
}
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::description:
        return "invalid or unsupported physical description";
    case error::map:
        return "map length or code rank is invalid";
    case error::duplicate:
        return "a writer selects the same code more than once";
    case error::value:
        return "a selected input byte exceeds its code width";
    case error::range:
        return "row range or selection coverage is invalid";
    case error::stride:
        return "row stride is shorter than the unit extent";
    case error::capacity:
        return "borrowed storage or effect capacity is insufficient";
    case error::overflow:
        return "byte extent arithmetic overflows";
    case error::overlap:
        return "semantic destinations overlap";
    }
    return "unknown TuplePack error";
}
} // namespace ikea::tuplepack
