#pragma once
#include <arm_neon.h>
#include <array>
#include <cstdint>

// A small experimental family, not a TuplePack API. A tuple contains sixteen
// packed bytes; each byte contains independent 1-, 4-, and 3-bit codes. Logical
// code ranks are 3*byte + kind, regardless of their physical order in the byte.
namespace tuple_probe {
using byte = std::uint8_t;
struct packet { uint8x16_t a, b, c, d; };
struct schema { std::array<unsigned, 3> shift; };
inline constexpr std::array<unsigned, 3> widths{1, 4, 3};
inline constexpr std::array<schema, 6> layouts{{
    {{{0, 1, 5}}}, {{{0, 4, 1}}}, {{{4, 0, 5}}},
    {{{7, 0, 4}}}, {{{3, 4, 0}}}, {{{7, 3, 0}}}
}};
inline constexpr std::array<const char*, 6> names{
    "143", "134", "413", "431", "314", "341"};

// The per-operation map places kind 0 in bytes 0..15, kind 1 in 16..31,
// kind 2 in 32..47. Unselected positions and 48..63 are zero in this probe.
// Code ranks remain stable when a tuple uses a different physical layout.
inline std::array<byte, 64> map(unsigned selected) {
    std::array<byte, 64> result;
    result.fill(255);
    for (unsigned kind = 0; kind < 3; ++kind)
        if (selected & (1u << kind))
            for (unsigned lane = 0; lane < 16; ++lane)
                result[kind * 16 + lane] = 3 * lane + kind;
    return result;
}

// Bit-at-a-time reference, deliberately independent of the native extraction.
inline std::array<byte, 64> reference_read(const byte* row, schema s,
                                          const std::array<byte, 64>& m) {
    std::array<byte, 64> out{};
    for (unsigned i = 0; i < m.size(); ++i) {
        if (m[i] == 255)
            continue;
        const unsigned kind = m[i] % 3, lane = m[i] / 3;
        for (unsigned bit = 0; bit < widths[kind]; ++bit)
            out[i] |= ((row[lane] >> (s.shift[kind] + bit)) & 1u) << bit;
    }
    return out;
}

inline void reference_write(byte* row, schema s, const std::array<byte, 64>& m,
                            const std::array<byte, 64>& input) {
    for (unsigned i = 0; i < m.size(); ++i) {
        if (m[i] == 255)
            continue;
        const unsigned kind = m[i] % 3, lane = m[i] / 3;
        for (unsigned bit = 0; bit < widths[kind]; ++bit) {
            const byte mask = 1u << (s.shift[kind] + bit);
            row[lane] = (row[lane] & ~mask) |
                        (((input[i] >> bit) & 1u) << (s.shift[kind] + bit));
        }
    }
}

inline packet load_packet(const byte* p) {
    return {vld1q_u8(p), vld1q_u8(p + 16), vld1q_u8(p + 32), vld1q_u8(p + 48)};
}
inline std::array<byte, 64> bytes(packet p) {
    std::array<byte, 64> out;
    vst1q_u8(out.data(), p.a);
    vst1q_u8(out.data() + 16, p.b);
    vst1q_u8(out.data() + 32, p.c);
    vst1q_u8(out.data() + 48, p.d);
    return out;
}

template <unsigned Layout, unsigned Kind>
[[gnu::always_inline]] inline uint8x16_t extract(uint8x16_t source) {
    constexpr unsigned shift = layouts[Layout].shift[Kind], width = widths[Kind];
    if constexpr (shift != 0)
        source = vshrq_n_u8(source, shift);
    if constexpr (shift + width != 8)
        source = vandq_u8(source, vdupq_n_u8((1u << width) - 1));
    return source;
}

template <unsigned Layout, unsigned Selected>
[[gnu::always_inline]] inline packet read_body(const byte* row) {
    auto source = vld1q_u8(row);
    auto zero = vdupq_n_u8(0);
    packet out{zero, zero, zero, zero};
    if constexpr (Selected & 1) out.a = extract<Layout, 0>(source);
    if constexpr (Selected & 2) out.b = extract<Layout, 1>(source);
    if constexpr (Selected & 4) out.c = extract<Layout, 2>(source);
    return out;
}

template <unsigned Layout, unsigned Kind>
[[gnu::always_inline]] inline uint8x16_t insert(uint8x16_t value) {
    // Value fit is admitted by the caller; no masking is needed here. This is
    // intentionally a trusted body, not a checked ordinary mutation surface.
    constexpr unsigned shift = layouts[Layout].shift[Kind];
    if constexpr (shift != 0)
        return vshlq_n_u8(value, shift);
    return value;
}

template <unsigned Layout, unsigned Selected>
[[gnu::always_inline]] inline void write_body(byte* row, packet value) {
    constexpr unsigned selected_bits =
        ((Selected & 1) ? 1u << layouts[Layout].shift[0] : 0) |
        ((Selected & 2) ? 15u << layouts[Layout].shift[1] : 0) |
        ((Selected & 4) ? 7u << layouts[Layout].shift[2] : 0);
    auto updated = vdupq_n_u8(0);
    if constexpr (Selected != 7)
        updated = vandq_u8(vld1q_u8(row), vdupq_n_u8(byte(~selected_bits)));
    if constexpr (Selected & 1) updated = vorrq_u8(updated, insert<Layout, 0>(value.a));
    if constexpr (Selected & 2) updated = vorrq_u8(updated, insert<Layout, 1>(value.b));
    if constexpr (Selected & 4) updated = vorrq_u8(updated, insert<Layout, 2>(value.c));
    vst1q_u8(row, updated);
}

using reader = packet (*)(const byte*);
using writer = void (*)(byte*, packet);
struct bound {
    reader read;
    writer write;
};
// A separate TU performs selection: a timed ordinary caller cannot accidentally
// devirtualize this endpoint merely because all candidates are in its source.
bound bind(unsigned layout, unsigned selected);

inline std::uint64_t sum(packet p) {
    return vaddlvq_u8(p.a) + vaddlvq_u8(p.b) + vaddlvq_u8(p.c);
}
} // namespace tuple_probe
