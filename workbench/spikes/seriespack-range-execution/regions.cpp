#include "regions.h"

#include <ikea/seriespack/composition_neon.h>
#include <ikea/seriespack/composition_x86.h>
#include <ikea/seriespack/detail/range_regions.h>

#include <array>
#include <type_traits>

namespace seriespack_measurement {
namespace {
namespace sp = ikea::seriespack;
namespace comp = sp::composition;
namespace regions = sp::detail::range_regions;
using U = std::uint64_t;

#if defined(__aarch64__) || defined(__AVX2__)
#if defined(__aarch64__)
struct neon_target {
    static constexpr unsigned lanes = 2;
    using vector = uint8x16_t;
    using rows = sp::neon::tile_position;
    template<class F, unsigned Begin> using ops = sp::neon::composition_ops<F, Begin>;
    template<unsigned Begin> static auto expand(regions::bytes16 bytes) {
        return sp::detail::neon::body_detail::expand_bytes<1, 8, Begin>(bytes);
    }
    template<unsigned Q> static auto body(const std::uint8_t* p) {
        return sp::detail::neon::decode_body_prefix<Q, 8, lanes>(p);
    }
    template<unsigned R> static auto join(vector high, vector low) { return sp::neon::join<8, R>(high, low); }
    static void store(U* out, vector values) { vst1q_u64(out, vreinterpretq_u64_u8(values)); }
};
#endif
#if defined(__AVX2__)
struct avx2_target {
    static constexpr unsigned lanes = 4;
    using vector = __m256i;
    using rows = sp::avx2::tile_position;
    template<class F, unsigned Begin> using ops = sp::avx2::composition_ops<F, Begin>;
    template<unsigned Begin> static auto expand(regions::bytes16 bytes) {
        return sp::avx2::native_detail::expand_bytes<8>(_mm_srli_si128(bytes, Begin));
    }
    template<unsigned Q> static auto body(const std::uint8_t* p) {
        return sp::detail::avx2::decode_body_prefix<Q, 8, lanes>(p);
    }
    template<unsigned R> static auto join(vector high, vector low) { return sp::avx2::join<8, R>(high, low); }
    static void store(U* out, vector values) { _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), values); }
};
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
struct avx512_target {
    static constexpr unsigned lanes = 8;
    using vector = __m512i;
    using rows = sp::avx512::tile_position;
    template<class F, unsigned Begin> using ops = sp::avx512::composition_ops<F, Begin>;
    template<unsigned Begin> static auto expand(regions::bytes16 bytes) {
        return sp::avx512::native_detail::expand_bytes<8>(_mm_srli_si128(bytes, Begin));
    }
    template<unsigned Q> static auto body(const std::uint8_t* p) {
        return sp::detail::avx512::decode_body_prefix<Q, 8, lanes>(p);
    }
    template<unsigned R> static auto join(vector high, vector low) { return sp::avx512::join<8, R>(high, low); }
    static void store(U* out, vector values) { _mm512_storeu_si512(out, values); }
};
#endif
#endif

// These are immediate native values: every byte names the corresponding
// original row, independently of the unsigned64 materializing sink's grain.
// This executor borrows the actual child, never a captured presumed source.
struct active16 {};
using original_rows = comp::lane_coordinates<comp::ordered_lanes<16>>;

