#pragma once
#include <ikea/seriespack/author/native.h>
#include <ikea/seriespack/detail/native/planes.h>
#include <ikea/seriespack/detail/native/total.h>

namespace ikea::seriespack::author {
enum class field_kind { body, tail, head0, head1 };
template <class Source, field_kind Field> struct leaf {
    using layout_type = typename Source::layout_type;
    const Source& source;
    static constexpr auto kind = Field;
    static constexpr unsigned bit_width = Field == field_kind::body   ? 8 * layout_type::body
                                          : Field == field_kind::tail ? layout_type::tail
                                                                      : 8;
};
template <unsigned K, class Body, class Tail> struct payload_expression {
    static_assert(Body::bit_width == K / 8 * 8 && Tail::bit_width == K % 8);
    static constexpr unsigned bit_width = K;
    Body body;
    Tail tail;
};
/// Replace a residual child with any expression of the same unsigned width.
/// The new tree owns its nodes, but still borrows all named source views.
template <unsigned K, class B, class T, class Replacement>
auto with_tail(const payload_expression<K, B, T>& source, Replacement replacement) {
    return payload_expression<K, B, Replacement>{source.body, std::move(replacement)};
}
template <unsigned K, unsigned H, class Payload, class Head0, class Head1> struct value_expression {
    static_assert(Payload::bit_width == K - H);
    static constexpr unsigned bit_width = K, heads = H;
    Payload payload;
    Head0 head0;
    Head1 head1;
};
/// Substitute the complete payload while preserving head identity and row
/// coordinates. Admission subsequently visits only the leaves actually used.
template <unsigned K, unsigned H, class P, class H0, class H1, class Replacement>
auto with_payload(const value_expression<K, H, P, H0, H1>& source, Replacement replacement) {
    return value_expression<K, H, Replacement, H0, H1>{std::move(replacement), source.head0,
                                                       source.head1};
}
/// Source objects and their byte owners must outlive evaluation. No reads occur.
template <class Source> auto expression(const Source& source) {
    using F = typename Source::layout_type;
    using B = leaf<Source, field_kind::body>;
    using T = leaf<Source, field_kind::tail>;
    using H0 = leaf<Source, field_kind::head0>;
    using H1 = leaf<Source, field_kind::head1>;
    using P = payload_expression<F::payload, B, T>;
    return value_expression<F::width, F::heads, P, H0, H1>{
        {{source}, {source}}, {source}, {source}};
}
template <class Source> auto expression(const Source&&) = delete;

// Expression traversal is always inlined: an outlined read takes its row at
// run time, so a striped group's position in its tile cannot fold, and native
// accumulators spill around the call (Zen 5: striped K = 3 SUM 9 ns per 256
// rows against 1.3 inlined).
template <class Ops, class S, field_kind F, class Rows, class Mask>
[[gnu::always_inline]] inline auto read(Ops& ops, const leaf<S, F>& source, Rows rows, Mask active) {
    return ops.read(source, rows, active);
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Rows, class Mask>
[[gnu::always_inline]] inline auto read(Ops& ops, const value_expression<K, H, P, H0, H1>& source, Rows rows, Mask active);
template <class Ops, unsigned K, class B, class T, class Rows, class Mask>
[[gnu::always_inline]] inline auto read(Ops& ops, const payload_expression<K, B, T>& source, Rows rows, Mask active) {
    if constexpr (K < 8)
        return read(ops, source.tail, rows, active);
    else if constexpr (K % 8 == 0)
        return read(ops, source.body, rows, active);
    else
        return ops.template join<K % 8>(read(ops, source.body, rows, active),
                                        read(ops, source.tail, rows, active));
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Rows, class Mask>
[[gnu::always_inline]] inline auto read(Ops& ops, const value_expression<K, H, P, H0, H1>& source, Rows rows, Mask active) {
    if constexpr (H == 0)
        return read(ops, source.payload, rows, active);
    else {
        auto high = [&] {
            if constexpr (H == 8)
                return read(ops, source.head0, rows, active);
            else
                return ops.template join<8>(read(ops, source.head0, rows, active),
                                            read(ops, source.head1, rows, active));
        }();
        if constexpr (K == H)
            return high;
        else
            return ops.template join<K - H>(high, read(ops, source.payload, rows, active));
    }
}

/// Author once; recording, native evaluation and scalar evaluation use this body.
template <class Ops, class Source, class Rows, class Mask, class Cutoff>
[[gnu::always_inline]] inline auto selected_sum(Ops& ops, const Source& source, Rows rows, Mask active, Cutoff cutoff) {
    auto values = read(ops, source, rows, active);
    auto keep = ops.unsigned_less(values, cutoff, active);
    return ops.sum(values, keep);
}

/// An unconditioned SUM's admission: every active row, the maximum value
/// included (a cutoff of ~0 is strict and excludes it at 64 bits).
struct all_rows {};

template <unsigned K> struct scalar_value {
    std::uint64_t value;
};
struct scalar_ops {
    template <class S, field_kind Field>
    auto read(const leaf<S, Field>& field, std::size_t row, bool) const {
        using F = typename S::layout_type;
        constexpr bool Dense = requires { field.source.data; };
        const auto lane = row % F::tile_rows;
        if constexpr (Field == field_kind::head0 || Field == field_kind::head1) {
            constexpr unsigned p = Field == field_kind::head0 ? 1 : 2;
            const auto& stream = field.source.stream(p);
            return scalar_value<8>{ikea::to_u8(stream.bytes)[(row / F::tile_rows) * stream.stride + lane]};
        } else {
            const auto* base = [&] {
                if constexpr (Dense)
                    return ikea::to_u8(field.source.data);
                else
                    return ikea::to_u8(field.source.stream(0).bytes).data();
            }();
            const auto stride = [&] {
                if constexpr (Dense)
                    return F::tile_bytes;
                else
                    return field.source.stream(0).stride;
            }();
            const auto* tile = base + (row / F::tile_rows) * stride;
            if constexpr (Field == field_kind::body)
                return scalar_value<F::body * 8>{
                    detail::load<F::body>(tile + detail::body_offset<F>(lane))};
            else
                return scalar_value<F::tail>{detail::point_tail<F>(tile, lane)};
        }
    }
    template <unsigned Shift, unsigned A, unsigned B>
    auto join(scalar_value<A> high, scalar_value<B> low) const {
        return scalar_value<A + Shift>{(high.value << Shift) | low.value};
    }
    template <unsigned K>
    bool unsigned_less(scalar_value<K> x, std::uint64_t cutoff, bool active) const {
        return active && x.value < cutoff;
    }
    template <unsigned K> bool unsigned_less(scalar_value<K>, all_rows, bool active) const {
        return active;
    }
    template <unsigned K> std::uint64_t sum(scalar_value<K> x, bool active) const {
        return active ? x.value : 0;
    }
};

#if defined(__aarch64__) || defined(__AVX2__)
struct native_ops {
    template <class S, field_kind Field>
    [[gnu::always_inline]] auto read(const leaf<S, Field>& field, std::size_t row,
                                     std::uint16_t) const {
        __builtin_assume(row % 16 == 0);
        using F = typename S::layout_type;
        constexpr bool Dense = requires { field.source.data; };
        if constexpr (Field == field_kind::body || Field == field_kind::tail) {
            const auto* p = [&] {
                if constexpr (Dense)
                    return ikea::to_u8(field.source.data);
                else
                    return ikea::to_u8(field.source.stream(0).bytes).data();
            }();
            const auto stride = [&] {
                if constexpr (Dense)
                    return F::tile_bytes;
                else
                    return field.source.stream(0).stride;
            }();
            if constexpr (Field == field_kind::body)
                return native::body16<F, Dense>(p, stride, row);
            else
                return native::values<F::tail>{{native::tail16<F, Dense>(p, stride, row)}};
        } else {
            constexpr unsigned plane = Field == field_kind::head0 ? 1 : 2;
            static_assert(plane <= F::heads / 8);
            const auto& s = field.source.stream(plane);
            // A head follows its owning layout's tile boundaries, independently
            // of the payload placement. Local heads can be gapped every8 rows.
            const auto* p = ikea::to_u8(s.bytes).data() + (row / F::tile_rows) * s.stride + row % F::tile_rows;
            native::values<8> out;
#if defined(__aarch64__)
            if constexpr (F::tile_rows == 8)
                out.v[0] = vcombine_u8(vld1_u8(p), vld1_u8(p + s.stride));
            else
                out.v[0] = vld1q_u8(p);
#else
            if constexpr (F::tile_rows == 8)
                out.v[0] = _mm_set_epi64x(detail::load<8>(p + s.stride), detail::load<8>(p));
            else
                out.v[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#endif
            return out;
        }
    }
    template <unsigned S, unsigned A, unsigned B>
    [[gnu::always_inline]] auto join(native::values<A> high, native::values<B> low) const {
        return native::join<S>(high, low);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto unsigned_less(native::values<K> x, std::uint64_t cutoff,
                                              std::uint16_t active) const {
        return native::less(x, cutoff, active);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto unsigned_less(native::values<K>, all_rows, std::uint16_t active) const {
        return native::mask16<K>(active);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto sum(native::values<K> x, native::values<K> mask) const {
        return native::sum(x, mask);
    }
};

#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
template <unsigned N> struct wide_ops {
    template <class S, field_kind Field>
    [[gnu::always_inline]] auto read(const leaf<S, Field>& field, std::size_t row,
                                     std::uint64_t) const {
        using F = typename S::layout_type;
        constexpr bool Dense = requires { field.source.data; };
        if constexpr (Field == field_kind::body || Field == field_kind::tail) {
            const auto* p = [&] {
                if constexpr (Dense)
                    return ikea::to_u8(field.source.data);
                else
                    return ikea::to_u8(field.source.stream(0).bytes).data();
            }();
            const auto stride = [&] {
                if constexpr (Dense)
                    return F::tile_bytes;
                else
                    return field.source.stream(0).stride;
            }();
            if constexpr (Field == field_kind::body)
                return native_group::body<F, Dense, N>(p, stride, row);
            else
                return native_group::tail<F, Dense, N>(p, stride, row);
        } else
            return native_group::packets<8, N>(
                [&](unsigned offset) { return native_ops{}.read(field, row + offset, 0xffff); });
    }
    template <unsigned S, unsigned A, unsigned B>
    [[gnu::always_inline]] auto join(native_group::values<A, N> high,
                                     native_group::values<B, N> low) const {
        return native_group::join<S>(high, low);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto unsigned_less(native_group::values<K, N> x, std::uint64_t cutoff,
                                              std::uint64_t active) const {
        return native_group::less(x, cutoff, active);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto unsigned_less(native_group::values<K, N>, all_rows, std::uint64_t active) const {
        return native_group::active_mask<K, N>(active);
    }
    template <unsigned K>
    [[gnu::always_inline]] auto sum(native_group::values<K, N> x,
                                    native_group::selection<K, N> mask) const {
        return native_group::sum(x, mask);
    }
    template <class S, field_kind Field>
    [[gnu::always_inline]] auto total(const leaf<S, Field>& field, std::size_t row) const {
        const auto x = read(field, row, ~std::uint64_t{0});
        return native_group::sum(x, unsigned_less(x, all_rows{}, ~std::uint64_t{0}));
    }
    template <unsigned S>
    [[gnu::always_inline]] auto shifted(native_group::sum_state x) const {
        return native_group::shifted<S>(x);
    }
};
#endif

/// The unconditioned sum of a group. A sum is linear, so a join's total is
/// its high child's total shifted plus its low child's: fields are summed
/// apart and never joined or widened.
template <class Ops, class S, field_kind F, class Rows>
[[gnu::always_inline]] inline auto total(Ops& ops, const leaf<S, F>& source, Rows rows)
    requires requires { ops.total(source, rows); }
{
    return ops.total(source, rows);
}
/// A node has a total when every child it reads has one (found at
/// instantiation, so later overloads such as a guard's take part).
template <class Ops, class E>
concept totalled = requires(Ops& ops, const E& e) { total(ops, e, std::size_t{}); };
template <class Ops, unsigned S, class High, class Low>
[[gnu::always_inline]] inline auto total_join(Ops& ops, High high, Low low) {
    auto out = ops.template shifted<S>(high);
    out.add(low);
    return out;
}
template <class Ops, unsigned K, class B, class T, class Rows>
[[gnu::always_inline]] inline auto total(Ops& ops, const payload_expression<K, B, T>& source, Rows rows)
    requires((K < 8 || totalled<Ops, B>) && (K % 8 == 0 || totalled<Ops, T>))
{
    if constexpr (K < 8)
        return total(ops, source.tail, rows);
    else if constexpr (K % 8 == 0)
        return total(ops, source.body, rows);
    else
        return total_join<Ops, K % 8>(ops, total(ops, source.body, rows), total(ops, source.tail, rows));
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Rows>
[[gnu::always_inline]] inline auto total(Ops& ops, const value_expression<K, H, P, H0, H1>& source, Rows rows)
    requires((K == H || totalled<Ops, P>) && (H == 0 || totalled<Ops, H0>) && (H < 16 || totalled<Ops, H1>))
{
    if constexpr (H == 0)
        return total(ops, source.payload, rows);
    else {
        auto high = [&] {
            if constexpr (H == 8)
                return total(ops, source.head0, rows);
            else
                return total_join<Ops, 8>(ops, total(ops, source.head0, rows), total(ops, source.head1, rows));
        }();
        if constexpr (K == H)
            return high;
        else
            return total_join<Ops, K - H>(ops, high, total(ops, source.payload, rows));
    }
}

/// Rows per physical tile of a striped payload, or 0 (Local, heads, or a
/// substituted expression). Traversal can then fix each group's position in
/// its tile at compile time.
template <class E> inline constexpr std::size_t striped_tile_rows = 0;
template <unsigned K, class S>
inline constexpr std::size_t
    striped_tile_rows<value_expression<K, 0, payload_expression<K, leaf<S, field_kind::body>, leaf<S, field_kind::tail>>,
                                       leaf<S, field_kind::head0>, leaf<S, field_kind::head1>>> =
        S::layout_type::storage == geometry::striped ? S::layout_type::tile_rows : 0;

/// Calls body(row) for consecutive aligned groups of Grain rows. Inside
/// complete striped tiles a row is written as tile * tile_rows + constant, so
/// its stripe row group, fragment shifts and branches fold at compile time;
/// otherwise they are computed per group.
template <unsigned Grain, std::size_t Tile, class Body>
[[gnu::always_inline]] inline void each_group(std::size_t& i, std::size_t& count, Body&& body) {
    if constexpr (Tile > Grain && Tile % Grain == 0) {
        while (count >= Grain && i % Tile) {
            body(i);
            i += Grain, count -= Grain;
        }
        for (; count >= Tile; count -= Tile) {
            const std::size_t t = i / Tile;
            detail::each<Tile / Grain>([&](auto j) __attribute__((always_inline)) { body(t * Tile + j * Grain); });
            i = (t + 1) * Tile;
        }
    }
    for (; count >= Grain; i += Grain, count -= Grain)
        body(i);
}

/// The driver owns traversal, empty-mask skipping and result finalization.
/// Scalar remainders use the same authored operation, with no reads beyond the
/// logical extent. Prefilters name sixteen original positions at the given origin.
/// Groups whose expression has a fieldwise total (author::total); others,
/// such as a guarded or substituted node without one, reconstruct and sum.
template <class Ops, class Source>
inline constexpr bool has_total = totalled<Ops, Source>;

template <bool Wide = true, class Source, class Cutoff, class Prefilter>
std::uint64_t sum_regions(const Source& source, std::size_t first, std::size_t count,
                          Cutoff cutoff, Prefilter&& mask) {
    native_ops ops;
    native::sum_state result;
    scalar_ops scalar;
    std::uint64_t answer = 0;
    std::size_t i = first;
    if (count && i % 16) {
        const auto origin = i - i % 16;
        const auto active = static_cast<std::uint16_t>(mask(origin));
        do {
            if (active & (1u << (i - origin)))
                answer += selected_sum(scalar, source, i, true, cutoff);
            ++i;
            --count;
        } while (count && i % 16);
    }
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    if constexpr (Wide) {
        // 32 rows up to 32 bits: complete striped row groups and full ZMM lanes.
        constexpr unsigned grain = Source::bit_width <= 8 ? 64 : Source::bit_width <= 32 ? 32 : 16;
        wide_ops<grain> wide;
        native_group::sum_state partial;
        constexpr std::uint64_t full = ~std::uint64_t{0} >> (64 - grain);
        const bool open = [&] {
            if constexpr (std::is_same_v<Cutoff, all_rows>)
                return true;
            else
                return Source::bit_width < 64 && cutoff >> (Source::bit_width % 64);
        }();
        each_group<grain, striped_tile_rows<Source>>(i, count, [&](std::size_t row) __attribute__((always_inline)) {
            std::uint64_t active = 0;
            detail::each<grain / 16>([&](auto p) {
                active |= std::uint64_t(static_cast<std::uint16_t>(mask(row + p * 16))) << (p * 16);
            });
            // Complete unconditioned groups sum their fields apart.
            if constexpr (has_total<decltype(wide), Source>)
                if (active == full && open) {
                    partial.add(total(wide, source, row));
                    return;
                }
            if (active)
                partial.add(selected_sum(wide, source, row, active, cutoff));
        });
        answer += partial.finish();
    }
#endif
    each_group<16, striped_tile_rows<Source>>(i, count, [&](std::size_t row) __attribute__((always_inline)) {
        const auto active = static_cast<std::uint16_t>(mask(row));
        if (active)
            result.add(selected_sum(ops, source, row, active, cutoff));
    });
    answer += result.finish();
    if (count) {
        const auto active = static_cast<std::uint16_t>(mask(i));
        const auto origin = i;
        for (; count; --count, ++i)
            if (active & (1u << (i - origin)))
                answer += selected_sum(scalar, source, i, true, cutoff);
    }
    return answer;
}
/// One decode, many consumers: group(row, values) receives each complete
/// aligned group of the widest native grain in [first, first + count), its
/// values still in registers, so any number of consumers composed inside it
/// share one reconstruction; edge(row, value) receives every other row.
/// Groups arrive in row order, inside complete striped tiles at compile-time
/// positions (as sum_regions). Rows outside the range are not read. Values are
/// native_group::values<K, grain> on NEON and AVX-512, native::values<K> (16
/// rows) on AVX2 alone.
template <class Source, class Group, class Edge>
[[gnu::always_inline]] inline void scan_regions(const Source& source, std::size_t first, std::size_t count,
                                                Group&& group, Edge&& edge) {
    constexpr unsigned K = Source::bit_width;
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    constexpr unsigned grain = K <= 8 ? 64 : K <= 32 ? 32 : 16;
    wide_ops<grain> ops;
    constexpr auto all = ~std::uint64_t{0};
#else
    constexpr unsigned grain = 16;
    native_ops ops;
    constexpr auto all = std::uint16_t{0xffff};
#endif
    scalar_ops scalar;
    std::size_t i = first;
    for (; count && i % grain; ++i, --count)
        edge(i, read(scalar, source, i, true).value);
    each_group<grain, striped_tile_rows<Source>>(i, count, [&](std::size_t row) __attribute__((always_inline)) {
        group(row, read(ops, source, row, all));
    });
    for (; count; ++i, --count)
        edge(i, read(scalar, source, i, true).value);
}
/// Modulo-2^64 sum of weights[row % period] * value over [first, first +
/// count), period 16 or 64: lanes are multiplied in registers inside
/// scan_regions, edges are scalar. Exact while count * 255 * 2^K < 2^64;
/// exact_regions below chunks it, or uses 32-bit halves, to stay exact.
template <class Source>
std::uint64_t weighted_sum_regions(const Source& source, std::size_t first, std::size_t count,
                                   const std::uint8_t* weights, unsigned period) {
    constexpr unsigned K = Source::bit_width;
    if (!count)
        return 0;  // reads no weight
    std::uint64_t edges = 0;
    const auto edge = [&](std::size_t row, std::uint64_t value) { edges += weights[row & (period - 1)] * value; };
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    // A part holds eight u64 lanes; its weights are the eight bytes at
    // (row + 8p) % period, widened (no per-call table).
    __m512i acc = _mm512_setzero_si512();
    scan_regions(
        source, first, count,
        [&](std::size_t row, const auto& x) __attribute__((always_inline)) {
            const auto y = zmm::widen<64>(x);
            detail::each<y.v.size()>([&](auto p) __attribute__((always_inline)) {
                const auto wp = _mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(weights + ((row + 8 * p) & (period - 1)))));
                acc = _mm512_add_epi64(acc, _mm512_mullo_epi64(y.v[p], wp));
            });
        },
        edge);
    return edges + static_cast<std::uint64_t>(_mm512_reduce_add_epi64(acc));
#elif defined(__aarch64__)
    // Per 16-row block, the sixteen weight bytes at (row + 16p) % period:
    // u8 or u16 products below 17 bits, u32 lanes up to 32, halves above.
    auto acc = vdupq_n_u64(0);
    scan_regions(
        source, first, count,
        [&](std::size_t row, const auto& x) __attribute__((always_inline)) {
            detail::each<std::tuple_size_v<std::remove_cvref_t<decltype(x.block)>>>([&](auto p) __attribute__((always_inline)) {
                const auto wv = vld1q_u8(weights + ((row + 16 * p) & (period - 1)));
                const std::array<uint16x8_t, 2> w16{vmovl_u8(vget_low_u8(wv)), vmovl_high_u8(wv)};
                if constexpr (K <= 8) {
                    // u8 x u8 products in u16 lanes, pairwise into u32, then u64.
                    const auto v = x.block[p].v[0];
                    auto pairs = vpaddlq_u16(vmull_u8(vget_low_u8(v), vget_low_u8(wv)));
                    pairs = vpadalq_u16(pairs, vmull_high_u8(v, wv));
                    acc = vpadalq_u32(acc, pairs);
                } else if constexpr (K <= 16) {
                    detail::each<2>([&](auto j) __attribute__((always_inline)) {
                        const auto v = vreinterpretq_u16_u8(x.block[p].v[j]);
                        acc = vpadalq_u32(acc, vmull_u16(vget_low_u16(v), vget_low_u16(w16[j])));
                        acc = vpadalq_u32(acc, vmull_high_u16(v, w16[j]));
                    });
                } else if constexpr (K <= 32) {
                    const auto y = neon::widen<32>(x.block[p]);
                    detail::each<4>([&](auto j) __attribute__((always_inline)) {
                        const auto v = vreinterpretq_u32_u8(y.v[j]);
                        const auto wj = j % 2 ? vmovl_high_u16(w16[j / 2]) : vmovl_u16(vget_low_u16(w16[j / 2]));
                        acc = vmlal_u32(acc, vget_low_u32(v), vget_low_u32(wj));
                        acc = vmlal_high_u32(acc, v, wj);
                    });
                } else {
                    const auto y = neon::widen<64>(x.block[p]);
                    detail::each<8>([&](auto j) __attribute__((always_inline)) {
                        const auto v = vreinterpretq_u64_u8(y.v[j]);
                        const auto w4 = (j / 2) % 2 ? vmovl_high_u16(w16[j / 4]) : vmovl_u16(vget_low_u16(w16[j / 4]));
                        const auto wj = j % 2 ? vget_high_u32(w4) : vget_low_u32(w4);
                        acc = vmlal_u32(acc, vmovn_u64(v), wj);
                        acc = vaddq_u64(acc, vshlq_n_u64(vmull_u32(vshrn_n_u64(v, 32), wj), 32));
                    });
                }
            });
        },
        edge);
    return edges + vaddvq_u64(acc);
#else
    std::uint64_t total = 0;
    scan_regions(
        source, first, count,
        [&](std::size_t row, const auto& x) {
            std::uint64_t values[16];
            native::store16(values, x);
            for (unsigned j = 0; j < 16; ++j)
                total += weights[(row + j) & (period - 1)] * values[j];
        },
        edge);
    return edges + total;
#endif
}

/// Exact u128 sums by 32-bit halves: each selected value's low and high
/// halves, times its weight (at most 255), add into separate u64 lanes,
/// which stay below 2^40 per group, folded into u128 every 2^20 rows.
/// weights may be null (weight 1); mask(origin) selects 16-row regions.
template <bool Weighted, class Source, class Mask>
unsigned __int128 exact_halves_regions(const Source& source, std::size_t first, std::size_t count,
                                       const std::uint8_t* weights, unsigned period, Mask&& mask) {
    using u128 = unsigned __int128;
    u128 total = 0;
    const auto weight = [&](std::size_t row) -> std::uint64_t {
        if constexpr (Weighted)
            return weights[row & (period - 1)];
        else
            return 1;
    };
    const auto edge = [&](std::size_t row, std::uint64_t value) {
        if (mask(row - row % 16) >> (row % 16) & 1)
            total += static_cast<u128>(value) * weight(row);
    };
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    const auto lower = _mm512_set1_epi64(0xffffffff);
    for (std::size_t at = 0; at < count; at += std::size_t{1} << 20) {
        const std::size_t n = std::min<std::size_t>(count - at, std::size_t{1} << 20);
        __m512i lo = _mm512_setzero_si512(), hi = _mm512_setzero_si512();
        scan_regions(
            source, first + at, n,
            [&](std::size_t row, const auto& x) __attribute__((always_inline)) {
                const auto y = zmm::widen<64>(x);
                detail::each<y.v.size()>([&](auto p) __attribute__((always_inline)) {
                    const std::size_t r = row + 8 * p;
                    const auto m = static_cast<__mmask8>(mask(r - r % 16) >> (r % 16));
                    const auto v = _mm512_maskz_mov_epi64(m, y.v[p]);
                    if constexpr (Weighted) {
                        const auto wp = _mm512_cvtepu8_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(weights + (r & (period - 1)))));
                        lo = _mm512_add_epi64(lo, _mm512_mul_epu32(_mm512_and_si512(v, lower), wp));
                        hi = _mm512_add_epi64(hi, _mm512_mul_epu32(_mm512_srli_epi64(v, 32), wp));
                    } else {
                        lo = _mm512_add_epi64(lo, _mm512_and_si512(v, lower));
                        hi = _mm512_add_epi64(hi, _mm512_srli_epi64(v, 32));
                    }
                });
            },
            edge);
        total += static_cast<u128>(static_cast<std::uint64_t>(_mm512_reduce_add_epi64(lo))) +
                 (static_cast<u128>(static_cast<std::uint64_t>(_mm512_reduce_add_epi64(hi))) << 32);
    }
#elif defined(__aarch64__)
    static constexpr std::uint8_t ones[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    for (std::size_t at = 0; at < count; at += std::size_t{1} << 20) {
        const std::size_t n = std::min<std::size_t>(count - at, std::size_t{1} << 20);
        auto lo = vdupq_n_u64(0), hi = vdupq_n_u64(0);
        scan_regions(
            source, first + at, n,
            [&](std::size_t row, const auto& x) __attribute__((always_inline)) {
                detail::each<std::tuple_size_v<std::remove_cvref_t<decltype(x.block)>>>([&](auto p) __attribute__((always_inline)) {
                    const std::size_t r = row + 16 * p;
                    const auto bits = mask(r - r % 16) >> (r % 16);
                    const auto w8 = vld1q_u8(Weighted ? weights + (r & (period - 1)) : ones);
                    const std::array<uint16x8_t, 2> w16{vmovl_u8(vget_low_u8(w8)), vmovl_high_u8(w8)};
                    const auto y = neon::widen<64>(x.block[p]);
                    detail::each<8>([&](auto j) __attribute__((always_inline)) {
                        const auto v = vreinterpretq_u64_u8(y.v[j]);
                        const std::uint64_t pair = (bits >> (2 * j) & 1 ? 0xffffffffULL : 0) | (bits >> (2 * j + 1) & 1 ? 0xffffffff00000000ULL : 0);
                        const auto w4 = (j / 2) % 2 ? vmovl_high_u16(w16[j / 4]) : vmovl_u16(vget_low_u16(w16[j / 4]));
                        const auto wj = vand_u32(j % 2 ? vget_high_u32(w4) : vget_low_u32(w4), vcreate_u32(pair));
                        lo = vmlal_u32(lo, vmovn_u64(v), wj);
                        hi = vmlal_u32(hi, vshrn_n_u64(v, 32), wj);
                    });
                });
            },
            edge);
        total += static_cast<u128>(vaddvq_u64(lo)) + (static_cast<u128>(vaddvq_u64(hi)) << 32);
    }
#elif defined(__AVX2__)
    // Four u64 lanes per part: a lane's selection bit becomes an all-ones
    // or zero lane, so unselected rows contribute zero to both halves.
    const auto lower = _mm256_set1_epi64x(0xffffffff), lane_bits = _mm256_setr_epi64x(1, 2, 4, 8);
    for (std::size_t at = 0; at < count; at += std::size_t{1} << 20) {
        const std::size_t n = std::min<std::size_t>(count - at, std::size_t{1} << 20);
        __m256i lo = _mm256_setzero_si256(), hi = _mm256_setzero_si256();
        scan_regions(
            source, first + at, n,
            [&](std::size_t row, const auto& x) __attribute__((always_inline)) {
                const auto y = native::widen<64>(x);
                const auto bits = mask(row);
                detail::each<y.v.size()>([&](auto p) __attribute__((always_inline)) {
                    const auto selected = _mm256_and_si256(_mm256_set1_epi64x(bits >> (4 * p)), lane_bits);
                    const auto v = _mm256_and_si256(_mm256_cmpeq_epi64(selected, lane_bits), y.v[p]);
                    if constexpr (Weighted) {
                        const std::size_t r = row + 4 * p;
                        std::uint32_t w4;
                        std::memcpy(&w4, weights + (r & (period - 1)), 4);
                        const auto wp = _mm256_cvtepu8_epi64(_mm_cvtsi32_si128(static_cast<int>(w4)));
                        lo = _mm256_add_epi64(lo, _mm256_mul_epu32(_mm256_and_si256(v, lower), wp));
                        hi = _mm256_add_epi64(hi, _mm256_mul_epu32(_mm256_srli_epi64(v, 32), wp));
                    } else {
                        lo = _mm256_add_epi64(lo, _mm256_and_si256(v, lower));
                        hi = _mm256_add_epi64(hi, _mm256_srli_epi64(v, 32));
                    }
                });
            },
            edge);
        alignas(32) std::uint64_t l[4], h[4];
        _mm256_store_si256(reinterpret_cast<__m256i*>(l), lo), _mm256_store_si256(reinterpret_cast<__m256i*>(h), hi);
        total += static_cast<u128>(l[0] + l[1] + l[2] + l[3]) + (static_cast<u128>(h[0] + h[1] + h[2] + h[3]) << 32);
    }
#else
    scan_regions(
        source, first, count,
        [&](std::size_t row, const auto& x) {
            std::uint64_t values[16];
            native::store16(values, x);
            const auto bits = mask(row);
            for (unsigned j = 0; j < 16; ++j)
                if (bits >> j & 1)
                    total += static_cast<unsigned __int128>(values[j]) * weight(row + j);
        },
        edge);
#endif
    return total;
}

/// The layout of a headless whole-source dense expression, else void.
template <class E> struct dense_layout {
    using type = void;
};
template <unsigned K, class F>
struct dense_layout<value_expression<K, 0,
                                     payload_expression<K, leaf<dense_source<F>, field_kind::body>,
                                                        leaf<dense_source<F>, field_kind::tail>>,
                                     leaf<dense_source<F>, field_kind::head0>,
                                     leaf<dense_source<F>, field_kind::head1>>> {
    using type = F;
};

/// The sum of every row of [first, first + count). Whole periods of complete
/// tiles take a one-pass field sum; edges use the group driver with every
/// row active.
template <class Source>
std::uint64_t total_regions(const Source& source, std::size_t first, std::size_t count) {
    const auto all = [](std::size_t) { return std::uint16_t{0xffff}; };
    using F = typename dense_layout<Source>::type;
    if constexpr (!std::is_void_v<F>) {
        using kernel = std::conditional_t<F::storage == geometry::local, ikea::seriespack::detail::local_total<F>,
                                          ikea::seriespack::detail::striped_total<F>>;
        if constexpr (kernel::supported) {
            const std::size_t start = (first + F::tile_rows - 1) / F::tile_rows * F::tile_rows;
            const std::size_t periods = count >= start - first ? (count - (start - first)) / kernel::rows : 0;
            if (periods) {
                const auto* tiles = ikea::to_u8(source.payload.body.source.data) + start / F::tile_rows * F::tile_bytes;
                const std::size_t end = start + periods * kernel::rows;
                return sum_regions(source, first, start - first, all_rows{}, all) + kernel::run(tiles, periods) +
                       sum_regions(source, end, first + count - end, all_rows{}, all);
            }
        }
    }
    return sum_regions(source, first, count, all_rows{}, all);
}

/// Exact sums as u128: the selected rows (mask(origin), or every row), each
/// weighted by weights[row % period] when weights is given (period 16 or
/// 64). Below the width that could wrap, the modulo kernels run on chunks
/// short enough to be exact; wider widths take 32-bit halves.
template <class Source, class Mask>
unsigned __int128 exact_regions(const Source& source, std::size_t first, std::size_t count, const std::uint8_t* weights,
                                unsigned period, bool masked, Mask&& mask) {
    constexpr unsigned K = Source::bit_width;
    // Rows whose maximum total stays below 2^64: 2^(64 - K - 8 for weights).
    const unsigned bits = K + (weights ? 8 : 0);
    const std::size_t chunk = bits >= 64 ? 0 : std::min<std::size_t>(std::size_t{1} << (64 - bits), std::size_t{1} << 40);
    if (chunk >= 256 && !(weights && masked)) {
        unsigned __int128 total = 0;
        for (std::size_t at = 0; at < count; at += chunk) {
            const std::size_t n = std::min(chunk, count - at);
            if (weights)
                total += weighted_sum_regions(source, first + at, n, weights, period);
            else if (masked)
                total += sum_regions(source, first + at, n, all_rows{}, mask);
            else
                total += total_regions(source, first + at, n);
        }
        return total;
    }
    return weights ? exact_halves_regions<true>(source, first, count, weights, period, mask)
                   : exact_halves_regions<false>(source, first, count, weights, period, mask);
}

template <bool Wide = true, class Source, class Cutoff, class Prefilter>
std::uint64_t sum_regions(const Source& source, std::size_t count, Cutoff cutoff,
                          Prefilter&& mask) {
    return sum_regions<Wide>(source, 0, count, cutoff, std::forward<Prefilter>(mask));
}
#endif
} // namespace ikea::seriespack::author
