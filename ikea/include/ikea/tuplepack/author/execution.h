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

#if defined(__aarch64__) || defined(__AVX2__)
/// Native reader shares the exact decoded packet with inline authors. Unlike
/// buffered access it does not materialize a 64-byte output array at this seam.
template <class Byte, unsigned Rows = 1> class native_reader {
    const reader<64, Rows>* plan_;
    const basic_view<Byte>* source_;

  public:
    static constexpr unsigned rows = Rows;
    explicit native_reader(const read_operation<64, Byte, Rows>& operation)
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
    [[gnu::always_inline]] native::packet
    get_unchecked(std::size_t first, std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return native::native_detail::join(
                native::native_detail::zero16(), native::native_detail::zero16(),
                native::native_detail::zero16(), native::native_detail::zero16());
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
template <unsigned Rows = 1>
class native_writer : public detail::mutation_commands<native_writer<Rows>, native::packet, Rows> {
    mutation_operation<64, Rows> operation_;

  public:
    using input_type = native::packet;
    explicit native_writer(mutation_operation<64, Rows> operation) : operation_(operation) {}
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
        auto invalid =
            native::bit_and(input, native::load_packet(plan().controls().invalid.data()));
        if (active != detail::all_rows<Rows>)
            invalid = native::bit_and(invalid, native::row_mask<Rows>(active));
        return !native::nonzero(invalid);
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
} // namespace native
/// Bounded straight-through CPS with early completion, sharing ordinary inline
/// bodies. Owner suspension is after completion, never with vector state live
/// in an arbitrary stage. The caller chooses mask meaning and stage granularity.
template <unsigned Slots = 8, class Mask = std::uint64_t>
using chain = ikea::detail::native_chain<native::pipeline_values, Mask, Slots, error,
                                         std::make_index_sequence<native::pipeline_values::parts>>;
#endif
} // namespace ikea::tuplepack
