#include "native_dispatch.h"
#include <ikea/seriespack/detail/physical.h>
#include <ikea/seriespack/detail/range_regions.h>

#if defined(__aarch64__)
#include <ikea/seriespack/native_neon.h>
#endif
#if defined(__AVX2__)
#include <ikea/seriespack/native_avx2.h>
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
#include <ikea/seriespack/native_avx512.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>

namespace ikea::seriespack::detail {
namespace {

// Target adapters retain actual ISA values. They are compile-time spelling
// reuse, never a runtime carrier or virtual operation interface.
#if defined(__aarch64__)
struct neon_ops {
    static constexpr execution_target target = execution_target::neon;
    static constexpr unsigned register_bytes = 16;
    template<unsigned L, unsigned Begin>
    static auto expand(range_regions::bytes16 x) {
        return neon::body_detail::expand_bytes<1, L, Begin>(x);
    }
    template<unsigned Begin>
    static auto byte_suffix(range_regions::bytes16 x) {
        if constexpr (Begin == 0) return x;
        else return vextq_u8(x, vdupq_n_u8(0), Begin);
    }
    template<unsigned W, geometry G, unsigned L>
    using traits = seriespack::neon::fragment_traits<W, G, L>;
    template<unsigned W, geometry G, unsigned L, unsigned B>
    static auto read(const std::uint8_t* p) {
        return seriespack::neon::read_fragment<W, G, L, B>(p);
    }
    template<unsigned W, unsigned L, unsigned Group>
    static auto read_group(const std::uint8_t* p, unsigned lane) {
        return seriespack::neon::native_detail::striped_values<W, L, Group>(p, lane);
    }
    template<unsigned Q, unsigned L, unsigned N>
    static auto load(const std::uint8_t* p) { return neon::decode_body_prefix<Q, L, N>(p); }
    template<unsigned Q, unsigned L, unsigned N>
    static void store(std::uint8_t* p, uint8x16_t x) {
        neon::body_detail::store_bytes<Q * N>(p, neon::body_detail::compact_bytes<Q, L>(x));
    }
    template<unsigned L, unsigned B>
    static auto join(uint8x16_t high, uint8x16_t low) { return seriespack::neon::join<L, B>(high, low); }
    template<unsigned L, unsigned B>
    static auto right(uint8x16_t x) { return seriespack::neon::native_detail::shift<L, -int(B)>(x); }
    template<unsigned W, geometry G, class U>
    static void encode_tile(const U* in, std::uint8_t* p) {
        seriespack::neon::encode_low_tile<W, G>(in, p);
    }
    template<unsigned W, geometry G, class U>
    static void encode_dense(const U* in, std::uint8_t* p, std::size_t tiles) {
        seriespack::neon::encode_low_tiles<W, G>(in, p, tiles);
    }
    template<unsigned W, geometry G, class U>
    static void decode_dense(const std::uint8_t* p, U* out, std::size_t tiles) {
        seriespack::neon::decode_tiles<W, G>(p, out, tiles);
    }
};
#endif

#if defined(__AVX2__)
struct avx2_ops {
    static constexpr execution_target target = execution_target::avx2;
    static constexpr unsigned register_bytes = 32;
    template<unsigned L, unsigned Begin>
    static auto expand(range_regions::bytes16 x) {
        return seriespack::avx2::native_detail::expand_bytes<L>(_mm_srli_si128(x, Begin));
    }
    template<unsigned Begin>
    static auto byte_suffix(range_regions::bytes16 x) { return _mm_srli_si128(x, Begin); }
    template<unsigned W, geometry G, unsigned L>
    using traits = seriespack::avx2::fragment_traits<W, G, L>;
    template<unsigned W, geometry G, unsigned L, unsigned B>
    static auto read(const std::uint8_t* p) { return seriespack::avx2::read_fragment<W, G, L, B>(p); }
    template<unsigned W, unsigned L, unsigned Group>
    static auto read_group(const std::uint8_t* p, unsigned lane) {
        return seriespack::avx2::read_group<W, L, Group>(p, lane);
    }
    template<unsigned Q, unsigned L, unsigned N>
    static auto load(const std::uint8_t* p) { return avx2::decode_body_prefix<Q, L, N>(p); }
    template<unsigned Q, unsigned L, unsigned N>
    static void store(std::uint8_t* p, __m256i x) { avx2::encode_body_prefix<Q, L, N>(p, x); }
    template<unsigned L, unsigned B>
    static auto join(__m256i high, __m256i low) {
        return _mm256_or_si256(seriespack::avx2::native_detail::shift<L, B>(high), low);
    }
    template<unsigned L, unsigned B>
    static auto right(__m256i x) {
        static_assert(B < 8 * L);
        auto shifted = seriespack::avx2::native_detail::shift<L, -int(B)>(x);
        // The physical field helper shifts words for byte lanes. Head
        // projection needs an actual per-byte shift, including u8 sources.
        if constexpr (L == 1 && B != 0)
            return _mm256_and_si256(shifted, _mm256_set1_epi8(static_cast<char>(255U >> B)));
        else return shifted;
    }
    template<unsigned W, geometry G, class U>
    static void encode_tile(const U* in, std::uint8_t* p) { seriespack::avx2::encode_low_tile<W, G>(in, p); }
    template<unsigned W, geometry G, class U>
    static void encode_dense(const U* in, std::uint8_t* p, std::size_t tiles) {
        seriespack::avx2::encode_low_tiles<W, G>(in, p, tiles);
    }
    template<unsigned W, geometry G, class U>
    static void decode_dense(const std::uint8_t* p, U* out, std::size_t tiles) {
        seriespack::avx2::decode_tiles<W, G>(p, out, tiles);
    }
};
#endif

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
struct avx512_ops {
    static constexpr execution_target target = execution_target::avx512;
    static constexpr unsigned register_bytes = 64;
    template<unsigned L, unsigned Begin>
    static auto expand(range_regions::bytes16 x) {
        return seriespack::avx512::native_detail::expand_bytes<L>(_mm_srli_si128(x, Begin));
    }
    template<unsigned Begin>
    static auto byte_suffix(range_regions::bytes16 x) { return _mm_srli_si128(x, Begin); }
    template<unsigned W, geometry G, unsigned L>
    using traits = seriespack::avx512::fragment_traits<W, G, L>;
    template<unsigned W, geometry G, unsigned L, unsigned B>
    static auto read(const std::uint8_t* p) { return seriespack::avx512::read_fragment<W, G, L, B>(p); }
    template<unsigned W, unsigned L, unsigned Group>
    static auto read_group(const std::uint8_t* p, unsigned lane) {
        return seriespack::avx512::read_group<W, L, Group>(p, lane);
    }
    template<unsigned Q, unsigned L, unsigned N>
    static auto load(const std::uint8_t* p) { return avx512::decode_body_prefix<Q, L, N>(p); }
    template<unsigned Q, unsigned L, unsigned N>
    static void store(std::uint8_t* p, __m512i x) {
        if constexpr (Q == 1 && L == 1 && N == 32)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_castsi512_si256(x));
        else avx512::encode_body_prefix<Q, L, N>(p, x);
    }
    template<unsigned L, unsigned B>
    static auto join(__m512i high, __m512i low) {
        return _mm512_or_si512(seriespack::avx512::native_detail::shift<L, B>(high), low);
    }
    template<unsigned L, unsigned B>
    static auto right(__m512i x) {
        static_assert(B < 8 * L);
        auto shifted = seriespack::avx512::native_detail::shift<L, -int(B)>(x);
        if constexpr (L == 1 && B != 0)
            return _mm512_and_si512(shifted, _mm512_set1_epi8(static_cast<char>(255U >> B)));
        else return shifted;
    }
    template<unsigned W, geometry G, class U>
    static void encode_tile(const U* in, std::uint8_t* p) { seriespack::avx512::encode_low_tile<W, G>(in, p); }
    template<unsigned W, geometry G, class U>
    static void encode_dense(const U* in, std::uint8_t* p, std::size_t tiles) {
        seriespack::avx512::encode_low_tiles<W, G>(in, p, tiles);
    }
    template<unsigned W, geometry G, class U>
    static void decode_dense(const std::uint8_t* p, U* out, std::size_t tiles) {
        seriespack::avx512::decode_tiles<W, G>(p, out, tiles);
    }
};
#endif

template<class F> decltype(auto) with_element(element_width width, F&& f) {
    switch (width) {
        case element_width::u8: return f.template operator()<std::uint8_t>();
        case element_width::u16: return f.template operator()<std::uint16_t>();
        case element_width::u32: return f.template operator()<std::uint32_t>();
        case element_width::u64: return f.template operator()<std::uint64_t>();
    }
    __builtin_unreachable();
}

template<class F> decltype(auto) with_payload(description layout, F&& f) {
    const unsigned w = payload_width(layout);
    if (layout.storage == geometry::local8)
        return dispatch_group<65>(w, [&](auto width) -> decltype(auto) {
            return f.template operator()<width, geometry::local8>();
        });
    switch (w) {
#define IKEA_NATIVE_STRIPE(W) case W: return f.template operator()<W, geometry::striped>();
        IKEA_NATIVE_STRIPE(1) IKEA_NATIVE_STRIPE(2) IKEA_NATIVE_STRIPE(3)
        IKEA_NATIVE_STRIPE(4) IKEA_NATIVE_STRIPE(5) IKEA_NATIVE_STRIPE(6)
        IKEA_NATIVE_STRIPE(7) IKEA_NATIVE_STRIPE(10) IKEA_NATIVE_STRIPE(12)
        IKEA_NATIVE_STRIPE(14) IKEA_NATIVE_STRIPE(15) IKEA_NATIVE_STRIPE(20)
#undef IKEA_NATIVE_STRIPE
    }
    __builtin_unreachable();
}

template<unsigned W, geometry G, unsigned H>
std::uint64_t point(const basic_placement<const std::byte>& p, std::size_t index) {
    constexpr auto T = payload_layout<W, G>::tile_values;
    const auto tile = index / T, local = index % T;
    std::uint64_t value = 0;
    if constexpr (W != 0) value = get<W, G>(
        reinterpret_cast<const std::uint8_t*>(p.payload.bytes.data() + tile * p.payload.stride), local);
    if constexpr (H != 0) {
        auto high = std::to_integer<std::uint64_t>(p.heads[0].bytes[tile * p.heads[0].stride + local]);
        if constexpr (H == 16) high = (high << 8) |
            std::to_integer<std::uint64_t>(p.heads[1].bytes[tile * p.heads[1].stride + local]);
        value |= high << W;
    }
    return value;
}

#if defined(__aarch64__) || defined(__AVX2__)
template<class Ops, unsigned W, unsigned H, class U, unsigned N, class Vector>
[[gnu::always_inline]] inline void finish_fragment(
    const basic_placement<const std::byte>& p, std::size_t tile, unsigned begin, U* output, Vector value) {
    constexpr unsigned L = sizeof(U);
    if constexpr (H != 0) {
        auto high = Ops::template load<1, L, N>(reinterpret_cast<const std::uint8_t*>(
            p.heads[0].bytes.data() + tile * p.heads[0].stride + begin));
        if constexpr (H == 16) {
            const auto low = Ops::template load<1, L, N>(reinterpret_cast<const std::uint8_t*>(
                p.heads[1].bytes.data() + tile * p.heads[1].stride + begin));
            high = Ops::template join<L, 8>(high, low);
        }
        value = Ops::template join<L, W>(high, value);
    }
    Ops::template store<L, L, N>(reinterpret_cast<std::uint8_t*>(output), value);
}

template<class Ops, unsigned W, geometry G, unsigned H, class U, unsigned Begin>
[[gnu::always_inline]] inline void decode_fragment(
    const basic_placement<const std::byte>& placement, std::size_t tile, U* output) {
    constexpr unsigned L = sizeof(U), N = Ops::template traits<W, G, L>::lanes;
    const auto& p = placement.payload;
    const auto* payload = reinterpret_cast<const std::uint8_t*>(p.bytes.data());
    if constexpr (W != 0) payload += tile * p.stride;
    finish_fragment<Ops, W, H, U, N>(placement, tile, Begin, output,
        Ops::template read<W, G, L, Begin>(payload));
}

template<class Ops, unsigned W, unsigned H, class U, unsigned Group>
[[gnu::always_inline]] inline void decode_group_fragment(
    const basic_placement<const std::byte>& placement, std::size_t tile, unsigned lane, U* output) {
    constexpr unsigned L = sizeof(U), N = std::min(32u, Ops::register_bytes / L);
    const auto& p = placement.payload;
    const auto* payload = reinterpret_cast<const std::uint8_t*>(p.bytes.data() + tile * p.stride);
    finish_fragment<Ops, W, H, U, N>(placement, tile, Group * 32 + lane, output,
        Ops::template read_group<W, L, Group>(payload, lane));
}

template<class Ops, unsigned W, geometry G, unsigned H, class U, unsigned Count, unsigned TailBegin = 0,
         class ByteVector>
[[gnu::always_inline]] inline void finish_region(const std::uint8_t* payload,
    const std::uint8_t* head0, const std::uint8_t* head1, unsigned first, U* output, ByteVector tail) {
    constexpr unsigned L = sizeof(U), Q = W / 8, R = W % 8;
    if constexpr (L == 1 && Q == 0 && H == 0) {
        range_regions::store_bytes<Count>(reinterpret_cast<std::uint8_t*>(output),
            Ops::template byte_suffix<TailBegin>(tail));
    } else {
        constexpr unsigned N = std::min(8u, Ops::register_bytes / L);
        static_for<Count / N>([&](auto part) {
            constexpr unsigned offset = part * N;
            auto value = Ops::template expand<L, TailBegin + offset>(tail);
            if constexpr (Q != 0) {
                const auto body = Ops::template load<Q, L, N>(
                    payload + body_offset<W, G>(first + offset));
                value = Ops::template join<L, R>(body, value);
            }
            if constexpr (H != 0) {
                auto high = Ops::template load<1, L, N>(head0 + first + offset);
                if constexpr (H == 16)
                    high = Ops::template join<L, 8>(high, Ops::template load<1, L, N>(head1 + first + offset));
                value = Ops::template join<L, W>(high, value);
            }
            Ops::template store<L, L, N>(reinterpret_cast<std::uint8_t*>(output + offset), value);
        });
    }
}

// A tile boundary borrows the placement; it does not copy spans and unused
// head metadata through the ABI. Bases/strides become private scalar locals
// before stores. The cursor never escapes into a reference-capturing scalar
// closure, so native stores cannot force it back through the stack.
template<class Ops, unsigned W, geometry G, unsigned H, class U>
[[gnu::noinline]] void decode_boundary(const basic_placement<const std::byte>& placement,
                                      index_range rows, U* __restrict output) {
    constexpr unsigned T = payload_layout<W, G>::tile_values;
    const auto tile = rows.begin / T;
    const auto* payload = reinterpret_cast<const std::uint8_t*>(placement.payload.bytes.data());
    if constexpr (W != 0) payload += tile * placement.payload.stride;
    const std::uint8_t* head0 = nullptr;
    const std::uint8_t* head1 = nullptr;
    if constexpr (H != 0) head0 = reinterpret_cast<const std::uint8_t*>(placement.heads[0].bytes.data()) + tile * placement.heads[0].stride;
    if constexpr (H == 16) head1 = reinterpret_cast<const std::uint8_t*>(placement.heads[1].bytes.data()) + tile * placement.heads[1].stride;
    auto i = unsigned(rows.begin % T);
    const unsigned end = i + unsigned(rows.size());
    if constexpr (G == geometry::striped) {
        while (end - i >= 16) {
            if (i % 32 <= 16) {
                const auto tail = range_regions::striped<W>(payload, i);
                finish_region<Ops, W, G, H, U, 16>(payload, head0, head1, i, output, tail);
                i += 16;
                output += 16;
            } else {
                const auto value = get_arithmetic<W, G>(payload, i);
                std::uint64_t high = 0;
                if constexpr (H != 0) high = head0[i];
                if constexpr (H == 16) high = (high << 8) | head1[i];
                *output++ = static_cast<U>(value | (high << W));
                ++i;
            }
        }
        if (end - i >= 8 && i % 32 <= 24) {
            const auto tail = range_regions::striped<W, 8>(payload, i);
            finish_region<Ops, W, G, H, U, 8>(payload, head0, head1, i, output, tail);
            i += 8;
            output += 8;
        }
    } else {
        constexpr unsigned N = Ops::template traits<W, G, sizeof(U)>::lanes;
        static_for<T / N>([&](auto part) {
            constexpr unsigned begin = part * N;
            if (i == begin && end - i >= N) {
                auto value = Ops::template read<W, G, sizeof(U), begin>(payload);
                if constexpr (H != 0) {
                    auto high = Ops::template load<1, sizeof(U), N>(head0 + begin);
                    if constexpr (H == 16) high = Ops::template join<sizeof(U), 8>(high,
                        Ops::template load<1, sizeof(U), N>(head1 + begin));
                    value = Ops::template join<sizeof(U), W>(high, value);
                }
                Ops::template store<sizeof(U), sizeof(U), N>(reinterpret_cast<std::uint8_t*>(output), value);
                i += N;
                output += N;
            }
        });
    }
    // Fewer than16 values remain. Keep this truly scalar: auto-vectorizing
    // arbitrary-index point maps here bloats the cold edge path and duplicates
    // the admitted native-region work above.
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (; i != end; ++i) {
        auto value = get_arithmetic<W, G>(payload, i);
        if constexpr (H != 0) {
            std::uint64_t high = head0[i];
            if constexpr (H == 16) high = (high << 8) | head1[i];
            value |= high << W;
        }
        *output++ = static_cast<U>(value);
    }
}

// A complete run has no boundary cursor to preserve. The coarse native leaf
// receives a borrowed placement, and snapshots just the planes it uses before
// storing output. This keeps short complete runs on the same body kernels as
// long dense reads without carrying the general edge traversal through them.
template<class Ops, unsigned W, geometry G, unsigned H, class U>
[[gnu::noinline]] void decode_complete(const basic_placement<const std::byte>& placement,
                                      std::size_t tile, std::size_t tiles, U* __restrict output) {
    constexpr unsigned T = payload_layout<W, G>::tile_values;
    constexpr unsigned N = G == geometry::striped ? std::min<unsigned>(32, Ops::register_bytes / sizeof(U))
        : Ops::template traits<W, G, sizeof(U)>::lanes;
    const auto stride = placement.payload.stride;
    const auto* payload = reinterpret_cast<const std::uint8_t*>(placement.payload.bytes.data());
    if constexpr (W != 0) payload += tile * stride;
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7) {
        if (tiles >= 2 && tiles < 4 && stride == W) {
            const auto tail = range_regions::local_pair<W>(payload);
            if constexpr (H == 0) {
                finish_region<Ops, W, G, H, U, 16>(payload, nullptr, nullptr, 0, output, tail);
            } else {
                const auto* head0 = reinterpret_cast<const std::uint8_t*>(placement.heads[0].bytes.data()) + tile * placement.heads[0].stride;
                const std::uint8_t* head1 = nullptr;
                if constexpr (H == 16) head1 = reinterpret_cast<const std::uint8_t*>(placement.heads[1].bytes.data()) + tile * placement.heads[1].stride;
                finish_region<Ops, W, G, H, U, 8>(payload, head0, head1, 0, output, tail);
                head0 += placement.heads[0].stride;
                if constexpr (H == 16) head1 += placement.heads[1].stride;
                finish_region<Ops, W, G, H, U, 8, 8>(payload + W, head0, head1, 0, output + 8, tail);
            }
            tile += 2;
            tiles -= 2;
            payload += 2 * W;
            output += 16;
            if (tiles == 0) return;
        }
    }
    if constexpr (H == 0) {
        if (stride == payload_layout<W, G>::tile_bytes) {
            Ops::template decode_dense<W, G>(payload, output, tiles);
            return;
        }
    }
    for (std::size_t t = 0; t != tiles; ++t) {
        if constexpr (G == geometry::local8) {
            static_for<T / N>([&](auto part) {
                decode_fragment<Ops, W, G, H, U, part * N>(placement, tile + t, output + t * T + part * N);
            });
        } else {
            static_for<T / 32>([&](auto group) {
                if constexpr (H == 0) {
#pragma clang loop unroll(disable)
                    for (unsigned lane = 0; lane < 32; lane += N)
                        decode_group_fragment<Ops, W, H, U, group>(placement, tile + t, lane,
                            output + t * T + group * 32 + lane);
                } else {
                    // These are region cursors, not original-index coordinates.
                    // Keep group offsets out of the repeated head/store address
                    // expressions when the body reads fewer lanes than a group.
                    constexpr unsigned L = sizeof(U);
                    const auto* encoded = payload + t * stride;
                    const auto* head0 = reinterpret_cast<const std::uint8_t*>(placement.heads[0].bytes.data()) +
                        (tile + t) * placement.heads[0].stride + group * 32;
                    const std::uint8_t* head1 = nullptr;
                    if constexpr (H == 16) head1 = reinterpret_cast<const std::uint8_t*>(placement.heads[1].bytes.data()) +
                        (tile + t) * placement.heads[1].stride + group * 32;
                    // Keep these already-admitted group bases opaque to
                    // induction reassociation. Otherwise LLVM sinks the group
                    // offset back into every head load. No instruction emits.
                    if constexpr (H == 16) asm("" : "+r"(head0), "+r"(head1));
                    else asm("" : "+r"(head0));
                    auto* out = output + t * T + group * 32;
#pragma clang loop unroll(disable)
                    for (unsigned lane = 0; lane < 32; lane += N) {
                        auto value = Ops::template read_group<W, L, group>(encoded, lane);
                        auto high = Ops::template load<1, L, N>(head0);
                        if constexpr (H == 16) high = Ops::template join<L, 8>(high, Ops::template load<1, L, N>(head1));
                        value = Ops::template join<L, W>(high, value);
                        Ops::template store<L, L, N>(reinterpret_cast<std::uint8_t*>(out), value);
                        head0 += N;
                        if constexpr (H == 16) head1 += N;
                        out += N;
                    }
                }
            });
        }
    }
}

