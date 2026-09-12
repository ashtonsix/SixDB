#pragma once
#include <ikea/tuplepack/author/native.h>
#include <ikea/tuplepack/detail/native/packet.h>
#include <ikea/tuplepack/detail/native/selection.h>
#include <ikea/tuplepack/write.h>
#include <ikea/detail/native_chain.h>

namespace ikea::tuplepack {
/// Erases a complete range, retaining its concrete traversal and maintenance
/// type. The named source operation outlives this handle; no ownership transfers.
template <class Input, class Maintenance = no_maintenance, unsigned Rows = 1>
class erased_mutation {
    const void* operation_;
    std::size_t count_;
    using function = std::expected<void, error> (*)(const void*, std::size_t,
                                                    std::span<const Input>, source_write_journal&,
                                                    selection, Maintenance&);
    function call_;

  public:
    static constexpr unsigned rows = Rows;
    template <class Operation>
    explicit erased_mutation(const Operation& operation)
        : operation_(&operation), count_(operation.size()),
          call_([](const void* p, std::size_t first, std::span<const Input> input,
                   source_write_journal& effects, selection selected, Maintenance& maintenance) {
              return static_cast<const Operation*>(p)->replace(first, input, effects, selected,
                                                               maintenance);
          }) {
        static_assert(detail::operation_rows<Operation> == Rows,
                      "Erasure must preserve the original-row packet shape");
    }
    template <class Operation> erased_mutation(const Operation&&) = delete;
    std::size_t size() const noexcept {
        return count_;
    }
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const Input> input, source_write_journal& effects,
            selection selected, Maintenance& maintenance) const {
        return call_(operation_, first, input, effects, selected, maintenance);
    }
};

/// Native reader shares the exact decoded packet with inline authors. Unlike
/// buffered access, the 64-byte shape does not materialize an output array.
/// The 8-byte shape hands off a uint64_t, including on baseline x86.
template <unsigned N, class Byte, unsigned Rows = 1> class native_reader {
    const reader<N, Rows>* plan_;
    const basic_view<Byte>* source_;

  public:
    static constexpr unsigned rows = Rows;
    explicit native_reader(const read_operation<N, Byte, Rows>& operation)
        : plan_(&operation.plan()), source_(&operation.source()) {}
    std::size_t size() const noexcept {
        return source_->size();
    }
    const auto& plan() const noexcept {
        return *plan_;
    }
    const auto& source() const noexcept {
        return *source_;
    }
    [[nodiscard]] std::expected<void, error>
    admit(std::size_t first, std::uint64_t active = detail::all_rows<Rows>) const {
        if (!detail::valid_window<Rows>(size(), first, active))
            return std::unexpected(error::range);
        return {};
    }
    [[gnu::always_inline]] native::packet_for<N>
    get_unchecked(std::size_t first, std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return native::packet_for<N>{};
        if constexpr (Rows == 1)
            return native::read_body(plan_->controls(), source_->row_unchecked(first));
        else
            return native::read_body<Rows>(
                plan_->controls(), [&](unsigned r) { return source_->row_unchecked(first + r); },
                active, source_->stride());
    }
};
/// Register-valued mutation operation and composition leaf. The shared shell
/// admits widths/rows/effects, then native traversal handles the complete packet.
template <unsigned N = 64, unsigned Rows = 1>
class native_writer
    : public detail::mutation_commands<native_writer<N, Rows>, native::packet_for<N>, Rows> {
    mutation_operation<N, Rows> operation_;

  public:
    using input_type = native::packet_for<N>;
    explicit native_writer(mutation_operation<N, Rows> operation) : operation_(operation) {}
    std::size_t size() const noexcept {
        return operation_.size();
    }
    const auto& plan() const noexcept {
        return operation_.plan();
    }
    const auto& destination() const noexcept {
        return operation_.destination();
    }
    auto effect_capacity() const noexcept {
        return operation_.effect_capacity();
    }
    template <class Visit> void visit_leaves(Visit&& visit) const {
        visit(*this);
    }
    template <class Visit> void visit_fields(Visit&& visit) const {
        operation_.visit_fields(visit);
    }
    [[gnu::always_inline]] bool
    accepts(input_type input, std::uint64_t active = detail::all_rows<Rows>) const noexcept {
        if (!active)
            return true;
        if constexpr (N == 8)
            return operation_.accepts(input, active);
#if defined(__aarch64__) || defined(__AVX2__)
        else {
            auto invalid =
                native::bit_and(input, native::load_packet(plan().controls().invalid.data()));
            if (active != detail::all_rows<Rows>)
                invalid = native::bit_and(invalid, native::row_mask<Rows>(active));
            return !native::nonzero(invalid);
        }
#endif
    }
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t row, input_type input, Coverage& effects,
                                              std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return;
        detail::emit_window<Rows>(destination(), row, plan().controls(), effects, active);
        if constexpr (Rows == 1)
            native::write_body(plan().controls(), destination().row_unchecked(row), input);
        else
            native::write_body<Rows>(
                plan().controls(), [&](unsigned r) { return destination().row_unchecked(row + r); },
                input, active, destination().stride());
    }
};
namespace native {
template <unsigned I> [[gnu::always_inline]] inline std::uint64_t word(std::uint64_t value) {
    static_assert(I == 0);
    return value;
}
#if defined(__aarch64__) || defined(__AVX2__)
/// Compile-time extraction to a GPR, for caller-owned struct assembly. Byte
/// semantics and any wider signed/float reinterpretation remain with that caller.
template <unsigned I> [[gnu::always_inline]] inline std::uint64_t word(packet value) {
    static_assert(I < 8);
    const auto part = native_detail::split<I / 2>(value);
#if defined(__aarch64__)
    return vgetq_lane_u64(vreinterpretq_u64_u8(part), I % 2);
#else
    return std::uint64_t(_mm_extract_epi64(part, I % 2));
#endif
}
/// Compact the first 8/Rows bytes of each row in a 64-byte native packet into
/// a row-major GPR. Rows is 1/2/4/8. Later slots are discarded, not interpreted.
template <unsigned Rows> [[gnu::always_inline]] inline std::uint64_t compact_word(packet value) {
    static_assert(Rows <= 8 && std::has_single_bit(Rows));
    constexpr unsigned B = 8 / Rows;
    constexpr auto mask = ~std::uint64_t(0) >> (64 - 8 * B);
    return [&]<std::size_t... R>(std::index_sequence<R...>) __attribute__((always_inline)) {
        return (((word<R * B>(value) & mask) << (R * B * 8)) | ...);
    }(std::make_index_sequence<Rows>{});
}
namespace native_detail {
template <unsigned Rows, unsigned I>
[[gnu::always_inline]] inline std::uint64_t expanded_word(std::uint64_t value) {
    constexpr unsigned B = 8 / Rows;
    if constexpr (I % B == 0)
        return (value >> (I * 8)) & (~std::uint64_t(0) >> (64 - 8 * B));
    else
        return 0;
}
template <unsigned Rows, unsigned I>
[[gnu::always_inline]] inline vector16 expanded_part(std::uint64_t value) {
    const auto lo = expanded_word<Rows, I * 2>(value);
    const auto hi = expanded_word<Rows, I * 2 + 1>(value);
#if defined(__aarch64__)
    return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(lo), vcreate_u64(hi)));
#else
    return _mm_set_epi64x(hi, lo);
#endif
}
} // namespace native_detail
/// Put a GPR's 8/Rows bytes per row into the corresponding 64-byte packet slots;
/// zero all other slots. For equivalent mutation use the same map (at most
/// 8/Rows entries) on both writers: extra mapped destinations would receive zero.
template <unsigned Rows> [[gnu::always_inline]] inline packet expand_word(std::uint64_t value) {
    static_assert(Rows <= 8 && std::has_single_bit(Rows));
    using native_detail::expanded_part;
    return native_detail::join(expanded_part<Rows, 0>(value), expanded_part<Rows, 1>(value),
                               expanded_part<Rows, 2>(value), expanded_part<Rows, 3>(value));
}
/// Pipeline ABI carrier: only homogeneous vectors, flattened at continuation
/// boundaries. Active rows/slots are a separate caller-interpreted mask.
struct pipeline_values {
#if defined(__aarch64__)
    using vector = uint8x16_t;
    static constexpr unsigned parts = 4;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) {
        return {{p.a, p.b, p.c, p.d}};
    }
    packet get() const {
        return {v[0], v[1], v[2], v[3]};
    }
