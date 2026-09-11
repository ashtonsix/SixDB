#pragma once

#include <ikea/seriespack/layout.h>
#include <ikea/seriespack/operations.h>

#include <cstddef>
#include <utility>

namespace ikea::seriespack::composition {

/// Whole-byte high part of the low payload, before joining residual bits.
template<class Format, class Source> struct body_ref {
    const Source& source;
    using format_type = Format;
    static constexpr unsigned bit_width = payload_width(Format::layout) / 8 * 8;
};
/// Residual low bits of the payload; bit_width is in 0..7.
template<class Format, class Source> struct tail_ref {
    const Source& source;
    using format_type = Format;
    static constexpr unsigned bit_width = payload_width(Format::layout) % 8;
};
/// Independently readable head byte; plane 0 is most significant.
template<class Format, unsigned Plane, class Source> struct head_ref {
    const Source& source;
    using format_type = Format;
    static constexpr unsigned plane = Plane;
    static constexpr unsigned bit_width = 8;
};

/// Body supplies high bits, tail low bits. Children may bind different sources;
/// substitution must preserve their bit domains, coordinates and actual dependencies.
template<unsigned Width, class Body, class Tail> struct payload_expression {
    static_assert(Width <= 64);
    static_assert(Body::bit_width == Width / 8 * 8);
    static_assert(Tail::bit_width == Width % 8);
    static constexpr unsigned bit_width = Width;
    Body body;
    Tail tail;
};
/// Unsigned full-value expression. Width is a bit domain, not a lane width or row count.
template<unsigned Width, unsigned Heads, class Payload, class Head0, class Head1>
struct value_expression {
    static_assert(Width >= 1 && Width <= 64);
    static_assert((Heads == 0 || Heads == 8 || Heads == 16) && Heads <= Width);
    static_assert(Payload::bit_width == Width - Heads);
    static constexpr unsigned bit_width = Width;
    static constexpr unsigned head_bits = Heads;
    Payload payload;
    Head0 head0;
    Head1 head1;
};

/// Leaves borrow the named source object as well as its bytes. Rebuild after moving
/// the source. Runtime descriptions must already match Format; no reads or checks occur.
template<class Format, class Source>
constexpr auto describe(const Source& source) {
    if constexpr (requires { typename Source::format_type; }) {
        static_assert(Format::layout == Source::format_type::layout,
                      "A static source must retain its actual representation");
    }
    constexpr auto d = Format::layout;
    using Body = body_ref<Format, Source>;
    using Tail = tail_ref<Format, Source>;
    using Payload = payload_expression<payload_width(d), Body, Tail>;
    return value_expression<d.width, d.head_bits, Payload,
                            head_ref<Format, 0, Source>, head_ref<Format, 1, Source>>{
        Payload{Body{source}, Tail{source}}, {source}, {source}};
}
template<class Format, class Source>
auto describe(const Source&&) = delete;
/// Infer Format from a named static source; returned leaves borrow that object.
template<class Source>
constexpr auto describe(const Source& source) {
    return describe<typename Source::format_type>(source);
}
template<class Source>
auto describe(const Source&&) = delete;

/// Expand through the required children. Ops must preserve compatible lane maps;
/// selection does not grant read permission or suppress empty-mask reads.
template<class Ops, unsigned W, class Body, class Tail, class Rows, class Active>
inline auto expand_payload(Ops& ops, const payload_expression<W, Body, Tail>& parts,
                           const Rows& rows, const Active& active) {
    static_assert(W != 0, "An empty payload is not read; heads supply the value");
    if constexpr (W < 8) {
        return ops.read_tail(parts.tail, rows, active);
    } else if constexpr (W % 8 == 0) {
        return ops.read_body(parts.body, rows, active);
    } else {
        auto high = ops.read_body(parts.body, rows, active);
        auto low = ops.read_tail(parts.tail, rows, active);
        return ops.template join<W % 8>(high, low);
    }
}
/// Optional compile-time substitution; preserve expansion semantics and each actual dependency.
template<class Ops, unsigned W, class Body, class Tail, class Rows, class Active>
inline auto read_payload(Ops& ops, const payload_expression<W, Body, Tail>& parts,
                         const Rows& rows, const Active& active) {
    if constexpr (requires { ops.read_payload(parts, rows, active); })
        return ops.read_payload(parts, rows, active);
    else
        return expand_payload(ops, parts, rows, active);
}
/// Reconstruct the expression; result representation comes from Ops.
template<class Ops, unsigned K, unsigned H, class Payload, class H0, class H1,
         class Rows, class Active>
inline auto read(Ops& ops, const value_expression<K, H, Payload, H0, H1>& source,
                 const Rows& rows, const Active& active) {
    if constexpr (H == 0) {
        return read_payload(ops, source.payload, rows, active);
    } else {
        auto high = [&] {
            auto h0 = ops.read_head(source.head0, rows, active);
            if constexpr (H == 8) return h0;
            else {
                auto h1 = ops.read_head(source.head1, rows, active);
                return ops.template join<8>(h0, h1);
            }
        }();
        if constexpr (K == H) return high;
        else {
            auto low = read_payload(ops, source.payload, rows, active);
            return ops.template join<K - H>(high, low);
        }
    }
}

/// Read this head alone. Projection does not change the executor's lane width or grain.
template<class Ops, class Format, unsigned Plane, class Source, class Rows, class Active>
inline auto read(Ops& ops, const head_ref<Format, Plane, Source>& source,
                 const Rows& rows, const Active& active) {
    static_assert(Plane < Format::layout.head_bits / 8, "The format has no such head plane");
    return ops.read_head(source, rows, active);
}

/// Read once and sum values strictly below cutoff, modulo 2^64; Ops chooses the result type.
/// Executes all three operations even for an empty mask; no traversal or stop protocol.
template<class Ops, class Source, class Rows, class Active, class Cutoff>
inline auto selected_sum(Ops& ops, const Source& source, const Rows& rows,
                         const Active& active, const Cutoff& cutoff) {
    auto values = read(ops, source, rows, active);
    auto keep = ops.unsigned_less(values, cutoff, active);
    return ops.sum(values, keep, modulo_u64_sum{});
}

/// Pass values, original coordinates and selection to store<Source::bit_width>.
/// The admitted sink writes only selected original positions, without compaction.
/// No empty-mask shortcut; the store result is ignored.
template<class Ops, class Source, class Rows, class Active, class Sink>
inline void materialize(Ops& ops, const Source& source, const Rows& rows,
                        const Active& active, Sink& sink) {
    auto values = read(ops, source, rows, active);
    sink.template store<Source::bit_width>(values, rows, active);
}

template<std::size_t Lanes> struct ordered_lanes {
    static_assert(Lanes > 0);
    static constexpr std::size_t lanes = Lanes;
    static constexpr std::size_t offset(std::size_t lane) { return lane; }
};
/// Two runs of Run lanes at offsets 0 and Second; any gap is preserved.
template<std::size_t Run, std::size_t Second> struct two_run_lanes {
    static_assert(Run > 0 && Second >= Run);
    static constexpr std::size_t lanes = 2 * Run;
    static constexpr std::size_t offset(std::size_t lane) {
        return lane < Run ? lane : Second + lane - Run;
    }
};
/// Original positions, independent of storage geometry. Index arithmetic is unchecked.
template<class Map> struct lane_coordinates {
    std::size_t origin;
    static constexpr std::size_t lanes = Map::lanes;
    constexpr std::size_t index(std::size_t lane) const { return origin + Map::offset(lane); }
};

} // namespace ikea::seriespack::composition