template<class Ops, unsigned W, geometry G, unsigned H, class U>
[[gnu::noinline]] void decode_edges(const basic_placement<const std::byte>& placement,
                                   index_range rows, U* __restrict output) {
    constexpr unsigned T = payload_layout<W, G>::tile_values;
    auto i = rows.begin;
    if (i % T != 0) {
        const auto end = std::min(rows.end, i + T - i % T);
        decode_boundary<Ops, W, G, H>(placement, {i, end}, output);
        i = end;
    }
    const auto tiles = (rows.end - i) / T;
    if (tiles != 0) {
        decode_complete<Ops, W, G, H>(placement, i / T, tiles, output + i - rows.begin);
        i += tiles * T;
    }
    if (i != rows.end)
        decode_boundary<Ops, W, G, H>(placement, {i, rows.end}, output + i - rows.begin);
}

template<class Ops, unsigned W, geometry G, unsigned H, class U>
[[gnu::always_inline]] inline void decode_placed(const basic_placement<const std::byte>& placement,
                                               index_range rows, U* __restrict output) {
    constexpr unsigned T = payload_layout<W, G>::tile_values;
    if (rows.empty()) return;
    if ((rows.begin % T | rows.end % T) == 0)
        return decode_complete<Ops, W, G, H>(placement, rows.begin / T, rows.size() / T, output);
    if (rows.begin / T == (rows.end - 1) / T)
        return decode_boundary<Ops, W, G, H>(placement, rows, output);
    decode_edges<Ops, W, G, H>(placement, rows, output);
}