#elif defined(__AVX512VBMI__)
    using vector = __m512i;
    static constexpr unsigned parts = 1;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) {
        return {{p}};
    }
    packet get() const {
        return v[0];
    }
#else
    using vector = __m256i;
    static constexpr unsigned parts = 2;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) {
        return {{p.a, p.b}};
    }
    packet get() const {
        return {v[0], v[1]};
    }
#endif
};
#endif
/// Carrier for a single GPR at inline and continuation boundaries.
struct gpr_pipeline_values {
    using vector = std::uint64_t;
    static constexpr unsigned parts = 1;
    std::array<vector, parts> v;
    static gpr_pipeline_values from(std::uint64_t value) {
        return {{value}};
    }
    std::uint64_t get() const {
        return v[0];
    }
};
template <unsigned N> struct pipeline_carrier;
template <> struct pipeline_carrier<8> {
    using type = gpr_pipeline_values;
};
#if defined(__aarch64__) || defined(__AVX2__)
template <> struct pipeline_carrier<64> {
    using type = pipeline_values;
};
#endif
template <unsigned N> using pipeline_values_for = typename pipeline_carrier<N>::type;
} // namespace native
/// Bounded straight-through CPS with early completion, sharing ordinary inline
/// bodies. Owner suspension is after completion, never with vector state live
/// in an arbitrary stage. The caller chooses mask meaning and stage granularity.
#if defined(__aarch64__) || defined(__x86_64__)
template <unsigned N, unsigned Slots = 8, class Mask = std::uint64_t>
using packet_chain =
    ikea::detail::native_chain<native::pipeline_values_for<N>, Mask, Slots, error,
                               std::make_index_sequence<native::pipeline_values_for<N>::parts>>;
#if defined(__aarch64__) || defined(__AVX2__)
template <unsigned Slots = 8, class Mask = std::uint64_t>
using chain = packet_chain<64, Slots, Mask>;
#endif
#endif
} // namespace ikea::tuplepack
