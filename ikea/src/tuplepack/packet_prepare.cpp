#include <ikea/tuplepack/detail/packet_plan.h>
#include <algorithm>
#include <bit>
namespace ikea::tuplepack::detail {
namespace {
packet_placement place(std::uint64_t used, unsigned bytes) {
    packet_placement p;
    if (!used)
        return p;
    p.first = std::countr_zero(used);
    const unsigned span = 64 - std::countl_zero(used) - p.first;
    p.contiguous = span <= bytes;
    if (p.contiguous) {
        p.count = span;
        for (unsigned i = 0; i < span; ++i)
            p.offsets[i] = p.first + i;
    } else
        for (unsigned i = 0; i < 64; ++i)
            if (used & (std::uint64_t(1) << i))
                p.offsets[p.count++] = i;
    p.grain = std::bit_ceil(std::max(1u, p.count));
    return p;
}
unsigned slot(const packet_placement& p, unsigned offset) {
    for (unsigned i = 0; i < p.count; ++i)
        if (p.offsets[i] == offset)
            return i;
    __builtin_unreachable();
}
} // namespace
std::expected<packet_read, error> prepare_packet_read(const layout& f, std::span<const byte> map,
                                                      unsigned rows) {
    if (!rows || rows > 64 || !std::has_single_bit(rows) || map.size() > 64 / rows)
        return std::unexpected(error::map);
    auto codes = prepare_read_codes(f, map);
    if (!codes)
        return std::unexpected(codes.error());
    const auto& scalar = *codes;
    packet_read p;
    static_cast<scalar_read<64>&>(p) = scalar;
    p.place = place(scalar.reads, 64 / rows);
    for (unsigned i = 0; i < p.place.count; ++i)
        p.native_reads |= std::uint64_t(1) << p.place.offsets[i];
    if (rows <= 4 && !p.place.contiguous && p.place.count > 8) {
        p.point = *prepare_read64(f, map);
        p.native_reads = p.point.native_reads;
    }
    shuffle_description route;
    for (unsigned r = 0; r < rows; ++r)
        for (unsigned i = 0; i < map.size(); ++i) {
            const auto c = scalar.codes[i];
            if (!c.width)
                continue;
            const auto out = r * (64 / rows) + i;
            route.index[out] = r * p.place.grain + slot(p.place, c.offset);
            route.shift[out] = -c.shift;
            route.mask[out] = (1u << c.width) - 1;
        }
    p.route = compile_shuffle(route, false);
    // A 3/6/12-byte physical hull otherwise needs a short transfer per row.
    // Full tight windows can load whole units once and route across them. Do
    // not extend this to stride padding: read_bytes() describes unit positions.
    if (rows >= 4 && f.bytes() <= 64 / rows && p.place.count &&
        !std::has_single_bit(p.place.count)) {
        shuffle_description tight;
        for (unsigned r = 0; r < rows; ++r)
            for (unsigned i = 0; i < map.size(); ++i) {
                const auto c = scalar.codes[i];
                if (!c.width)
                    continue;
                const auto out = r * (64 / rows) + i;
                tight.index[out] = r * f.bytes() + c.offset;
                tight.shift[out] = -c.shift;
                tight.mask[out] = (1u << c.width) - 1;
            }
        p.tight_route = compile_shuffle(tight, false);
        p.tight_bytes = rows * f.bytes();
        p.native_reads = (std::uint64_t(1) << f.bytes()) - 1;
    }
    return p;
}
std::expected<packet_write, error> prepare_packet_write(const layout& f, std::span<const byte> map,
                                                        unsigned rows) {
    if (!rows || rows > 64 || !std::has_single_bit(rows) || map.size() > 64 / rows)
        return std::unexpected(error::map);
    auto codes = prepare_write_codes(f, map);
    if (!codes)
        return std::unexpected(codes.error());
    const auto& scalar = *codes;
    packet_write p;
    static_cast<scalar_write<64>&>(p) = scalar;
    p.place = place(scalar.writes, 64 / rows);
    if (rows <= 4 && !p.place.contiguous && p.place.count > 8) {
        const auto point = *prepare_write64(f, map);
        p.rounds = point.rounds;
        p.round_count = point.round_count;
        p.preserve = point.preserve;
        p.needs_old = point.needs_old;
        if (p.needs_old)
            p.native_reads =
                f.bytes() == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << f.bytes()) - 1;
        // Point lowering is used per row, but packet-wide admission uses the
        // repeated input map. Ignored rows are masked by the operation shell.
        for (unsigned r = 1; r < rows; ++r)
            for (unsigned i = 0; i < map.size(); ++i)
                p.invalid[r * (64 / rows) + i] = scalar.invalid[i];
        return p;
    }
    // The issued-store footprint follows the selected physical transfer, which
    // can reissue preserved bytes between selected codes in a compact window.
    if (p.place.contiguous && p.place.count) {
        p.runs = 1;
        p.footprint[0] = {byte(p.place.first), byte(p.place.count)};
        p.writes = 0;
        for (unsigned i = 0; i < p.place.count; ++i)
            p.writes |= std::uint64_t(1) << p.place.offsets[i];
    }
    p.preserve.fill(255);
    std::array<shuffle_description, 8> routes;
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned i = 0; i < map.size(); ++i)
            p.invalid[r * (64 / rows) + i] = scalar.invalid[i];
        for (unsigned i = 0; i < scalar.count; ++i) {
            const auto& b = scalar.stores[i];
            const auto out = r * p.place.grain + slot(p.place, b.offset);
            p.preserve[out] = byte(~b.mask);
            for (unsigned j = 0; j < b.count; ++j) {
                const auto c = scalar.read.codes[b.input[j]];
                routes[j].index[out] = r * (64 / rows) + b.input[j];
                routes[j].shift[out] = c.shift;
                routes[j].mask[out] = ((1u << c.width) - 1) << c.shift;
            }
            p.round_count = std::max(p.round_count, unsigned(b.count));
        }
    }
    for (unsigned i = 0; i < p.place.count; ++i)
        p.needs_old |= p.preserve[i] != 0;
    if (p.needs_old)
        p.native_reads = p.writes;
    for (unsigned i = 0; i < p.round_count; ++i)
        p.rounds[i] = compile_shuffle(routes[i], true);
    return p;
}
} // namespace ikea::tuplepack::detail