template<class Ops, unsigned W, geometry G, unsigned H>
void decode_bound(const const_view& source, index_range rows, void* output, element_width width) {
    with_element(width, [&]<class U> {
        if constexpr (sizeof(U) * 8 >= W + H)
            decode_placed<Ops, W, G, H>(source.placement(), rows, static_cast<U*>(output));
        else __builtin_unreachable(); // Admitted output represents every K-bit value.
    });
}

template<class Ops, unsigned W, geometry G, unsigned H>
bound_reader reader(const_view source, bound_reader::point_function point, point_reader strategy) {
    return bound_reader::assume_valid(source, Ops::target,
        point, decode_bound<Ops, W, G, H>, strategy);
}

template<class Ops, unsigned W, geometry G>
bound_reader choose_reader(const_view source, bound_reader::point_function point, point_reader strategy) {
    const auto h = source.layout().head_bits;
    if constexpr (W >= 1) if (h == 0) return reader<Ops, W, G, 0>(source, point, strategy);
    if constexpr (W <= 56) if (h == 8) return reader<Ops, W, G, 8>(source, point, strategy);
    if constexpr (W <= 48) if (h == 16) return reader<Ops, W, G, 16>(source, point, strategy);
    __builtin_unreachable();
}

#endif // compiled native readers

