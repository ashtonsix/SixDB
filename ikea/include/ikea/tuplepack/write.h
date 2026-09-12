#pragma once
#include <ikea/effects.h>
#include <ikea/tuplepack/read.h>
#include <bit>
#include <limits>

namespace ikea::tuplepack {
struct no_maintenance {
    struct state {};
    state before(std::size_t) const noexcept {
        return {};
    }
    void after(std::size_t, state) const noexcept {}
    bool covers(std::size_t, std::size_t) const noexcept {
        return true;
    }
};
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
} // namespace detail

/// Borrowed whole mutation operation. Placement is admitted once at binding.
/// Calls do not acquire leases, allocate, suspend or publish. The owner keeps
/// input/selection/plans/effect storage disjoint from mutable outputs and secures
/// exclusion for issued bytes, including preserved neighboring bits.
template <unsigned N> class mutation_operation {
    const writer<N>* plan_;
    const view* destination_;
    mutation_operation(const writer<N>& plan, const view& destination)
        : plan_(&plan), destination_(&destination) {}

  public:
    static constexpr unsigned slots = N;
    using input_type = packet<N>;
    [[nodiscard]] static std::expected<mutation_operation, error> bind(const writer<N>& plan,
                                                                       const view& destination) {
        if (plan.unit_bytes() != destination.unit_bytes())
            return std::unexpected(error::description);
        return mutation_operation(plan, destination);
    }
    static auto bind(const writer<N>&&, const view&) = delete;
    static auto bind(const writer<N>&, const view&&) = delete;
    std::size_t size() const noexcept {
        return destination_->size();
    }
    const writer<N>& plan() const noexcept {
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
    bool accepts(const input_type& input) const noexcept {
        return plan_->accepts(input);
    }
    /// Conservative slots needed per selected row; a journal can coalesce them.
    unsigned effect_capacity() const noexcept {
        return plan_->effect_capacity();
    }
    [[nodiscard]] std::expected<void, error> admit(std::size_t first,
                                                   std::span<const packet<N>> input,
                                                   selection selected, std::size_t capacity) const {
        if (first > size() || input.size() > size() - first ||
            !selected.covers(first, input.size()))
            return std::unexpected(error::range);
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (!selected.contains(first + i))
                continue;
            if (!plan_->accepts(input[i]))
                return std::unexpected(error::value);
            if (capacity < effect_capacity())
                return std::unexpected(error::capacity);
            capacity -= effect_capacity();
        }
        return {};
    }
    /// Trusted local group: coverage capacity and input widths were admitted.
    /// Hooks must be no-fail/no-suspend and may only use pre-admitted resources.
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t row, const packet<N>& input,
                                              Coverage& coverage) const {
        detail::emit(*destination_, row, plan_->controls(), coverage);
        plan_->set_unchecked(destination_->row_unchecked(row), input);
    }
    /// Checked point. Error leaves this call's bytes, effects and maintenance
    /// unchanged. Maintenance.after sees the complete update, never an input
    /// projection mistaken for the full row; maintenance owns its dependencies.
    template <class Maintenance = no_maintenance>
    [[nodiscard]] std::expected<void, error> set(std::size_t row, const packet<N>& input,
                                                 source_write_journal& effects,
                                                 Maintenance&& maintenance = {}) const {
        // Point calls deliberately do not enter the range traversal or create
        // a one-element span/selection frame. The concrete writer still owns
        // its specialized body; this shell only admits and brackets one row.
        if (row >= size() || !maintenance.covers(row, 1))
            return std::unexpected(error::range);
        if (!plan_->accepts(input))
            return std::unexpected(error::value);
        if (effects.used > effects.storage.size() || effects.remaining() < effect_capacity())
            return std::unexpected(error::capacity);
        auto before = maintenance.before(row);
        set_unchecked(row, input, effects);
        maintenance.after(row, std::move(before));
        return {};
    }
    template <class Maintenance = no_maintenance>
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const packet<N>> input, source_write_journal& effects,
            selection selected = selection::all(), Maintenance&& maintenance = {}) const {
        if (effects.used > effects.storage.size())
            return std::unexpected(error::capacity);
        if (auto valid = admit(first, input, selected, effects.remaining()); !valid)
            return valid;
        if (!maintenance.covers(first, input.size()))
            return std::unexpected(error::range);
        replace_unchecked(first, input, selected, effects, maintenance);
        return {};
    }
    template <class Coverage, class Maintenance = no_maintenance>
    void replace_unchecked(std::size_t first, std::span<const packet<N>> input, selection selected,
                           Coverage& effects, Maintenance&& maintenance = {}) const {
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto row = first + i;
            if (!selected.contains(row))
                continue;
            auto before = maintenance.before(row);
            set_unchecked(row, input[i], effects);
            maintenance.after(row, std::move(before));
        }
    }
};
template <unsigned N> auto bind_writer(const writer<N>& plan, const view& destination) {
    return mutation_operation<N>::bind(plan, destination);
}
template <unsigned N> auto bind_writer(const writer<N>&&, const view&) = delete;
template <unsigned N> auto bind_writer(const writer<N>&, const view&&) = delete;
} // namespace ikea::tuplepack
