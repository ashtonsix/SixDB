#pragma once
#include <ikea/tuplepack/detail/plan.h>

namespace ikea::tuplepack::detail {
/// Physical byte slots gathered into one row's portion of a native packet.
/// A compact window may include preserved neighbors; a scattered map retains
/// only selected byte positions. Neither recipe reads allocation padding.
struct packet_placement {
    std::array<byte, 64> offsets{};
    unsigned count = 0, first = 0, grain = 1;
    bool contiguous = true;
};
struct packet_read : scalar_read<64> {
    packet_placement place;
    shuffle route;
    // Tight small tuples can avoid repeated short transfers. This alternate
    // route reads complete units only, and is usable for a full active window.
    shuffle tight_route;
    unsigned tight_bytes = 0;
    read64 point;
    std::uint64_t native_reads = 0;
};
struct packet_write : scalar_write<64> {
    packet_placement place;
    std::array<shuffle, 8> rounds;
    std::array<byte, 64> preserve{};
    unsigned round_count = 0;
    bool needs_old = false;
    std::uint64_t native_reads = 0;
};
std::expected<scalar_read<64>, error> prepare_read_codes(const layout&, std::span<const byte>);
std::expected<scalar_write<64>, error> prepare_write_codes(const layout&, std::span<const byte>);
std::expected<packet_read, error> prepare_packet_read(const layout&, std::span<const byte>,
                                                      unsigned rows);
std::expected<packet_write, error> prepare_packet_write(const layout&, std::span<const byte>,
                                                        unsigned rows);
std::array<byte, 64> read_packet_buffered(const packet_read&, unsigned rows, const byte*,
                                          std::size_t stride, std::uint64_t active);
void write_packet_buffered(const packet_write&, unsigned rows, byte*, std::size_t stride,
                           const std::array<byte, 64>&, std::uint64_t active);
} // namespace ikea::tuplepack::detail