// Heavy payload encoding is independent of head count. Head projection uses a
// separate native stream pass; it never materializes widened input or reloads
// an already decoded output. The full source type remains a compile-time fact.
template<class Ops, unsigned W, geometry G, class U>
void encode_payload(const mutable_view& destination, const U* input) {
    if constexpr (W == 0) return;
    else {
        constexpr auto T = payload_layout<W, G>::tile_values;
        const auto p = destination.placement().payload;
        auto* base = reinterpret_cast<std::uint8_t*>(p.bytes.data());
        const auto n = destination.size(), full = n / T;
        if (p.stride == payload_layout<W, G>::tile_bytes) {
#if defined(__aarch64__)
            if constexpr (Ops::target == execution_target::neon && W == 1 && G == geometry::local8) {
                // Full-K value validation has already completed. Select the
                // headless one-bit preset once for this admitted dense region.
                if (destination.layout().head_bits == 0)
                    seriespack::neon::encode_local1_tiles(input, base, full);
                else Ops::template encode_dense<W, G>(input, base, full);
            } else
#endif
                Ops::template encode_dense<W, G>(input, base, full);
        } else for (std::size_t tile = 0; tile < full; ++tile)
            Ops::template encode_tile<W, G>(input + tile * T, base + tile * p.stride);
        if (const auto left = n % T; left != 0) {
            std::array<U, T> boundary{};
            std::memcpy(boundary.data(), input + full * T, left * sizeof(U));
            Ops::template encode_tile<W, G>(boundary.data(), base + full * p.stride);
        }
    }
}

