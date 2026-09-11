#pragma once

#include <ikea_predecessor/seriespack/layout.h>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <type_traits>
#include <utility>

namespace ikea_predecessor::seriespack::detail {

template<std::size_t Count, class F>
[[gnu::always_inline]] constexpr inline void static_for(F&& f) {
    [&]<std::size_t... I> [[gnu::always_inline]] (std::index_sequence<I...>) {
        (f(std::integral_constant<std::size_t, I>{}), ...);
    }(std::make_index_sequence<Count>{});
}

// Exact little-endian byte access, including irregular word sizes. Overlapping
// power-of-two accesses have the same union as the requested byte interval.
template<unsigned N>
[[gnu::always_inline]] inline std::uint64_t load_le(const std::uint8_t* p) {
    static_assert(N <= 8);
    if constexpr (N == 0) return 0;
    else if constexpr (std::has_single_bit(N)) {
        using U = std::conditional_t<N == 1, std::uint8_t,
                  std::conditional_t<N == 2, std::uint16_t,
                  std::conditional_t<N == 4, std::uint32_t, std::uint64_t>>>;
        U v;
        std::memcpy(&v, p, N);
        if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
        return v;
    } else {
        constexpr unsigned Half = std::bit_floor(N);
        return load_le<Half>(p) | (load_le<Half>(p + N - Half) << (8 * (N - Half)));
    }
}

template<unsigned N>
[[gnu::always_inline]] inline void store_le(std::uint8_t* p, std::uint64_t value) {
    static_assert(N <= 8);
    if constexpr (N == 0) return;
    else if constexpr (std::has_single_bit(N)) {
        using U = std::conditional_t<N == 1, std::uint8_t,
                  std::conditional_t<N == 2, std::uint16_t,
                  std::conditional_t<N == 4, std::uint32_t, std::uint64_t>>>;
        U v = static_cast<U>(value);
        if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
        std::memcpy(p, &v, N);
    } else {
        constexpr unsigned Half = std::bit_floor(N);
        store_le<Half>(p, value);
        store_le<Half>(p + N - Half, value >> (8 * (N - Half)));
    }
}

[[gnu::always_inline]] inline std::uint64_t transpose_bytes(std::uint64_t x) {
    auto t = (x ^ (x >> 7)) & 0x00aa00aa00aa00aaULL;
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000cccc0000ccccULL;
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000f0f0f0f0ULL;
    return x ^ t ^ (t << 28);
}

// Residual maps describe bits only; the enclosing payload chooses stripe
// addresses. This also serves native field-plan generation without ISA code.
template<unsigned R>
constexpr unsigned residual_position(unsigned group, unsigned bit) {
    static_assert(R >= 1 && R <= 7);
    if constexpr (R == 3) {
        constexpr unsigned map[8][3] = {
            {0,1,2}, {3,4,5}, {6,7,14}, {8,9,10},
            {11,12,13}, {22,23,15}, {16,17,18}, {19,20,21}};
        return map[group][bit];
    } else if constexpr (R == 6) {
        constexpr unsigned map[4][6] = {
            {0,1,2,3,4,5}, {8,9,10,11,6,7},
            {12,13,14,15,22,23}, {16,17,18,19,20,21}};
        return map[group][bit];
    } else {
        const unsigned start = group * R, room = 8 - start % 8;
        if (R <= room) return start + bit;
        const unsigned low = R - room;
        return bit < low ? (start / 8 + 1) * 8 + bit : start + bit - low;
    }
}

template<unsigned R, unsigned Group, unsigned Stripe>
struct residual_field {
    struct field { unsigned value_mask = 0, wire_mask = 0; int shift = 0; };
    static constexpr field value = [] {
        field result;
        for (unsigned bit = 0; bit < R; ++bit) {
            const unsigned position = residual_position<R>(Group, bit);
            if (position / 8 == Stripe) {
                result.value_mask |= 1u << bit;
                result.wire_mask |= 1u << (position % 8);
                result.shift = static_cast<int>(position % 8) - static_cast<int>(bit);
            }
        }
        return result;
    }();
};

template<unsigned W, geometry G>
constexpr std::size_t body_offset(std::size_t i) {
    constexpr unsigned Q = W / 8;
    if constexpr (G == geometry::local8) return i * Q;
    else {
        const auto group = i / 32, lane = i % 32;
        if constexpr (W == 10) return (group + (group >= 2)) * 32 + lane;
        else if constexpr (W == 12) return group * 32 + lane;
        else if constexpr (W == 14 || W == 15) return group * 64 + lane;
        else if constexpr (W == 20) return group * 96 + lane * 2;
        else return 0; // Pure residual; callers do not access a body.
    }
}

template<unsigned W, geometry G = geometry::striped>
constexpr std::size_t stripe_offset(unsigned stripe) {
    static_assert(G == geometry::striped);
    if constexpr (W < 8) return 32 * stripe;
    else if constexpr (W == 14 || W == 15) return 32 + 64 * stripe;
    else return 64;
}

template<unsigned Count, class F>
[[gnu::always_inline]] inline auto dispatch_group(unsigned group, F&& f) {
    if constexpr (Count == 1) return f(std::integral_constant<unsigned, 0>{});
    else {
        if (group == Count - 1) return f(std::integral_constant<unsigned, Count - 1>{});
        return dispatch_group<Count - 1>(group, std::forward<F>(f));
    }
}

template<unsigned W, unsigned Group>
[[gnu::always_inline]] inline unsigned get_striped_tail(const std::uint8_t* tile,
                                                       unsigned lane) {
    constexpr unsigned R = W % 8;
    constexpr unsigned Stripes = payload_layout<W, geometry::striped>::tile_bytes == 0
        ? 0 : R / std::gcd(R, 8u);
    unsigned result = 0;
    static_for<Stripes>([&](auto c) {
        constexpr auto f = residual_field<R, Group, c>::value;
        if constexpr (f.value_mask != 0) {
            const unsigned raw = tile[stripe_offset<W>(c) + lane];
            if constexpr (f.shift >= 0) result |= (raw >> f.shift) & f.value_mask;
            else result |= (raw << -f.shift) & f.value_mask;
        }
    });
    return result;
}

template<unsigned W, unsigned Group>
[[gnu::always_inline]] inline void set_striped_tail(std::uint8_t* tile,
                                                   unsigned lane, unsigned value) {
    constexpr unsigned R = W % 8, Stripes = R / std::gcd(R, 8u);
    static_for<Stripes>([&](auto c) {
        constexpr auto f = residual_field<R, Group, c>::value;
        if constexpr (f.value_mask != 0) {
            auto& byte = tile[stripe_offset<W>(c) + lane];
            const unsigned bits = [&] {
                if constexpr (f.shift >= 0) return value << f.shift;
                else return value >> -f.shift;
            }();
            byte = static_cast<std::uint8_t>((byte & ~f.wire_mask) | (bits & f.wire_mask));
        }
    });
}

// Trusted, tile-local point operations. Their byte footprints depend on the
// wire and original index, not on a materialized tile or allocation padding.
template<unsigned W, geometry G>
[[gnu::always_inline]] inline std::uint64_t get(const std::uint8_t* tile, std::size_t i) {
    static_assert(W <= 64);
    constexpr unsigned Q = W / 8, R = W % 8;
    std::uint64_t body = 0;
    if constexpr (Q != 0) body = load_le<Q>(tile + body_offset<W, G>(i));
    if constexpr (R == 0) return body;
    else if constexpr (G == geometry::local8) {
        constexpr auto columns = 0x0101010101010101ULL >> (8 * (8 - R));
        const auto bits = (load_le<R>(tile + 8 * Q) >> i) & columns;
        const auto tail = (bits * 0x0102040810204080ULL) >> 56;
        return (body << R) | tail;
    } else if constexpr (R == 1 || R == 2 || R == 4) {
        // Every group uses the same byte stripe. Only its bit offset changes;
        // dispatching separate constant-shift leaves adds an unpredictable
        // branch to random access without changing the required byte load.
        const unsigned raw = tile[stripe_offset<W>(0) + i % 32];
        const unsigned tail = (raw >> ((i / 32) * R)) & ((1u << R) - 1);
        return (body << R) | tail;
    } else {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        const auto tail = dispatch_group<Groups>(static_cast<unsigned>(i / 32), [&](auto group) {
            return get_striped_tail<W, group>(tile, static_cast<unsigned>(i % 32));
        });
        return (body << R) | tail;
    }
}

// Same bytes and footprint as the constant-offset leaves above. Arithmetic
// groups favor small/hot accesses; the binding keeps this choice independent
// of the bulk target and never attempts to classify cache residence.
template<unsigned W, geometry G>
[[gnu::always_inline]] inline std::uint64_t get_arithmetic(const std::uint8_t* tile,
                                                         std::size_t i) {
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (G != geometry::striped || (R != 3 && R != 5 && R != 6 && R != 7))
        return get<W, G>(tile, i);
    else {
        const unsigned group = static_cast<unsigned>(i / 32);
        const unsigned lane = static_cast<unsigned>(i % 32);
        const unsigned tail = [&] {
            if constexpr (R == 3) {
                // One descriptor byte per group: first stripe, bit shift and
                // whether its top bit comes from the middle stripe.
                static constexpr std::uint64_t controls = [] {
                    std::uint64_t result = 0;
                    for (unsigned g = 0; g < 8; ++g) {
                        const unsigned low = residual_position<3>(g, 0);
                        const unsigned high = residual_position<3>(g, 2);
                        const unsigned descriptor = (low / 8) * 32 | (low % 8) |
                            (low / 8 != high / 8 ? 8u : 0u);
                        result |= std::uint64_t{descriptor} << (g * 8);
                    }
                    return result;
                }();
                const unsigned descriptor = static_cast<unsigned>(controls >> (group * 8));
                const unsigned byte = tile[stripe_offset<W>((descriptor >> 5) & 3u) + lane];
                if (descriptor & 8u)
                    return (byte >> 6) |
                        ((tile[stripe_offset<W>(1) + lane] >> (4 + group / 4)) & 4u);
                return (byte >> (descriptor & 7u)) & 7u;
            } else if constexpr (R == 6) {
                const unsigned edge = group & 2u;
                const unsigned byte = tile[stripe_offset<W>(edge) + lane];
                if (((group + 1) & 2u) == 0) return byte & 63u;
                return ((byte >> 2) & 48u) |
                    ((tile[stripe_offset<W>(1) + lane] >> (edge * 2)) & 15u);
            } else {
                const unsigned start = group * R, first = start / 8, shift = start % 8;
                const unsigned byte = tile[stripe_offset<W>(first) + lane];
                if (shift <= 8 - R) return (byte >> shift) & ((1u << R) - 1);
                const unsigned low_mask = (1u << (shift + R - 8)) - 1;
                return ((byte >> (8 - R)) & ~low_mask) |
                       (tile[stripe_offset<W>(first + 1) + lane] & low_mask);
            }
        }();
        if constexpr (Q == 0) return tail;
        else return (load_le<Q>(tile + body_offset<W, G>(i)) << R) | tail;
    }
}

// Explicit field projection for enclosing headed values: store the low W bits.
// This is not the checked logical setter, which validates the full K-bit value.
template<unsigned W, geometry G>
[[gnu::always_inline]] inline void set_low(std::uint8_t* tile, std::size_t i,
                                         std::uint64_t value) {
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (Q != 0) store_le<Q>(tile + body_offset<W, G>(i), value >> R);
    if constexpr (R == 0) return;
    else if constexpr (G == geometry::local8) {
        const unsigned mask = 1u << i;
        static_for<R>([&](auto bit) {
            auto& byte = tile[8 * Q + bit];
            byte = static_cast<std::uint8_t>((byte & ~mask) | (((value >> bit) & 1u) << i));
        });
    } else {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        dispatch_group<Groups>(static_cast<unsigned>(i / 32), [&](auto group) {
            set_striped_tail<W, group>(tile, static_cast<unsigned>(i % 32), static_cast<unsigned>(value));
        });
    }
}

template<unsigned W, geometry G>
[[gnu::always_inline]] inline void set(std::uint8_t* tile, std::size_t i,
                                     std::uint64_t value) {
    // Caller proves value fits W; set_low also serves explicit parent projection.
    set_low<W, G>(tile, i, value);
}

// The physical write union for set(). An enclosing effect adapter supplies its
// own address space and capacity; no callback is injected into individual stores.
template<unsigned W, geometry G, class F>
inline void point_write_spans(std::size_t i, F&& emit) {
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (Q != 0) emit(body_offset<W, G>(i), std::size_t{Q});
    if constexpr (R != 0 && G == geometry::local8) emit(std::size_t{8 * Q}, std::size_t{R});
    else if constexpr (R != 0) {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        dispatch_group<Groups>(static_cast<unsigned>(i / 32), [&](auto group) {
            static_for<R / std::gcd(R, 8u)>([&](auto stripe) {
                if constexpr (residual_field<R, group, stripe>::value.value_mask != 0)
                    emit(stripe_offset<W>(stripe) + i % 32, std::size_t{1});
            });
        });
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_low_tile(const UInt* in, std::uint8_t* tile) {
    // Each input contributes its low W bits. A narrower unsigned source proves
    // zero high bits; an enclosing head/payload split explicitly projects here.
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (G == geometry::local8) {
        std::uint64_t low_bytes = 0;
        static_for<8>([&](auto i) {
            if constexpr (Q != 0) store_le<Q>(tile + i * Q, std::uint64_t(in[i]) >> R);
            if constexpr (R != 0) low_bytes |= std::uint64_t(in[i] & ((1u << R) - 1)) << (8 * i);
        });
        if constexpr (R != 0) store_le<R>(tile + 8 * Q, transpose_bytes(low_bytes));
    } else {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        constexpr unsigned Stripes = R / std::gcd(R, 8u);
        for (unsigned lane = 0; lane < 32; ++lane) {
            if constexpr (Q != 0) static_for<Groups>([&](auto group) {
                constexpr unsigned First = group * 32;
                store_le<Q>(tile + body_offset<W, G>(First + lane), std::uint64_t(in[First + lane]) >> R);
            });
            static_for<Stripes>([&](auto stripe) {
                unsigned byte = 0;
                static_for<Groups>([&](auto group) {
                    constexpr auto f = residual_field<R, group, stripe>::value;
                    if constexpr (f.value_mask != 0) {
                        const unsigned low = static_cast<unsigned>(in[group * 32 + lane]);
                        if constexpr (f.shift >= 0) byte |= (low << f.shift) & f.wire_mask;
                        else byte |= (low >> -f.shift) & f.wire_mask;
                    }
                });
                tile[stripe_offset<W>(stripe) + lane] = static_cast<std::uint8_t>(byte);
            });
        }
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_tile(const UInt* in, std::uint8_t* tile) {
    // Width-valid values, any unsigned source element width.
    encode_low_tile<W, G>(in, tile);
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_tile(const std::uint8_t* tile, UInt* out) {
    static_assert(sizeof(UInt) * 8 >= W);
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (G == geometry::local8) {
        const auto tails = [&] {
            if constexpr (R != 0) return transpose_bytes(load_le<R>(tile + 8 * Q));
            else return std::uint64_t{0};
        }();
        static_for<8>([&](auto i) {
            std::uint64_t body = 0;
            if constexpr (Q != 0) body = load_le<Q>(tile + i * Q);
            out[i] = static_cast<UInt>((body << R) | ((tails >> (8 * i)) & ((1u << R) - 1)));
        });
    } else {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        static_for<Groups>([&](auto group) {
            for (unsigned lane = 0; lane < 32; ++lane) {
                const unsigned i = group * 32 + lane;
                std::uint64_t body = 0;
                if constexpr (Q != 0) body = load_le<Q>(tile + body_offset<W, G>(i));
                out[i] = static_cast<UInt>((body << R) | get_striped_tail<W, group>(tile, lane));
            }
        });
    }
}

} // namespace ikea_predecessor::seriespack::detail
