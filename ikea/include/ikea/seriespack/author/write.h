#pragma once
#include <ikea/seriespack/detail/mutation/composed_construction.h>
#include <ikea/seriespack/selection.h>
#include <ikea/seriespack/detail/mutation/admission.h>
#include <ikea/seriespack/mutation_operation.h>

namespace ikea::seriespack::composition {
#if defined(__aarch64__) || defined(__AVX2__)
/// Owns the admitted expression tree, borrowing its named leaf views and bytes.
/// Moving this object invalidates erased operations made from its old address.
/// Binding may allocate temporary alias-analysis storage; execution does not.
template <class Expression> class prepared_mutation {
    Expression expression_;
    std::size_t count_;
    using physical_type =
        decltype(range_detail::physical_source(std::declval<const Expression&>()));
    physical_type physical_;
    prepared_mutation(Expression expression, std::size_t count)
        : expression_(std::move(expression)), count_(count),
          physical_(range_detail::physical_source(expression_)) {}

    template <class ValidValue>
    [[nodiscard]] std::expected<std::size_t, error> preflight(std::size_t first, std::size_t count,
                                                              row_selection selected,
                                                              ValidValue valid_value) const {
        if (first > count_ || count > count_ - first || !selected.covers(first, count))
            return std::unexpected(error::range);
        detail::count_writes needed;
        std::size_t offset = 0;
        while (offset < count) {
            const auto row = first + offset, origin = row - row % 16;
            const auto active = selected(origin);
            const auto n = std::min<std::size_t>(16 - row % 16, count - offset);
            for (std::size_t j = 0; j < n; ++j)
                if ((active & (1u << (row % 16 + j))) && !valid_value(offset + j))
                    return std::unexpected(error::value);
            if (row % 16 == 0 && n == 16) {
                if (active)
                    visit_assignment16(expression_, row, needed);
            } else
                for (std::size_t j = 0; j < n; ++j)
                    if (active & (1u << (row % 16 + j)))
                        visit_assignment_point(expression_, row + j, needed);
            offset += n;
        }
        return needed.count;
    }

  public:
    /// Cold admission reads placement metadata only. The conservative alias rule
    /// requires distinct writable leaves' whole physical fields to be disjoint;
    /// disjoint stride gaps can still interleave. No 2^16 container-size limit.
    [[nodiscard]] static std::expected<prepared_mutation, error>
    bind(Expression expression, std::size_t count, mutation_diagnostic* diagnostic = nullptr) {
        detail::write_admission admission{count};
        assign(admission, expression, 0, write_token{});
        if (diagnostic)
            *diagnostic = admission.diagnostic;
        if (admission.allocation_failed)
            return std::unexpected(error::allocation);
        if (!admission.valid)
            return std::unexpected(error::range);
        if (admission.aliased)
            return std::unexpected(error::overlap);
        return prepared_mutation{std::move(expression), count};
    }
    std::size_t size() const {
        return count_;
    }
    const Expression& expression() const {
        return expression_;
    }
    /// Cold upper bound on additional journal records for this original-row
    /// range. Reads placement and selection metadata only, never field data.
    /// Coalescing may reduce the actual count. Existing records need separate
    /// capacity. Invalid coordinates/selection return range without effects.
    [[nodiscard]] std::expected<std::size_t, error>
    effect_capacity(std::size_t first, std::size_t count, row_selection selected) const {
        return preflight(first, count, selected, [](std::size_t) { return true; });
    }
    /// Additional effect records needed to initialize owned fields and slack.
    /// Prefix-bound expressions cannot initialize larger borrowed leaf extents.
    [[nodiscard]] std::expected<std::size_t, error> construction_effect_capacity() const {
        construction_detail::extent_check extents{count_};
        assign(extents, expression_, 0, write_token{});
        if (count_ && !extents.valid)
            return std::unexpected(error::range);
        detail::count_writes needed;
        composition::visit_storage(expression_, count_, needed);
        return needed.count;
    }
    template <class U, class Summary> auto erase() const& {
        return mutation_operation<U, Summary>{
            this,
            count_,
            [](const void* pointer, std::size_t first, const U* input, std::uint16_t active,
               Summary& summary, write_journal& effects) {
                const auto& operation = *static_cast<const prepared_mutation*>(pointer);
                composition::replace16_unchecked(operation.expression_, first, input, active,
                                                 summary, effects);
            },
            [](const void* pointer, std::size_t first, std::size_t count, const U* input,
               row_selection selected, Summary& summary, write_journal& effects) {
                const auto& operation = *static_cast<const prepared_mutation*>(pointer);
                operation.replace_unchecked(first, count, input, selected, summary, effects);
            },
            [](const void* pointer, std::size_t first, std::span<const U> input,
               row_selection selected, Summary& summary, write_journal& effects) {
                const auto& operation = *static_cast<const prepared_mutation*>(pointer);
                return operation.replace(first, input, selected, summary, effects);
            },
            [](const void* pointer, std::size_t row, U value, Summary& summary,
               write_journal& effects) {
                const auto& operation = *static_cast<const prepared_mutation*>(pointer);
                return operation.set(row, value, summary, effects);
            },
            [](const void* pointer, std::span<const U> input, write_journal& effects) {
                const auto& operation = *static_cast<const prepared_mutation*>(pointer);
                return operation.initialize(input, effects);
            }};
    }
    template <class U, class Summary> auto erase() const&& = delete;
    /// Checked construction of every logical position. Exact leaf extents are
    /// required. On a returned error, bytes and effects remain unchanged. Input
    /// and effect storage must be disjoint from occupied destination fields.
    template <class U>
    [[nodiscard]] std::expected<void, error> initialize(std::span<const U> input,
                                                        write_journal& effects) const {
        if (input.size() != count_)
            return std::unexpected(error::range);
        if (effects.used > effects.storage.size())
            return std::unexpected(error::capacity);
        if (!count_)
            return {};
        const auto needed = construction_effect_capacity();
        if (!needed)
            return std::unexpected(needed.error());
        if constexpr (Expression::bit_width < sizeof(U) * 8)
            for (const auto value : input)
                if (value >> Expression::bit_width)
                    return std::unexpected(error::value);
        if (*needed > effects.storage.size() - effects.used)
            return std::unexpected(error::capacity);
        if constexpr (std::is_pointer_v<physical_type>)
            if (physical_) {
                range_detail::physical_coverage coverage{*physical_, effects};
                ikea::seriespack::initialize_unchecked(*physical_, input.data(), coverage);
                return {};
            }
        composition::initialize_unchecked(expression_, count_, input.data(), effects);
        return {};
    }
    template <class U, class Summary>
    void replace_unchecked(std::size_t first, std::size_t count, const U* input,
                           row_selection selected, Summary& summary, write_journal& effects) const {
        if (selected.is_none() || !count)
            return;
        auto run = [&](auto mask) {
            if constexpr (std::is_pointer_v<physical_type>)
                if (physical_) {
                    range_detail::physical_coverage coverage{*physical_, effects};
                    ikea::seriespack::replace_unchecked(*physical_, first, count, input, mask,
                                                         summary, coverage);
                    return;
                }
            composition::replace_unchecked(expression_, first, count, input, mask, summary,
                                           effects);
        };
        if (selected.is_all())
            run([](std::size_t) { return std::uint16_t{0xffff}; });
        else
            run(selected);
    }
    template <class U, class Summary>
    /// Checked original-row range, preserving unselected values. Returned
    /// errors leave bytes, summary and effects unchanged. All input slots in a
    /// nonempty interior 16-row region are readable; selected values fit width.
    [[nodiscard]] std::expected<void, error> replace(std::size_t first, std::span<const U> input,
                                                     row_selection selected, Summary& summary,
                                                     write_journal& effects) const {
        if (effects.used > effects.storage.size())
            return std::unexpected(error::capacity);
        const auto needed = preflight(first, input.size(), selected, [&](std::size_t offset) {
            if constexpr (Expression::bit_width < sizeof(U) * 8)
                return !(input[offset] >> Expression::bit_width);
            else
                return true;
        });
        if (!needed)
            return std::unexpected(needed.error());
        if (*needed > effects.storage.size() - effects.used)
            return std::unexpected(error::capacity);
        replace_unchecked(first, input.size(), input.data(), selected, summary, effects);
        return {};
    }
    template <class U, class Summary>
    [[nodiscard]] std::expected<void, error> set(std::size_t row, U value, Summary& summary,
                                                 write_journal& effects) const {
        if (row >= count_)
            return std::unexpected(error::range);
        if constexpr (Expression::bit_width < sizeof(U) * 8)
            if (value >> Expression::bit_width)
                return std::unexpected(error::value);
        detail::count_writes needed;
        visit_assignment_point(expression_, row, needed);
        if (effects.used > effects.storage.size() ||
            needed.count > effects.storage.size() - effects.used)
            return std::unexpected(error::capacity);
        composition::replace_point_unchecked(expression_, row, value, summary, effects);
        return {};
    }
    template <class U, class Summary>
    [[nodiscard]] std::expected<void, error> replace16(std::size_t first, std::span<const U> input,
                                                       std::uint16_t active, Summary& summary,
                                                       write_journal& effects) const {
        if (first % 16 || first > count_ || count_ - first < 16)
            return std::unexpected(error::range);
        if (input.size() < 16)
            return std::unexpected(error::capacity);
        if (!active)
            return {};
        if constexpr (Expression::bit_width < sizeof(U) * 8)
            for (unsigned j = 0; j < 16; ++j)
                if ((active & (1u << j)) && (input[j] >> Expression::bit_width))
                    return std::unexpected(error::value);
        detail::count_writes needed;
        visit_assignment16(expression_, first, needed);
        if (effects.used > effects.storage.size() ||
            needed.count > effects.storage.size() - effects.used)
            return std::unexpected(error::capacity);
        composition::replace16_unchecked(expression_, first, input.data(), active, summary,
                                         effects);
        return {};
    }
};
template <class E>
[[nodiscard]] auto prepare_mutation(E expression, std::size_t count,
                                    mutation_diagnostic* diagnostic = nullptr) {
    return prepared_mutation<E>::bind(std::move(expression), count, diagnostic);
}
#endif
} // namespace ikea::seriespack::composition