#if defined(__aarch64__)
// A head byte need not travel through a full-width result lane. Gather its
// one- or two-byte source window across up to four exact native loads, then
// compact eight or sixteen head bytes for one store.
template<unsigned Shift, unsigned L, unsigned N>
[[gnu::always_inline]] inline uint8x16_t neon_head_bytes(const std::uint8_t* input) {
    static_assert((L == 1 || L == 4 || L == 8) && (N == 8 || N == 16));
    if constexpr (Shift >= 8 * L) return vdupq_n_u8(0);
    else if constexpr (L == 1) {
        auto bytes = neon::body_detail::load_bytes<N>(input);
        if constexpr (Shift != 0) bytes = vshrq_n_u8(bytes, Shift);
        return bytes;
    } else if constexpr (N == 16 && Shift % 8 != 0) {
        static_assert(L == 4);
        const auto first = neon_head_bytes<Shift, L, 8>(input);
        const auto second = neon_head_bytes<Shift, L, 8>(input + 8 * L);
        return vcombine_u8(vget_low_u8(first), vget_low_u8(second));
    } else {
        constexpr unsigned Registers = N * L / 16;
        static_assert(Registers <= 4);
        std::array<uint8x16_t, Registers> table;
        static_for<Registers>([&](auto i) { table[i] = vld1q_u8(input + i * 16); });
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> bytes{};
            for (unsigned i = 0; i < 16; ++i) {
                if constexpr (Shift % 8 == 0)
                    bytes[i] = i < N ? i * L + Shift / 8 : 255;
                else bytes[i] = Shift / 8 + i % 2 < L ? i / 2 * L + Shift / 8 + i % 2 : 255;
            }
            return bytes;
        }();
        auto bytes = neon::body_detail::table<0, Registers>(table, vld1q_u8(indices.data()));
        if constexpr (Shift % 8 != 0)
            bytes = vcombine_u8(vmovn_u16(vshrq_n_u16(vreinterpretq_u16_u8(bytes), Shift % 8)),
                                vdup_n_u8(0));
        return bytes;
    }
}

