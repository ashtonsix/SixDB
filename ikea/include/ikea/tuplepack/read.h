#pragma once
#include <ikea/tuplepack/plan.h>
#include <ikea/tuplepack/view.h>
#include <ikea/tuplepack/selection.h>

namespace ikea::tuplepack {
/// Borrows a named plan and view, both of which must remain at stable addresses.
/// Output/plan/selection metadata must be disjoint from mutable source storage.
template <unsigned N, class Byte> class read_operation {
    const reader<N>* plan_;
    const basic_view<Byte>* source_;
    read_operation(const reader<N>& plan, const basic_view<Byte>& source)
        : plan_(&plan), source_(&source) {}

  public:
    [[nodiscard]] static std::expected<read_operation, error> bind(const reader<N>& plan,
                                                                   const basic_view<Byte>& source) {
        if (plan.unit_bytes() != source.unit_bytes())
            return std::unexpected(error::description);
        return read_operation(plan, source);
    }
    static auto bind(const reader<N>&&, const basic_view<Byte>&) = delete;
    static auto bind(const reader<N>&, const basic_view<Byte>&&) = delete;
    std::size_t size() const noexcept {
        return source_->size();
    }
    [[nodiscard]] std::expected<packet<N>, error> get(std::size_t row) const {
        if (row >= size())
            return std::unexpected(error::range);
        return get_unchecked(row);
    }
    packet<N> get_unchecked(std::size_t row) const {
        return plan_->get_unchecked(source_->row_unchecked(row));
    }
    /// Failure leaves output unchanged. Inactive positions are zero and issue
    /// no payload reads; zero data is not an absence/evidence encoding.
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<packet<N>> output,
                                                  selection selected = selection::all()) const {
        if (first > size() || output.size() > size() - first ||
            !selected.covers(first, output.size()))
            return std::unexpected(error::range);
        read_unchecked(first, output, selected);
        return {};
    }
    void read_unchecked(std::size_t first, std::span<packet<N>> output,
                        selection selected = selection::all()) const {
        for (std::size_t i = 0; i < output.size(); ++i)
            output[i] = selected.contains(first + i) ? get_unchecked(first + i) : packet<N>{};
    }
    const basic_view<Byte>& source() const noexcept {
        return *source_;
    }
    const reader<N>& plan() const noexcept {
        return *plan_;
    }
};
template <unsigned N, class Byte>
auto bind_reader(const reader<N>& plan, const basic_view<Byte>& source) {
    return read_operation<N, Byte>::bind(plan, source);
}
template <unsigned N, class Byte>
auto bind_reader(const reader<N>&&, const basic_view<Byte>&) = delete;
template <unsigned N, class Byte>
auto bind_reader(const reader<N>&, const basic_view<Byte>&&) = delete;
} // namespace ikea::tuplepack