template<class Format>
struct byte_source_ops {
    static constexpr auto d = Format::layout;
    static constexpr unsigned W = d.width;
    template<class F, class Source>
    [[gnu::always_inline]] auto read_tail(const comp::tail_ref<F, Source>& ref,
                                         original_rows rows, active16) const {
        static_assert(F::layout == d);
        const auto& plane = ref.source.placement().payload;
        const auto* p = reinterpret_cast<const std::uint8_t*>(plane.bytes.data());
        constexpr auto T = F::payload::tile_values;
        const auto tile = rows.origin / T;
        p += tile * plane.stride;
        if constexpr (d.storage == sp::geometry::striped) {
            return regions::striped<W, 16>(p, static_cast<unsigned>(rows.origin % T));
        } else {
            static_assert(W <= 7);
            if (plane.stride == W) return regions::local_pair<W>(p);
#if defined(__aarch64__)
            const auto first = sp::neon::read_fragment<W, sp::geometry::local8, 1, 0>(p);
            const auto second = sp::neon::read_fragment<W, sp::geometry::local8, 1, 0>(p + plane.stride);
            return vcombine_u8(vget_low_u8(first), vget_low_u8(second));
#else
            const auto first = sp::avx2::read_fragment<W, sp::geometry::local8, 1, 0>(p);
            const auto second = sp::avx2::read_fragment<W, sp::geometry::local8, 1, 0>(p + plane.stride);
            return _mm_unpacklo_epi64(_mm256_castsi256_si128(first), _mm256_castsi256_si128(second));
#endif
        }
    }
};

// The wider striped region retains the same sixteen-byte tail source, and
// joins each independent body child into the sink's native registers. The
// array is an inline value expression, never an out-of-line aggregate ABI.
template<class Target, class Format>
struct striped_source_ops : byte_source_ops<Format> {
    using values = std::array<typename Target::vector, 16 / Target::lanes>;
    template<unsigned W, class Body, class Tail>
    [[gnu::always_inline]] auto read_payload(const comp::payload_expression<W, Body, Tail>& parts,
                                            original_rows rows, active16 active) const {
        const auto tail = this->read_tail(parts.tail, rows, active);
        if constexpr (W < 8) return tail;
        else {
            static_assert(Body::format_type::layout == Format::layout);
            const auto& plane = parts.body.source.placement().payload;
            constexpr auto T = Format::payload::tile_values;
            const auto* p = reinterpret_cast<const std::uint8_t*>(plane.bytes.data())
                          + (rows.origin / T) * plane.stride;
            const auto first = static_cast<unsigned>(rows.origin % T);
            values result;
            sp::detail::static_for<16 / Target::lanes>([&](auto part) {
                constexpr unsigned offset = part * Target::lanes;
                result[part] = Target::template join<W % 8>(
                    Target::template body<W / 8>(p + sp::detail::body_offset<W, sp::geometry::striped>(first + offset)),
                    Target::template expand<offset>(tail));
            });
            return result;
        }
    }
};

// Full-store admission only: the caller supplies Ops::active_all() and every
// stored row is selected and writable. A partial-mask driver needs another sink.
template<class Target>
struct full_tile_sink {
    U* output;
    template<unsigned Bits, class Rows, class Active>
    [[gnu::always_inline]] void store(typename Target::vector values, const Rows&, const Active&) const {
        static_assert(Bits <= 64);
        Target::store(output, values);
    }
};

template<class Target>
struct region_sink {
    U* output;
    template<unsigned Bits, class Values>
    [[gnu::always_inline]] void store(const Values& values, original_rows, active16) const {
        static_assert(Bits <= 64);
        sp::detail::static_for<16 / Target::lanes>([&](auto part) {
            if constexpr (Bits < 8)
                Target::store(output + part * Target::lanes, Target::template expand<part * Target::lanes>(values));
            else Target::store(output + part * Target::lanes, values[part]);
        });
    }
};

