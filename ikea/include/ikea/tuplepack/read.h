#pragma once
#include <ikea/tuplepack/plan.h>
#include <ikea/tuplepack/view.h>
#include <ikea/tuplepack/selection.h>

namespace ikea::tuplepack {
/// Borrows a named plan and view, both of which must remain at stable addresses.
/// Output/plan/selection metadata must be disjoint from mutable source storage.
template <unsigned N, class Byte, unsigned Rows = 1> class read_operation {
    const reader<N, Rows>* plan_;
    const basic_view<Byte>* source_;
    read_operation(const reader<N, Rows>& plan, const basic_view<Byte>& source)
        : plan_(&plan), source_(&source) {}

  public:
    static constexpr unsigned rows = Rows;
    [[nodiscard]] static std::expected<read_operation, error> bind(const reader<N, Rows>& plan,
                                                                   const basic_view<Byte>& source) {
        if (plan.unit_bytes() != source.unit_bytes())
            return std::unexpected(error::description);
        return read_operation(plan, source);
    }
    static auto bind(const reader<N, Rows>&&, const basic_view<Byte>&) = delete;
    static auto bind(const reader<N, Rows>&, const basic_view<Byte>&&) = delete;
    std::size_t size() const noexcept {
        return source_->size();
    }
    /// A packet begins at original row first; bit r selects first+r. Only
    /// active rows require storage. Inactive rows and unused map slots are zero.
    [[nodiscard]] std::expected<packet<N>, error>
    get(std::size_t first, std::uint64_t active = detail::all_rows<Rows>) const {
        if (!detail::valid_window<Rows>(size(), first, active))
            return std::unexpected(error::range);
        return get_unchecked(first, active);
    }
    packet<N> get_unchecked(std::size_t first,
                            std::uint64_t active = detail::all_rows<Rows>) const {
        if (!active)
            return {};
        return plan_->get_unchecked(source_->row_unchecked(first), source_->stride(), active);
    }
    /// Packet i starts at first+i*Rows. The last packet may cover a short tail;
    /// selection uses original rows. Failure leaves every output packet unchanged.
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<packet<N>> output,
                                                  selection selected = selection::all()) const {
        const auto count = detail::range_rows<Rows>(size(), first, output.size());
        if (!count || !selected.covers(first, *count))
            return std::unexpected(error::range);
        read_unchecked(first, output, selected);
        return {};
    }
    void read_unchecked(std::size_t first, std::span<packet<N>> output,
                        selection selected = selection::all()) const {
        const auto count = *detail::range_rows<Rows>(size(), first, output.size());
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto row = first + i * Rows;
            output[i] =
                get_unchecked(row, detail::selected_window<Rows>(selected, row, count - i * Rows));
        }
    }
    const basic_view<Byte>& source() const noexcept {
        return *source_;
    }
    const reader<N, Rows>& plan() const noexcept {
        return *plan_;
    }
};
template <unsigned N, class Byte, unsigned Rows>
auto bind_reader(const reader<N, Rows>& plan, const basic_view<Byte>& source) {
    return read_operation<N, Byte, Rows>::bind(plan, source);
}
template <unsigned N, class Byte, unsigned Rows>
auto bind_reader(const reader<N, Rows>&&, const basic_view<Byte>&) = delete;
template <unsigned N, class Byte, unsigned Rows>
auto bind_reader(const reader<N, Rows>&, const basic_view<Byte>&&) = delete;
} // namespace ikea::tuplepack
