#include <ikea/bec256/codec.h>
#include <ikea/bec256/detail/tables.h>
#include <algorithm>
#include <bit>

namespace ikea::bec256 {
std::expected<void, error> validate(std::span<const byte> body, unsigned population) noexcept {
    if (population > 256)
        return std::unexpected(error::population);
    if (body.size() > max_bytes)
        return std::unexpected(error::trailing);
    unsigned bit = 0;
    auto take = [&](unsigned width, unsigned &value) {
        if (bit + width > body.size() * 8)
            return false;
        value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= ((std::to_integer<unsigned>(body[(bit + i) / 8]) >> ((bit + i) % 8)) & 1) << i;
        bit += width;
        return true;
    };
    std::array<unsigned, 64> tree{};
    tree[1] = population;
    for (unsigned level = 1, half = 128; level != 32; level *= 2, half /= 2) {
        for (unsigned j = level; j != 2 * level; ++j) {
            const auto p = tree[j], alternatives = std::min(p, 2 * half - p);
            unsigned value;
            if (!take(std::bit_width(alternatives), value))
                return std::unexpected(error::truncated);
            if (value > alternatives)
                return std::unexpected(error::split);
            tree[2 * j] = value + (p > half ? p - half : 0);
            tree[2 * j + 1] = p - tree[2 * j];
        }
    }
    for (unsigned j = 32; j != 64; ++j) {
        unsigned value;
        if (!take(detail::byte_width[tree[j]], value))
            return std::unexpected(error::truncated);
        if (value >= detail::choices[tree[j]])
            return std::unexpected(error::rank);
    }
    if ((bit + 7) / 8 != body.size())
        return std::unexpected(error::trailing);
    if (bit % 8 && (std::to_integer<unsigned>(body.back()) >> (bit % 8)))
        return std::unexpected(error::padding);
    return {};
}
std::expected<const_view, error> const_view::attach(std::span<const byte> storage,
                                                     unsigned bytes, unsigned population) noexcept {
    if (bytes > storage.size())
        return std::unexpected(error::capacity);
    if (population > positions)
        return std::unexpected(error::population);
    if (bytes > max_bytes)
        return std::unexpected(error::trailing);
    return const_view(storage, bytes, population);
}
std::expected<void, error> const_view::validate() const noexcept {
    return bec256::validate(storage_.first(bytes_), population_);
}
std::expected<reader, error> bind_reader(const_view input) noexcept {
    if (auto result = input.validate(); !result)
        return std::unexpected(result.error());
    return reader(input);
}
std::expected<reader, error> bind_reader(std::span<const byte> storage, unsigned bytes,
                                          unsigned population) noexcept {
    const auto placed = const_view::attach(storage, bytes, population);
    if (!placed)
        return std::unexpected(placed.error());
    return bind_reader(*placed);
}
std::string_view describe(error value) noexcept {
    switch (value) {
    case error::population:
        return "population is outside 0..256 or differs from input";
    case error::truncated:
        return "body ends inside an encoded field";
    case error::split:
        return "bisection population is out of range";
    case error::rank:
        return "enumerative byte rank is out of range";
    case error::padding:
        return "nonzero final padding bits";
    case error::trailing:
        return "length is not the exact headless body length";
    case error::capacity:
        return "destination or readable extent is too small";
    case error::effects:
        return "effect journal needs one available entry";
    case error::overlap:
        return "destination overlaps command or effect storage";
    }
    return "unknown Bec256 error";
}
} // namespace ikea::bec256
