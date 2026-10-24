#pragma once
#include <array>
#include <ikea/seriespack/detail/mutation/composed_construction.h>
#include <ikea/seriespack/selection.h>
#include <ikea/seriespack/detail/mutation/admission.h>
#include <ikea/seriespack/erased_writer.h>

namespace ikea::seriespack {
/// Owns the admitted expression tree, borrowing its named leaf views and bytes.
/// Moving this object invalidates erased operations made from its old address.
/// A whole admitted view reuses its placement proof without allocation. Other
/// expressions may allocate temporary alias-analysis storage; execution does not.
template <class Expression> class writer {
    Expression expression_;
    std::size_t count_;
    using physical_type =
        decltype(author::range_detail::physical_source(std::declval<const Expression&>()));
    physical_type physical_;
    writer(Expression expression, std::size_t count)
        : expression_(std::move(expression)), count_(count),
          physical_(author::range_detail::physical_source(expression_)) {}

    template <class ValidValue>
    [[nodiscard]] std::expected<std::size_t, error> preflight(std::size_t first, std::size_t count,
                                                              selection selected,
                                                              ValidValue valid_value) const {
        if (first > count_ || count > count_ - first || !selected.covers(first, count))
            return std::unexpected(error::range);
        author::detail::count_writes needed;
        std::size_t offset = 0;
        const auto regions = selected.region_words();
        while (offset < count) {
            const auto row = first + offset, origin = row - row % 16;
            const auto active = regions(origin);
            const auto n = std::min<std::size_t>(16 - row % 16, count - offset);
            for (std::size_t j = 0; j < n; ++j)
                if ((active & (1u << (row % 16 + j))) && !valid_value(offset + j))
                    return std::unexpected(error::value);
            if (row % 16 == 0 && n == 16) {
                if (active)
                    author::visit_assignment16(expression_, row, needed);
            } else
                for (std::size_t j = 0; j < n; ++j)
                    if (active & (1u << (row % 16 + j)))
                        author::visit_assignment_point(expression_, row + j, needed);
            offset += n;
        }
        return needed.count;
    }

  public:
    /// Cold admission reads placement metadata only. The conservative alias rule
    /// requires distinct writable leaves' whole physical fields to be disjoint;
    /// disjoint stride gaps can still interleave. No 2^16 container-size limit.
    [[nodiscard]] static std::expected<writer, error>
    bind(Expression expression, std::size_t count, mutation_diagnostic* diagnostic = nullptr) {
        if constexpr (std::is_pointer_v<physical_type>) {
            using Source = std::remove_cv_t<std::remove_pointer_t<physical_type>>;
            if constexpr (std::is_same_v<Source, basic_view<typename Source::layout_type, ikea::byte>>)
                if (const auto* source = author::range_detail::physical_source(expression)) {
                    // A whole admitted view already proves its fields disjoint.
                    // Substituted views still need the cross-source walk below.
                    if (diagnostic)
                        *diagnostic = count > source->size() ? mutation_diagnostic{source}
                                                           : mutation_diagnostic{};
                    if (count > source->size())
                        return std::unexpected(error::range);
                    return writer{std::move(expression), count};
                }
        }
        author::detail::write_admission admission{count};
        author::assign(admission, expression, 0, author::write_token{});
        if (diagnostic)
            *diagnostic = admission.diagnostic;
        if (admission.allocation_failed)
            return std::unexpected(error::allocation);
        if (!admission.valid)
            return std::unexpected(error::range);
        if (admission.aliased)
            return std::unexpected(error::overlap);
        return writer{std::move(expression), count};
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
    effect_capacity(std::size_t first, std::size_t count, selection selected) const {
        return preflight(first, count, selected, [](std::size_t) { return true; });
    }
    /// Additional effect records needed to initialize owned fields and slack.
    /// Prefix-bound expressions cannot initialize larger borrowed leaf extents.
    [[nodiscard]] std::expected<std::size_t, error> effect_capacity_initialize() const {
        author::construction_detail::extent_check extents{count_};
        author::assign(extents, expression_, 0, author::write_token{});
        if (count_ && !extents.valid)
            return std::unexpected(error::range);
        author::detail::count_writes needed;
        author::visit_storage(expression_, count_, needed);
        return needed.count;
    }
    template <class U, class Summary = author::no_summary> auto erase() const& {
        return erased_writer<U, Summary>{
            this,
            count_,
            [](const void* pointer, std::size_t first, const U* input, journal& effects,
               std::uint16_t active, Summary& summary) {
                const auto& operation = *static_cast<const writer*>(pointer);
                author::replace16_unchecked(operation.expression_, first, input, active,
                                                 summary, effects);
            },
            [](const void* pointer, std::size_t first, std::size_t count, const U* input,
               journal& effects, selection selected, Summary& summary) {
                const auto& operation = *static_cast<const writer*>(pointer);
                operation.replace_unchecked(first, count, input, effects, selected, summary);
            },
            [](const void* pointer, std::size_t first, std::span<const U> input,
               journal& effects, selection selected, Summary& summary) {
                const auto& operation = *static_cast<const writer*>(pointer);
                return operation.replace(first, input, effects, selected, summary);
            },
            [](const void* pointer, std::size_t row, U value, journal& effects,
               Summary& summary) {
                const auto& operation = *static_cast<const writer*>(pointer);
                return operation.set(row, value, effects, summary);
            },
            [](const void* pointer, std::span<const U> input, journal& effects) {
                const auto& operation = *static_cast<const writer*>(pointer);
                return operation.initialize(input, effects);
            }};
    }
    template <class U, class Summary = author::no_summary> auto erase() const&& = delete;
    /// Checked construction of every logical position. Exact leaf extents are
    /// required. On a returned error, bytes and effects remain unchanged. Input
    /// and effect storage must be disjoint from occupied destination fields.
    template <class U>
    [[nodiscard]] std::expected<void, error> initialize(std::span<const U> input,
                                                        journal& effects) const {
        if (input.size() != count_)
            return std::unexpected(error::range);
        if (effects.used > effects.storage.size())
            return std::unexpected(error::effects);
        if (!count_)
            return {};
        const auto needed = effect_capacity_initialize();
        if (!needed)
            return std::unexpected(needed.error());
        if constexpr (Expression::bit_width < sizeof(U) * 8)
            for (const auto value : input)
                if (value >> Expression::bit_width)
                    return std::unexpected(error::value);
        if (*needed > effects.storage.size() - effects.used)
            return std::unexpected(error::effects);
        if constexpr (std::is_pointer_v<physical_type>)
            if (physical_) {
                ikea::seriespack::author::initialize_unchecked(*physical_, input.data(), effects);
                return {};
            }
        author::initialize_unchecked(expression_, count_, input.data(), effects);
        return {};
    }
    template <class U, class Summary>
    void replace_unchecked(std::size_t first, std::size_t count, const U* input,
                           journal& effects, selection selected, Summary& summary) const {
        if (selected.is_none() || !count)
            return;
        auto run = [&](auto mask) {
            if constexpr (std::is_pointer_v<physical_type>)
                if (physical_) {
                    ikea::seriespack::author::replace_unchecked(*physical_, first, count, input, mask,
                                                         summary, effects);
                    return;
                }
            author::replace_unchecked(expression_, first, count, input, mask, summary,
                                           effects);
        };
        if (selected.is_all())
            run([](std::size_t) { return std::uint16_t{0xffff}; });
        else
            run([regions = selected.region_words()](std::size_t origin) { return regions(origin); });
    }
    template <class U, class Summary>
    /// Checked original-row range, preserving unselected values. Returned
    /// errors leave bytes, summary and effects unchanged. All input slots in a
    /// nonempty interior 16-row region are readable; selected values fit width.
    [[nodiscard]] std::expected<void, error> replace(std::size_t first, std::span<const U> input,
                                                     journal& effects, selection selected,
                                                     Summary& summary) const {
        if (effects.used > effects.storage.size())
            return std::unexpected(error::effects);
        const auto needed = preflight(first, input.size(), selected, [&](std::size_t offset) {
            if constexpr (Expression::bit_width < sizeof(U) * 8)
                return !(input[offset] >> Expression::bit_width);
            else
                return true;
        });
        if (!needed)
            return std::unexpected(needed.error());
        if (*needed > effects.storage.size() - effects.used)
            return std::unexpected(error::effects);
        replace_unchecked(first, input.size(), input.data(), effects, selected, summary);
        return {};
    }
    template <class U, class Summary>
    [[nodiscard]] std::expected<void, error> set(std::size_t row, U value, journal& effects,
                                                 Summary& summary) const {
        if (row >= count_)
            return std::unexpected(error::range);
        if constexpr (Expression::bit_width < sizeof(U) * 8)
            if (value >> Expression::bit_width)
                return std::unexpected(error::value);
        author::detail::count_writes needed;
        author::visit_assignment_point(expression_, row, needed);
        if (effects.used > effects.storage.size() ||
            needed.count > effects.storage.size() - effects.used)
            return std::unexpected(error::effects);
        author::replace_point_unchecked(expression_, row, value, summary, effects);
        return {};
    }
    template <class U, class Summary>
    [[nodiscard]] std::expected<void, error> replace16(std::size_t first, std::span<const U> input,
                                                       journal& effects, std::uint16_t active,
                                                       Summary& summary) const {
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
        author::detail::count_writes needed;
        author::visit_assignment16(expression_, first, needed);
        if (effects.used > effects.storage.size() ||
            needed.count > effects.storage.size() - effects.used)
            return std::unexpected(error::effects);
        author::replace16_unchecked(expression_, first, input.data(), active, summary,
                                         effects);
        return {};
    }
    /// Omitting a summary uses a stateless recipe; stateful summaries remain
    /// explicit lvalue resources. Selection defaults to every original row.
    template <class U>
    void replace_unchecked(std::size_t first, std::size_t count, const U* input,
                           journal& effects, selection selected = selection::all()) const {
        author::no_summary summary;
        replace_unchecked(first, count, input, effects, selected, summary);
    }
    template <class U>
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const U> input, journal& effects,
            selection selected = selection::all()) const {
        author::no_summary summary;
        return replace(first, input, effects, selected, summary);
    }
    template <class U>
    [[nodiscard]] std::expected<void, error> set(std::size_t row, U value,
                                                  journal& effects) const {
        author::no_summary summary;
        return set(row, value, effects, summary);
    }
    template <class U>
    [[nodiscard]] std::expected<void, error>
    replace16(std::size_t first, std::span<const U> input, journal& effects,
              std::uint16_t active = 0xffff) const {
        author::no_summary summary;
        return replace16(first, input, effects, active, summary);
    }

};
template <class E>
[[nodiscard]] auto bind_writer(E expression, std::size_t count,
                                    mutation_diagnostic* diagnostic = nullptr) {
    return writer<E>::bind(std::move(expression), count, diagnostic);
}

/// Runtime-width dense headless destination, the writable peer of dense_input.
/// It is also the effect source for construction through it: records name this
/// object and its single plane, so keep it named until they are resolved.
struct dense_output {
    unsigned width;
    geometry storage;
    ikea::mutable_bytes bytes;
    std::size_t count;
    std::span<const ikea::byte> plane_storage(unsigned) const noexcept { return bytes; }
};
/// Additional records construction through target needs, or the placement error
/// that would refuse it. Reads only the description; no bytes are touched.
[[nodiscard]] std::expected<std::size_t, error> effect_capacity_initialize(const dense_output& target);
/// Compiled complete construction for a runtime width: the same checks, wire
/// and effects as attaching a view, binding a writer and calling initialize.
/// Exactly target.count inputs; failure leaves bytes and effects unchanged.
template <class U>
[[nodiscard]] std::expected<void, error> initialize(const dense_output& target,
                                                    std::span<const U> input, journal& effects);
extern template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint8_t>, journal&);
extern template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint16_t>, journal&);
extern template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint32_t>, journal&);
extern template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint64_t>, journal&);
/// Compiled point replacement for a runtime width: the same checks, store and
/// effects as a bound writer's set, with records naming target. Range, value
/// and capacity errors leave bytes and effects unchanged.
template <class U>
[[nodiscard]] std::expected<void, error> set(const dense_output& target, std::size_t row, U value, journal& effects);
extern template std::expected<void, error> set(const dense_output&, std::size_t, std::uint8_t, journal&);
extern template std::expected<void, error> set(const dense_output&, std::size_t, std::uint16_t, journal&);
extern template std::expected<void, error> set(const dense_output&, std::size_t, std::uint32_t, journal&);
extern template std::expected<void, error> set(const dense_output&, std::size_t, std::uint64_t, journal&);
/// The bytes of target.bytes a point replacement at row stores (stores,
/// one record each) and the bytes its value occupies (reads), as offsets.
/// A point touches at most a body span and three residual fragments.
struct point_footprint {
    struct span {
        std::size_t offset, size;
    };
    std::array<span, 4> stores, reads;
    unsigned store_count = 0, read_count = 0;
};
[[nodiscard]] std::expected<point_footprint, error> footprint(const dense_output& target, std::size_t row);
} // namespace ikea::seriespack