template<unsigned Shift, unsigned T, class U>
[[gnu::noinline]] void encode_neon_head(basic_plane<std::byte> plane, std::size_t n, const U* input) {
    constexpr unsigned L = sizeof(U), N = std::min(16U, 64 / L);
    auto* base = reinterpret_cast<std::uint8_t*>(plane.bytes.data());
    const auto run = [](const U* source, std::uint8_t* out, std::size_t count) {
        if constexpr (Shift >= 8 * L) std::memset(out, 0, count);
        else {
            std::size_t i = 0;
            for (; count - i >= N; i += N)
                neon::body_detail::store_bytes<N>(out + i, neon_head_bytes<Shift, L, N>(
                    reinterpret_cast<const std::uint8_t*>(source + i)));
            for (; i < count; ++i) {
                U value;
                std::memcpy(&value, source + i, sizeof(U));
                out[i] = static_cast<std::uint8_t>(value >> Shift);
            }
        }
    };
    // Adjacent head tiles are one writable region; a strided placement keeps
    // each tile independent. Neither path reads final-tile source slack.
    if (plane.stride == T) {
        run(input, base, n);
        if (const auto left = n % T; left != 0) std::memset(base + n, 0, T - left);
    } else {
        const auto full = n / T;
        for (std::size_t tile = 0; tile < full; ++tile)
            run(input + tile * T, base + tile * plane.stride, T);
        if (const auto left = n % T; left != 0) {
            auto* out = base + full * plane.stride;
            run(input + full * T, out, left);
            std::memset(out + left, 0, T - left);
        }
    }
}
#endif

