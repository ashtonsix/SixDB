#include <ikea/tuplepack/detail/packet_plan.h>
#include <algorithm>
#include <bit>

namespace ikea::tuplepack::detail {
namespace {
template <unsigned N>
std::expected<scalar_read<N>, error> read(const layout& format, std::span<const byte> map) {
    if (map.size() > N)
        return std::unexpected(error::map);
    scalar_read<N> p;
    p.bytes = format.bytes();
    p.slots = map.size();
    for (unsigned i = 0; i < map.size(); ++i) {
        if (map[i] == hole)
            continue;
        if (map[i] >= format.codes().size())
            return std::unexpected(error::map);
        p.codes[i] = format.codes()[map[i]];
        p.reads |= std::uint64_t(1) << p.codes[i].offset;
    }
    return p;
}
template <unsigned N>
std::expected<scalar_write<N>, error> write(const layout& format, std::span<const byte> map) {
    auto r = read<N>(format, map);
    if (!r)
        return std::unexpected(r.error());
    scalar_write<N> p;
    p.read = *r;
    std::array<bool, 128> seen{};
    std::array<write_byte, 64> by_byte{};
    for (unsigned i = 0; i < map.size(); ++i) {
        if (map[i] == hole)
            continue;
        if (seen[map[i]])
            return std::unexpected(error::duplicate);
        seen[map[i]] = true;
        const auto c = p.read.codes[i];
        p.invalid[i] = byte(~((1u << c.width) - 1));
        auto& b = by_byte[c.offset];
        b.offset = c.offset;
        b.mask |= ((1u << c.width) - 1) << c.shift;
        b.input[b.count] = i;
        b.shift[b.count++] = c.shift;
        p.writes |= std::uint64_t(1) << c.offset;
    }
    for (auto b : by_byte)
        if (b.count)
            p.stores[p.count++] = b;
    for (unsigned i = 0; i < p.count; ++i) {
        const auto offset = p.stores[i].offset;
        if (p.runs && p.footprint[p.runs - 1].offset + p.footprint[p.runs - 1].size == offset)
            ++p.footprint[p.runs - 1].size;
        else
            p.footprint[p.runs++] = {offset, 1};
    }
    if constexpr (N == 8) {
        unsigned selected = 0;
        for (auto c : p.read.codes)
            selected += c.width != 0;
        p.word.selected = selected;
        if (p.count && p.stores[p.count - 1].offset - p.stores[0].offset < 8) {
            auto& w = p.word;
            w.offset = p.stores[0].offset;
            w.bytes = p.stores[p.count - 1].offset - w.offset + 1;
            unsigned slot = 0;
            for (unsigned i = 0; i < N; ++i) {
                const auto c = p.read.codes[i];
                if (!c.width)
                    continue;
                w.input_shift[slot] = i * 8;
                w.output_shift[slot++] = (c.offset - w.offset) * 8 + c.shift;
                w.changed |= std::uint64_t((1u << c.width) - 1)
                             << ((c.offset - w.offset) * 8 + c.shift);
            }
        }
    }
    return p;
}
void set_route(shuffle& p, unsigned out, unsigned in, int shift, byte mask, bool left) {
    p.index[out] = in;
    p.mask[out] = mask;
    p.routes |= 1u << (in / 16);
    p.shifting |= shift != 0;
#if defined(__aarch64__)
    p.shift[out] = shift;
#elif defined(__AVX512VBMI__)
    p.bit_index[out] = (8 * (out % 8) - shift) & 63;
#elif defined(__AVX2__)
    p.avx2_index[in / 16][out] = in % 16;
    const unsigned amount = left ? shift : -shift;
    (out & 1 ? p.odd_factor : p.even_factor)[out / 2] = left ? 1u << amount : 1u << (8 - amount);
#endif
    (void)left;
}
void initialize(shuffle& p) {
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
    for (auto& indices : p.avx2_index)
        indices.fill(255);
#endif
    (void)p;
}
void finish(shuffle& p) {
    p.masking = std::ranges::any_of(p.mask, [](byte b) { return b != 255; });
}
} // namespace
shuffle compile_shuffle(const shuffle_description& description, bool left) {
    shuffle result;
    initialize(result);
    for (unsigned i = 0; i < 64; ++i)
        if (description.mask[i])
            set_route(result, i, description.index[i], description.shift[i], description.mask[i],
                      left);
    finish(result);
    return result;
}
std::expected<scalar_read<64>, error> prepare_read_codes(const layout& f, std::span<const byte> m) {
    return read<64>(f, m);
}
std::expected<scalar_write<64>, error> prepare_write_codes(const layout& f,
                                                           std::span<const byte> m) {
    return write<64>(f, m);
}
std::expected<scalar_read<8>, error> prepare_read8(const layout& f, std::span<const byte> m) {
    return read<8>(f, m);
}
std::expected<scalar_write<8>, error> prepare_write8(const layout& f, std::span<const byte> m) {
    return write<8>(f, m);
}
std::expected<read64, error> prepare_read64(const layout& f, std::span<const byte> m) {
    auto scalar = read<64>(f, m);
    if (!scalar)
        return std::unexpected(scalar.error());
    read64 p;
    static_cast<scalar_read<64>&>(p) = *scalar;
    unsigned chunks = 0;
    for (auto c : p.codes)
        if (c.width)
            chunks |= 1u << (c.offset / 16);
    std::array<byte, 4> compact{};
    for (unsigned i = 0; i < 4; ++i) {
        if (!(chunks & (1u << i)))
            continue;
        compact[i] = p.count;
        const unsigned length = std::min(16u, p.bytes - i * 16);
        p.chunks[p.count++] = {byte(i * 16), byte(length)};
        for (unsigned j = 0; j < length; ++j)
            p.native_reads |= std::uint64_t(1) << (i * 16 + j);
    }
    initialize(p.operation);
    for (unsigned i = 0; i < m.size(); ++i) {
        auto c = p.codes[i];
        if (c.width)
            set_route(p.operation, i, compact[c.offset / 16] * 16 + c.offset % 16, -c.shift,
                      (1u << c.width) - 1, false);
    }
    finish(p.operation);
    return p;
}
std::expected<write64, error> prepare_write64(const layout& f, std::span<const byte> m) {
    auto scalar = write<64>(f, m);
    if (!scalar)
        return std::unexpected(scalar.error());
    write64 p;
    static_cast<scalar_write<64>&>(p) = *scalar;
    p.preserve.fill(255);
    for (auto& round : p.rounds)
        initialize(round);
    for (unsigned i = 0; i < p.count; ++i) {
        const auto& b = p.stores[i];
        p.preserve[b.offset] = byte(~b.mask);
        for (unsigned j = 0; j < b.count; ++j) {
            const auto c = p.read.codes[b.input[j]];
            set_route(p.rounds[j], b.offset, b.input[j], c.shift, ((1u << c.width) - 1) << c.shift,
                      true);
        }
        p.round_count = std::max(p.round_count, unsigned(b.count));
    }
    for (unsigned i = 0; i < p.round_count; ++i)
        finish(p.rounds[i]);
    p.dense = p.count == p.read.bytes;
    for (unsigned i = 0; i < p.count; ++i)
        p.needs_old |= p.stores[i].mask != 255;
    return p;
}
} // namespace ikea::tuplepack::detail
