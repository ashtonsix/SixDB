#pragma once
#include <ikea/effects.h>
#include <ikea/tuplepack/read.h>
#include <ikea/tuplepack/detail/mutation.h>
#include <bit>
#include <limits>

namespace ikea::tuplepack {
namespace detail {
/// Runs describe actual issued stores, not semantic bit ownership. Never emit
/// a zero length or shift by 64 when the whole unit is written.
template <class Plan, class Coverage>
[[gnu::always_inline]] inline void emit(const view& destination, std::size_t row, const Plan& plan,
                                        Coverage& coverage) noexcept {
    const auto base = destination.offset() + row * destination.stride();
    for (unsigned i = 0; i < plan.runs; ++i) {
        const auto span = plan.footprint[i];
        coverage.before(destination, byte_write{0, base + span.offset, span.size});
    }
}
template <unsigned Rows, class Plan, class Coverage>
[[gnu::always_inline]] inline void emit_window(const view& destination, std::size_t first,
                                               const Plan& p, Coverage& effects,
                                               std::uint64_t active) {
    if constexpr (Rows > 1) {
        // Full tight windows issue one contiguous span, even for 64 one-byte
        // tuples. Semantic maintenance still names each original row explicitly.
        if (active == all_rows<Rows> && p.runs == 1 &&
            p.footprint[0].size == destination.stride()) {
            const auto base =
                destination.offset() + first * destination.stride() + p.footprint[0].offset;
            effects.before(destination, byte_write{0, base, Rows * destination.stride()});
            return;
        }
        for (unsigned r = 0; r < Rows; ++r)
            if (active & (std::uint64_t(1) << r))
                emit(destination, first + r, p, effects);
    } else if (active)
        emit(destination, first, p, effects);
}
} // namespace detail

/// Borrowed whole mutation operation. Placement is admitted once at binding.
/// Calls do not acquire leases, allocate, suspend or publish. The owner keeps
/// input/selection/plans/effect storage disjoint from mutable outputs and secures
/// exclusion for issued bytes, including preserved neighboring bits.
template <unsigned N, unsigned Rows = 1>
class mutation_operation
    : public detail::mutation_commands<mutation_operation<N, Rows>, packet<N>, Rows> {
    const writer<N, Rows>* plan_;
    const view* destination_;
    mutation_operation(const writer<N, Rows>& plan, const view& destination)
        : plan_(&plan), destination_(&destination) {}

  public:
    static constexpr unsigned slots = N;
    using input_type = packet<N>;
    [[nodiscard]] static std::expected<mutation_operation, error> bind(const writer<N, Rows>& plan,
                                                                       const view& destination) {
        if (plan.unit_bytes() != destination.unit_bytes())
            return std::unexpected(error::description);
        return mutation_operation(plan, destination);
    }
    static auto bind(const writer<N, Rows>&&, const view&) = delete;
    static auto bind(const writer<N, Rows>&, const view&&) = delete;
    std::size_t size() const noexcept {
        return destination_->size();
    }
    const writer<N, Rows>& plan() const noexcept {
        return *plan_;
    }
    const view& destination() const noexcept {
        return *destination_;
    }
    template <class Visit> void visit_leaves(Visit&& visit) const {
        visit(*this);
    }
    /// Cold semantic destination walk for composition admission. Byte masks
    /// are separate from the wider issued-store/read envelope.
    template <class Visit> void visit_fields(Visit&& visit) const {
        const auto& p = plan_->controls();
        for (unsigned i = 0; i < p.count; ++i)
            visit(*destination_, p.stores[i].offset, p.stores[i].mask);
    }
    bool accepts(const input_type& input,
                 std::uint64_t active = detail::all_rows<Rows>) const noexcept {
        return plan_->accepts(input, active);
    }
    /// Conservative slots needed per selected row; a journal can coalesce them.
    unsigned effect_capacity() const noexcept {
        return plan_->effect_capacity();
    }
    /// Trusted local group: coverage capacity and input widths were admitted.
    /// Hooks must be no-fail/no-suspend and may only use pre-admitted resources.
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t first, const packet<N>& input,
                                              Coverage& effects,
                                              std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return;
        detail::emit_window<Rows>(*destination_, first, plan_->controls(), effects, active);
        plan_->set_unchecked(destination_->row_unchecked(first), destination_->stride(), input,
                             active);
    }
};
template <unsigned N, unsigned Rows>
auto bind_writer(const writer<N, Rows>& plan, const view& destination) {
    return mutation_operation<N, Rows>::bind(plan, destination);
}
template <unsigned N, unsigned Rows>
auto bind_writer(const writer<N, Rows>&&, const view&) = delete;
template <unsigned N, unsigned Rows>
auto bind_writer(const writer<N, Rows>&, const view&&) = delete;
} // namespace ikea::tuplepack