template<class Ops, unsigned Shift, unsigned T, class U>
void encode_head(basic_plane<std::byte> plane, std::size_t n, const U* input) {
#if defined(__aarch64__)
    if constexpr (Ops::target == execution_target::neon && sizeof(U) != 2) {
        encode_neon_head<Shift, T>(plane, n, input);
        return;
    }
#endif
    constexpr unsigned L = sizeof(U), N = std::min(T, Ops::register_bytes / L);
    const auto full = n / T;
    for (std::size_t tile = 0; tile < full; ++tile) {
        auto* out = reinterpret_cast<std::uint8_t*>(plane.bytes.data() + tile * plane.stride);
        if constexpr (Shift >= 8 * L) std::memset(out, 0, T);
        else for (unsigned i = 0; i < T; i += N) {
            const auto values = Ops::template load<L, L, N>(
                reinterpret_cast<const std::uint8_t*>(input + tile * T + i));
            Ops::template store<1, L, N>(out + i, Ops::template right<L, Shift>(values));
        }
    }
    if (const auto left = n % T; left != 0) {
        auto* out = reinterpret_cast<std::uint8_t*>(plane.bytes.data() + full * plane.stride);
        // At most one boundary tile; exact source extent, canonical unused
        // lanes, and no pointer into a gap or an absent following head tile.
        for (std::size_t i = 0; i < left; ++i) {
            if constexpr (Shift >= 8 * L) out[i] = 0;
            else {
                U value;
                std::memcpy(&value, input + full * T + i, sizeof(U));
                out[i] = static_cast<std::uint8_t>(value >> Shift);
            }
        }
        std::memset(out + left, 0, T - left);
    }
}