template<class Target, class Format, unsigned Count, unsigned OriginAlignment, class Expression, class Output>
[[gnu::always_inline]] void materialize_region(const Expression& expression,
                                               std::size_t origin, Output* __restrict out) {
    static_assert(Count == 16 && OriginAlignment == 16 && std::is_same_v<Output, U>);
    static_assert(Format::layout.head_bits == 0);
    static_assert(Expression::bit_width == Format::layout.width && Expression::head_bits == 0);
    __builtin_assume(origin % OriginAlignment == 0);
    if constexpr (Format::layout.storage == sp::geometry::local8 && Format::layout.width == 56) {
        constexpr unsigned N = Target::lanes;
        sp::detail::static_for<2>([&](auto tile) {
            sp::detail::static_for<8 / N>([&](auto part) {
                using Ops = typename Target::template ops<Format, part * N>;
                Ops ops;
                full_tile_sink<Target> sink{out + 8 * tile + N * part};
                comp::materialize(ops, expression, typename Target::rows{origin / 8 + tile}, ops.active_all(), sink);
            });
        });
    } else {
        striped_source_ops<Target, Format> ops;
        region_sink<Target> sink{out};
        comp::materialize(ops, expression, original_rows{origin}, active16{}, sink);
    }
}

template<class Target, class Format>
void admitted(const sp::const_view& source, std::size_t origin, U* __restrict out) {
    const auto expression = comp::describe<Format>(source);
    materialize_region<Target, Format, 16, 16>(expression, origin, out);
}

template<class Target, class Format>
void raw_dense(const std::uint8_t* __restrict source, std::size_t origin, U* __restrict out) {
    // The raw witness supplies only its admitted payload dependency, without
    // inventing a shorter logical view whose final slack might be nonzero.
    struct dense_source {
        sp::basic_placement<const std::byte> planes;
        const auto& placement() const { return planes; }
    };
    constexpr auto B = Format::payload::tile_bytes, T = Format::payload::tile_values;
    const dense_source ref{{{{reinterpret_cast<const std::byte*>(source), ((origin + 15) / T + 1) * B}, B}, {}}};
    const auto expression = comp::describe<Format>(ref);
    materialize_region<Target, Format, 16, 16>(expression, origin, out);
}

template<class Target, unsigned W, sp::geometry G>
materialized_region choose() {
    using F = sp::static_format<W, G>;
    return {admitted<Target, F>, raw_dense<Target, F>};
}

template<class Target>
materialized_region select(sp::description d) {
    if (d.storage == sp::geometry::local8) {
        if (d.width == 56) return choose<Target, 56, sp::geometry::local8>();
        if (d.width > 7) return {};
        return sp::detail::dispatch_group<7>(d.width - 1, [](auto index) {
            return choose<Target, index + 1, sp::geometry::local8>();
        });
    }
    switch (d.width) {
#define SERIESPACK_DIAGNOSTIC_STRIPE(W) case W: return choose<Target, W, sp::geometry::striped>();
        SERIESPACK_DIAGNOSTIC_STRIPE(1) SERIESPACK_DIAGNOSTIC_STRIPE(2)
        SERIESPACK_DIAGNOSTIC_STRIPE(3) SERIESPACK_DIAGNOSTIC_STRIPE(4)
        SERIESPACK_DIAGNOSTIC_STRIPE(5) SERIESPACK_DIAGNOSTIC_STRIPE(6)
        SERIESPACK_DIAGNOSTIC_STRIPE(7) SERIESPACK_DIAGNOSTIC_STRIPE(10)
        SERIESPACK_DIAGNOSTIC_STRIPE(12) SERIESPACK_DIAGNOSTIC_STRIPE(14)
        SERIESPACK_DIAGNOSTIC_STRIPE(15) SERIESPACK_DIAGNOSTIC_STRIPE(20)
#undef SERIESPACK_DIAGNOSTIC_STRIPE
    }
    return {};
}
#endif
} // namespace

materialized_region static_materialized16(sp::description d, sp::execution_target target) {
    if (!sp::validate(d) || d.head_bits != 0) return {};
#if defined(__aarch64__)
    if (target == sp::execution_target::neon) return select<neon_target>(d);
#endif
#if defined(__AVX2__)
    if (target == sp::execution_target::avx2) return select<avx2_target>(d);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    if (target == sp::execution_target::avx512) return select<avx512_target>(d);
#endif
#endif
    return {};
}
} // namespace seriespack_measurement
