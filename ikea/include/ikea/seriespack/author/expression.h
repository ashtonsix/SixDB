#pragma once
#include <ikea/seriespack/author/native.h>

namespace ikea::seriespack::composition {
enum class field_kind { body, tail, head0, head1 };
template <class Source, field_kind Field> struct leaf {
    using format_type = typename Source::format_type;
    const Source& source;
    static constexpr auto kind = Field;
    static constexpr unsigned bit_width = Field == field_kind::body   ? 8 * format_type::body
                                          : Field == field_kind::tail ? format_type::tail
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
template <class Source> auto describe(const Source& source) {
    using F = typename Source::format_type;
    using B = leaf<Source, field_kind::body>;
    using T = leaf<Source, field_kind::tail>;
    using H0 = leaf<Source, field_kind::head0>;
    using H1 = leaf<Source, field_kind::head1>;
    using P = payload_expression<F::payload, B, T>;
    return value_expression<F::width, F::heads, P, H0, H1>{
        {{source}, {source}}, {source}, {source}};
}
template <class Source> auto describe(const Source&&) = delete;

template <class Ops, class S, field_kind F, class Rows, class Mask>
auto read(Ops& ops, const leaf<S, F>& source, Rows rows, Mask active) {
    return ops.read(source, rows, active);
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Rows, class Mask>
auto read(Ops& ops, const value_expression<K, H, P, H0, H1>& source, Rows rows, Mask active);
template <class Ops, unsigned K, class B, class T, class Rows, class Mask>
auto read(Ops& ops, const payload_expression<K, B, T>& source, Rows rows, Mask active) {
    if constexpr (K < 8)
        return read(ops, source.tail, rows, active);
    else if constexpr (K % 8 == 0)
        return read(ops, source.body, rows, active);
    else
        return ops.template join<K % 8>(read(ops, source.body, rows, active),
                                        read(ops, source.tail, rows, active));
}
template <class Ops, unsigned K, unsigned H, class P, class H0, class H1, class Rows, class Mask>
auto read(Ops& ops, const value_expression<K, H, P, H0, H1>& source, Rows rows, Mask active) {
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
auto selected_sum(Ops& ops, const Source& source, Rows rows, Mask active, Cutoff cutoff) {
    auto values = read(ops, source, rows, active);
    auto keep = ops.unsigned_less(values, cutoff, active);
    return ops.sum(values, keep);
}

template <unsigned K> struct scalar_value {
    std::uint64_t value;
};
struct scalar_ops {
    template <class S, field_kind Field>
    auto read(const leaf<S, Field>& field, std::size_t row, bool) const {
        using F = typename S::format_type;
        constexpr bool Dense = requires { field.source.data; };
        const auto lane = row % F::tile_rows;
        if constexpr (Field == field_kind::head0 || Field == field_kind::head1) {
            constexpr unsigned p = Field == field_kind::head0 ? 1 : 2;
            const auto& stream = field.source.stream(p);
            return scalar_value<8>{stream.bytes[(row / F::tile_rows) * stream.stride + lane]};
        } else {
            const auto* base = [&] {
                if constexpr (Dense)
                    return field.source.data;
                else
                    return field.source.stream(0).bytes.data();
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
        using F = typename S::format_type;
        constexpr bool Dense = requires { field.source.data; };
        if constexpr (Field == field_kind::body || Field == field_kind::tail) {
            const auto* p = [&] {
                if constexpr (Dense)
                    return field.source.data;
                else
                    return field.source.stream(0).bytes.data();
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
            // A head follows its owning format's tile boundaries, independently
            // of the payload placement. Local heads can be gapped every8 rows.
            const auto* p = s.bytes.data() + (row / F::tile_rows) * s.stride + row % F::tile_rows;
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
        using F = typename S::format_type;
        constexpr bool Dense = requires { field.source.data; };
        if constexpr (Field == field_kind::body || Field == field_kind::tail) {
            const auto* p = [&] {
                if constexpr (Dense)
                    return field.source.data;
                else
                    return field.source.stream(0).bytes.data();
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
    [[gnu::always_inline]] auto sum(native_group::values<K, N> x,
                                    native_group::selection<K, N> mask) const {
        return native_group::sum(x, mask);
    }
};
#endif

/// The driver owns traversal, empty-mask skipping and result finalization.
/// Scalar remainders use the same authored operation, with no reads beyond the
/// logical extent. Prefilters name sixteen original positions at the given origin.
template <bool Wide = true, class Source, class Prefilter>
std::uint64_t sum_regions(const Source& source, std::size_t first, std::size_t count,
                          std::uint64_t cutoff, Prefilter&& mask) {
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
        constexpr unsigned grain = Source::bit_width <= 8 ? 64 : Source::bit_width <= 16 ? 32 : 16;
        wide_ops<grain> wide;
        native_group::sum_state partial;
        for (; count >= grain; i += grain, count -= grain) {
            std::uint64_t active = 0;
            detail::each<grain / 16>([&](auto p) {
                active |= std::uint64_t(static_cast<std::uint16_t>(mask(i + p * 16))) << (p * 16);
            });
            if (active)
                partial.add(selected_sum(wide, source, i, active, cutoff));
        }
        answer += partial.finish();
    }
#endif
    for (; count >= 16; i += 16, count -= 16) {
        const auto active = static_cast<std::uint16_t>(mask(i));
        if (active)
            result.add(selected_sum(ops, source, i, active, cutoff));
    }
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
template <bool Wide = true, class Source, class Prefilter>
std::uint64_t sum_regions(const Source& source, std::size_t count, std::uint64_t cutoff,
                          Prefilter&& mask) {
    return sum_regions<Wide>(source, 0, count, cutoff, std::forward<Prefilter>(mask));
}
#endif
} // namespace ikea::seriespack::composition