template<class Ops, unsigned W, geometry G, class U>
void encode_typed(const mutable_view& destination, const U* input) {
    const auto h = destination.layout().head_bits;
    const auto n = destination.size();
    const auto heads = destination.placement().heads;
    encode_payload<Ops, W, G>(destination, input);
    constexpr unsigned T = payload_layout<W, G>::tile_values;
    if (h == 8) encode_head<Ops, W, T>(heads[0], n, input);
    else if (h == 16) {
        encode_head<Ops, W + 8, T>(heads[0], n, input);
        encode_head<Ops, W, T>(heads[1], n, input);
    }
}

template<class Ops, unsigned W, geometry G>
void encode_bound(const mutable_view& destination, const void* input,
                  element_width width, effect_output* effects) {
    if (destination.size() == 0) return;
    with_element(width, [&]<class U> {
        encode_typed<Ops, W, G>(destination, static_cast<const U*>(input));
    });
    encode_effects(destination, effects);
}

template<class Ops>
bound_encoder bind_encoder_target(mutable_view destination) {
    return with_payload(destination.layout(), [&]<unsigned W, geometry G> {
        return bound_encoder::assume_valid(destination, Ops::target, encode_bound<Ops, W, G>);
    });
}

template<class Ops>
bound_reader bind_target(const_view source, point_reader strategy) {
    const auto point = select_point_function(source.layout(), strategy);
    return with_payload(source.layout(), [&]<unsigned W, geometry G> {
        return choose_reader<Ops, W, G>(source, point, strategy);
    });
}

template<class Ops>
void encode_target(mutable_view& destination, input_values input) {
    with_payload(destination.layout(), [&]<unsigned W, geometry G> {
        with_element(input.width, [&]<class U> {
            encode_typed<Ops, W, G>(destination, static_cast<const U*>(input.data));
        });
    });
}

} // namespace

bool native_target_available(execution_target target) noexcept {
    switch (target) {
#if defined(__aarch64__)
        case execution_target::neon: return true;
#endif
#if defined(__AVX2__)
        case execution_target::avx2: return true;
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        case execution_target::avx512: return true;
#endif
        default: return false;
    }
}

std::expected<bound_reader, error> bind_native_reader(
    [[maybe_unused]] const_view source, execution_target target, [[maybe_unused]] point_reader strategy) {
    switch (target) {
#if defined(__aarch64__)
        case execution_target::neon: return bind_target<neon_ops>(source, strategy);
#endif
#if defined(__AVX2__)
        case execution_target::avx2: return bind_target<avx2_ops>(source, strategy);
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        case execution_target::avx512: return bind_target<avx512_ops>(source, strategy);
#endif
        default: return std::unexpected(error::unsupported);
    }
}

std::expected<bound_encoder, error> bind_native_encoder(
    [[maybe_unused]] mutable_view destination, execution_target target) {
    switch (target) {
#if defined(__aarch64__)
        case execution_target::neon: return bind_encoder_target<neon_ops>(destination);
#endif
#if defined(__AVX2__)
        case execution_target::avx2: return bind_encoder_target<avx2_ops>(destination);
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        case execution_target::avx512: return bind_encoder_target<avx512_ops>(destination);
#endif
        default: return std::unexpected(error::unsupported);
    }
}

void native_encode(mutable_view& destination, [[maybe_unused]] input_values input, execution_target target) {
    if (destination.size() == 0) return;
    switch (target) {
#if defined(__aarch64__)
        case execution_target::neon: return encode_target<neon_ops>(destination, input);
#endif
#if defined(__AVX2__)
        case execution_target::avx2: return encode_target<avx2_ops>(destination, input);
#endif
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        case execution_target::avx512: return encode_target<avx512_ops>(destination, input);
#endif
        default: __builtin_unreachable(); // Availability was admitted before mutation.
    }
}

} // namespace ikea::seriespack::detail
