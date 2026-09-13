#pragma once
#include <ikea/detail/native_chain.h>
#include <ikea/tuplepack/author/native.h>
#include <ikea/tuplepack/detail/native/packet.h>
#include <ikea/tuplepack/detail/native/selection.h>
#include <ikea/tuplepack/write.h>

namespace ikea::tuplepack {
/// Erases a complete range, retaining its concrete traversal and maintenance
/// type. The named source operation outlives this handle; no ownership transfers.
template <class Input, class Maintenance = no_maintenance, unsigned Rows = 1>
class erased_mutation {
    const void *operation_;
    std::size_t count_;
    using function = std::expected<void, error> (*)(const void *, std::size_t,
                                                    std::span<const Input>, source_write_journal &,
                                                    selection, Maintenance &);
    function call_;

  public:
    static constexpr unsigned rows = Rows;
    template <class Operation>
    explicit erased_mutation(const Operation &operation)
        : operation_(&operation), count_(operation.size()),
          call_([](const void *p, std::size_t first, std::span<const Input> input,
                   source_write_journal &effects, selection selected, Maintenance &maintenance) {
              return static_cast<const Operation *>(p)->replace(first, input, effects, selected,
                                                                maintenance);
          }) {
        static_assert(detail::operation_rows<Operation> == Rows,
                      "Erasure must preserve the original-row packet shape");
    }
    template <class Operation> erased_mutation(const Operation &&) = delete;
    std::size_t size() const noexcept { return count_; }
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const Input> input, source_write_journal &effects,
            selection selected, Maintenance &maintenance) const {
        return call_(operation_, first, input, effects, selected, maintenance);
    }
};

/// Native reader shares the exact decoded packet with inline authors. Unlike
/// buffered access, the 64-byte shape does not materialize an output array.
/// The 8-byte shape hands off a uint64_t, including on baseline x86.
template <unsigned N, class Byte, unsigned Rows = 1> class native_reader {
    const reader<N, Rows> *plan_;
    const basic_view<Byte> *source_;

  public:
    static constexpr unsigned rows = Rows;
    explicit native_reader(const read_operation<N, Byte, Rows> &operation)
        : plan_(&operation.plan()), source_(&operation.source()) {}
    std::size_t size() const noexcept { return source_->size(); }
    const auto &plan() const noexcept { return *plan_; }
    const auto &source() const noexcept { return *source_; }
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
        else {
            const auto base = source_->row_unchecked(first);
            const auto stride = source_->stride();
            return native::read_body<Rows>(plan_->controls(),
                [&](unsigned r) { return base + r * stride; }, active, stride);
        }
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
    std::size_t size() const noexcept { return operation_.size(); }
    const auto &plan() const noexcept { return operation_.plan(); }
    const auto &destination() const noexcept { return operation_.destination(); }
    auto effect_capacity() const noexcept { return operation_.effect_capacity(); }
    template <class Visit> void visit_leaves(Visit &&visit) const { visit(*this); }
    template <class Visit> void visit_fields(Visit &&visit) const {
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
                invalid = native::bit_and(invalid, native::row_mask(plan(), active));
            return !native::nonzero(invalid);
        }
#endif
    }
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t row, input_type input, Coverage &effects,
                                              std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return;
        detail::emit_window<Rows>(destination(), row, plan().controls(), effects, active);
        if constexpr (Rows == 1)
            native::write_body(plan().controls(), destination().row_unchecked(row), input);
        else {
            const auto base = destination().row_unchecked(row);
            const auto stride = destination().stride();
            native::write_body<Rows>(plan().controls(),
                [&](unsigned r) { return base + r * stride; }, input, active, stride);
        }
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
/// Extract the first eight packet bytes to a GPR. For equivalent operations,
/// bind both widths with the same map (at most 8/Rows slots) and groups: grouping
/// then gives identical positions at either width, including holes.
[[gnu::always_inline]] inline std::uint64_t compact_word(packet value) { return word<0>(value); }
/// Widen a GPR packet in registers; its eight bytes keep their positions and
/// bytes 8..63 are zero. The consumer owns the map/group interpretation.
[[gnu::always_inline]] inline packet expand_word(std::uint64_t value) {
    using namespace native_detail;
#if defined(__aarch64__)
    const auto first = vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(value), vdup_n_u64(0)));
#else
    const auto first = _mm_cvtsi64_si128(value);
#endif
    return join(first, zero16(), zero16(), zero16());
}
/// Pipeline ABI carrier: only homogeneous vectors, flattened at continuation
/// boundaries. Active rows/slots are a separate caller-interpreted mask.
struct pipeline_values {
#if defined(__aarch64__)
    using vector = uint8x16_t;
    static constexpr unsigned parts = 4;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) { return {{p.a, p.b, p.c, p.d}}; }
    packet get() const { return {v[0], v[1], v[2], v[3]}; }
#elif defined(__AVX512VBMI__)
    using vector = __m512i;
    static constexpr unsigned parts = 1;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) { return {{p}}; }
    packet get() const { return v[0]; }
#else
    using vector = __m256i;
    static constexpr unsigned parts = 2;
    std::array<vector, parts> v;
    static pipeline_values from(packet p) { return {{p.a, p.b}}; }
    packet get() const { return {v[0], v[1]}; }
#endif
};
#endif
/// Carrier for a single GPR at inline and continuation boundaries.
struct gpr_pipeline_values {
    using vector = std::uint64_t;
    static constexpr unsigned parts = 1;
    std::array<vector, parts> v;
    static gpr_pipeline_values from(std::uint64_t value) { return {{value}}; }
    std::uint64_t get() const { return v[0]; }
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
