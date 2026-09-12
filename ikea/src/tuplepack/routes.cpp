#include <ikea/tuplepack/author/routes.h>
#include <algorithm>

namespace ikea::tuplepack::composition {
namespace {
bool valid(const bit_routes& bits) {
    return std::ranges::all_of(bits, [](int b) { return b >= -1 && b < 512; });
}
} // namespace
bit_routes zero_routes() noexcept {
    bit_routes result;
    result.fill(-1);
    return result;
}
bit_routes identity_routes() noexcept {
    bit_routes result;
    for (unsigned i = 0; i < 512; ++i)
        result[i] = i;
    return result;
}
std::expected<bit_routes, error> compose(const bit_routes& outer, const bit_routes& inner) {
    if (!valid(outer) || !valid(inner))
        return std::unexpected(error::map);
    bit_routes result;
    for (unsigned i = 0; i < 512; ++i)
        result[i] = outer[i] < 0 ? -1 : inner[outer[i]];
    return result;
}
std::expected<bit_routes, error> unite(const bit_routes& a, const bit_routes& b) {
    if (!valid(a) || !valid(b))
        return std::unexpected(error::map);
    bit_routes result;
    for (unsigned i = 0; i < 512; ++i) {
        if (a[i] >= 0 && b[i] >= 0 && a[i] != b[i])
            return std::unexpected(error::overlap);
        result[i] = a[i] < 0 ? b[i] : a[i];
    }
    return result;
}
std::expected<bit_routes, error> decoding_routes(const layout& format, std::span<const byte> map) {
    if (map.size() > 64)
        return std::unexpected(error::map);
    auto result = zero_routes();
    for (unsigned i = 0; i < map.size(); ++i) {
        if (map[i] == hole)
            continue;
        if (map[i] >= format.codes().size())
            return std::unexpected(error::map);
        const auto c = format.codes()[map[i]];
        for (unsigned b = 0; b < c.width; ++b)
            result[i * 8 + b] = c.offset * 8 + c.shift + b;
    }
    return result;
}
std::expected<route_plan, error> prepare_routes(const bit_routes& bits,
                                                std::span<route_term> storage) {
    if (!valid(bits))
        return std::unexpected(error::map);
    using ikea::tuplepack::detail::compile_shuffle;
    using ikea::tuplepack::detail::shuffle_description;
    std::array<shuffle_description, 16> descriptions{};
    std::array<bool, 16> left{};
    bool normalized = true, identity = true, rotating = false;
    for (unsigned i = 0; i < 64 && normalized; ++i) {
        if (bits[i * 8] < 0) {
            normalized = false;
            break;
        }
        const int source = bits[i * 8] / 8, rotation = bits[i * 8] % 8;
        for (unsigned b = 0; b < 8; ++b)
            normalized &= bits[i * 8 + b] == source * 8 + (int(b) + rotation) % 8;
        auto& p = descriptions[0];
        auto& plus = descriptions[1];
        auto& minus = descriptions[2];
        p.index[i] = source;
        p.mask[i] = 255;
        plus.index[i] = minus.index[i] = i;
        minus.shift[i] = -rotation;
        minus.mask[i] = 255u >> rotation;
        plus.shift[i] = rotation ? 8 - rotation : 0;
        plus.mask[i] = rotation ? byte(255u << (8 - rotation)) : 0;
        identity &= source == int(i) && rotation == 0;
        rotating |= rotation != 0;
    }
    unsigned count = 0;
    if (normalized) {
        count = identity ? 0 : rotating ? 3 : 1;
        left[1] = true;
    } else {
        descriptions = {};
        // At most eight contributions of each direction per output byte;
        // allocating a term is needed only when that byte's existing terms
        // are incompatible. Sixteen is therefore a sufficient fixed bound.
        for (unsigned i = 0; i < 64; ++i)
            for (unsigned b = 0; b < 8; ++b) {
                const int source = bits[i * 8 + b];
                if (source < 0)
                    continue;
                const int shift = int(b) - source % 8;
                const bool direction = shift >= 0;
                unsigned term = 0;
                for (; term < count; ++term)
                    if (left[term] == direction && (!descriptions[term].mask[i] ||
                                                    (descriptions[term].index[i] == source / 8 &&
                                                     descriptions[term].shift[i] == shift)))
                        break;
                if (term == count) {
                    ++count;
                    left[term] = direction;
                }
                auto& p = descriptions[term];
                p.index[i] = source / 8;
                p.shift[i] = shift;
                p.mask[i] |= 1u << b;
            }
    }
    if (storage.size() < count)
        return std::unexpected(error::capacity);
    for (unsigned i = 0; i < count; ++i)
        storage[i] = {compile_shuffle(descriptions[i], left[i]), left[i]};
    return route_plan{storage.first(count), normalized};
}
} // namespace ikea::tuplepack::composition
