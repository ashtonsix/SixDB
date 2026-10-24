#include <ikea/bec/author/bec4096.h>

namespace ikea::bec4096 {
namespace detail {
struct access {
    static reader make(const_view input) {
        struct frame f;
        read_frame(input.storage(), input.bytes(), input.population(), f);
        return reader(input, f);
    }
    static std::uint8_t *data(encoding &e) { return reinterpret_cast<std::uint8_t *>(e.data_.data()); }
    static void bytes(encoding &e, unsigned n) { e.bytes_ = n; }
};
} // namespace detail
namespace {
// Bit reader over an exact span; reports truncation instead of reading beyond.
struct checked_bits {
    std::span<const byte> body;
    unsigned bit = 0;
    bool take(unsigned width, unsigned &value) {
        if (bit + width > body.size() * 8)
            return false;
        value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= ((std::to_integer<unsigned>(body[(bit + i) / 8]) >> ((bit + i) % 8)) & 1) << i;
        bit += width;
        return true;
    }
};
// One Bec256 stream starting at bits.bit; leaves bits.bit at its end.
std::expected<void, error> validate_block(checked_bits &bits, unsigned population) {
    std::array<unsigned, 64> tree{};
    tree[1] = population;
    for (unsigned level = 1, half = 128; level != 32; level *= 2, half /= 2)
        for (unsigned j = level; j != 2 * level; ++j) {
            const auto p = tree[j], alternatives = std::min(p, 2 * half - p);
            unsigned value;
            if (!bits.take(std::bit_width(alternatives), value))
                return std::unexpected(error::truncated);
            if (value > alternatives)
                return std::unexpected(error::split);
            tree[2 * j] = value + detail::lower(p, half);
            tree[2 * j + 1] = p - tree[2 * j];
        }
    for (unsigned j = 32; j != 64; ++j) {
        unsigned value;
        if (!bits.take(ikea::bec::detail::byte_width[tree[j]], value))
            return std::unexpected(error::truncated);
        if (value >= ikea::bec::detail::choices[tree[j]])
            return std::unexpected(error::rank);
    }
    return {};
}
} // namespace

std::expected<void, error> validate(std::span<const byte> body, unsigned population) noexcept {
    if (population > positions)
        return std::unexpected(error::population);
    if (body.size() > max_bytes)
        return std::unexpected(error::trailing);
    checked_bits bits{body};
    std::array<unsigned, 32> tree{};
    tree[1] = population;
    for (unsigned i = 1; i < 16; ++i) {
        const unsigned half = 2048u >> (std::bit_width(i) - 1), p = tree[i];
        const unsigned alternatives = std::min(p, 2 * half - p);
        unsigned value;
        if (!bits.take(std::bit_width(alternatives), value))
            return std::unexpected(error::truncated);
        if (value > alternatives)
            return std::unexpected(error::split);
        tree[2 * i] = value + detail::lower(p, half);
        tree[2 * i + 1] = p - tree[2 * i];
    }
    std::array<unsigned, 16> length{};
    for (unsigned k = 0; k < 15; ++k) {
        const unsigned c = tree[16 + k];
        unsigned value;
        if (!bits.take(detail::length_width[c], value))
            return std::unexpected(error::truncated);
        if (value > unsigned(detail::max_bits[c] - detail::min_bits[c]))
            return std::unexpected(error::length);
        length[k] = detail::min_bits[c] + value;
    }
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned start = bits.bit;
        if (auto ok = validate_block(bits, tree[16 + k]); !ok)
            return ok;
        if (k < 15 && bits.bit - start != length[k])
            return std::unexpected(error::length);
    }
    if ((bits.bit + 7) / 8 != body.size())
        return std::unexpected(error::trailing);
    if (bits.bit % 8 && (std::to_integer<unsigned>(body.back()) >> (bits.bit % 8)))
        return std::unexpected(error::padding);
    return {};
}


std::expected<const_view, error> const_view::attach(std::span<const byte> storage, unsigned bytes,
                                                     unsigned population) noexcept {
    if (bytes > storage.size())
        return std::unexpected(error::capacity);
    if (population > positions)
        return std::unexpected(error::population);
    if (bytes > max_bytes)
        return std::unexpected(error::trailing);
    return const_view(storage, bytes, population);
}
std::expected<void, error> const_view::validate() const noexcept {
    return bec4096::validate(storage_.first(bytes_), population_);
}
reader bind_reader_unchecked(const_view input) noexcept { return detail::access::make(input); }
std::expected<reader, error> bind_reader(const_view input) noexcept {
    if (auto ok = input.validate(); !ok)
        return std::unexpected(ok.error());
    return detail::access::make(input);
}
std::expected<reader, error> bind_reader(std::span<const byte> storage, unsigned bytes,
                                         unsigned population) noexcept {
    const auto placed = const_view::attach(storage, bytes, population);
    if (!placed)
        return std::unexpected(placed.error());
    return bind_reader(*placed);
}

void decode(const reader &input, std::span<byte, 512> output) noexcept {
    detail::fused::decode_all(input, to_u8(output).data());
}
void decode(const reader &input, unsigned block, std::span<byte, 32> output) noexcept {
    detail::decode_block(input, block, to_u8(output).data());
}
void counts(const reader &input, unsigned block, std::span<std::uint8_t, 32> output) noexcept {
    detail::block_counts(input, block, output.data());
}
void counts(const reader &input, std::span<std::uint8_t, 512> output) noexcept {
    detail::fused::counts_all(input, output.data());
}
unsigned rank(const reader &input, unsigned position) noexcept {
    return detail::rank(input, position);
}
unsigned population(const reader &input, unsigned first, unsigned count) noexcept {
    return detail::rank(input, first + count) - detail::rank(input, first);
}

std::expected<encoding, error> encoding::make(std::span<const byte, 512> input,
                                              unsigned population) noexcept {
    detail::plan plan;
    if (detail::prepare(to_u8(input).data(), plan) != population)
        return std::unexpected(error::population);
    encoding result;
    const unsigned n = (plan.bits + 7) / 8;
    detail::access::bytes(result, n);
    if (n)
        detail::emit(plan, detail::access::data(result));
    return result;
}
std::expected<void, error> encoding::admit_write(const view &target, std::size_t offset,
                                                 const journal &effects) const noexcept {
    return ikea::bec::detail::admit_write(target, offset, bytes_, effects, this, sizeof(*this));
}
std::expected<void, error> encoding::write(view &target, std::size_t offset,
                                           journal &effects) const noexcept {
    if (auto admitted = admit_write(target, offset, effects); !admitted)
        return admitted;
    write_unchecked(target, offset, effects);
    return {};
}
std::expected<unsigned, error> encode(std::span<const byte, 512> input, unsigned population,
                                      view &target, std::size_t offset, journal &effects) noexcept {
    detail::plan plan;
    if (detail::prepare(to_u8(input).data(), plan) != population)
        return std::unexpected(error::population);
    const unsigned n = (plan.bits + 7) / 8;
    if (auto admitted = ikea::bec::detail::admit_write(target, offset, n, effects, input.data(), 512);
        !admitted)
        return std::unexpected(admitted.error());
    if (n) {
        effects.before(target, 0, offset, n);
        detail::emit(plan, reinterpret_cast<std::uint8_t *>(target.storage.data()) + offset);
    }
    return n;
}
} // namespace ikea::bec4096
