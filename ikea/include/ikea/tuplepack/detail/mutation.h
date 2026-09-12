#pragma once
#include <ikea/effects.h>
#include <ikea/tuplepack/description.h>
#include <ikea/tuplepack/detail/window.h>
#include <type_traits>

namespace ikea::tuplepack {
struct no_maintenance {
    struct state {};
    state before(std::size_t) const noexcept {
        return {};
    }
    void after(std::size_t, state) const noexcept {}
    state before_batch(std::size_t, std::uint64_t) const noexcept {
        return {};
    }
    void after_batch(std::size_t, std::uint64_t, state) const noexcept {}
    bool covers(std::size_t, std::size_t) const noexcept {
        return true;
    }
};
namespace detail {
template <class Operation>
inline constexpr unsigned operation_rows = [] {
    if constexpr (requires { Operation::rows; })
        return Operation::rows;
    else
        return 1u;
}();
template <class... Operation>
inline constexpr unsigned common_rows = [] {
    unsigned result = 1;
    ((result = operation_rows<Operation>), ...);
    return result;
}();
template <class Operation, class Input>
[[gnu::always_inline]] inline bool accepts(const Operation& operation, const Input& input,
                                           std::uint64_t active) {
    if constexpr (requires { operation.accepts(input, active); })
        return operation.accepts(input, active);
    else
        return !active || operation.accepts(input);
}
template <class Operation, class Input, class Coverage>
[[gnu::always_inline]] inline void assign(const Operation& operation, std::size_t first,
                                          const Input& input, Coverage& effects,
                                          std::uint64_t active) {
    if constexpr (requires { operation.set_unchecked(first, input, effects, active); })
        operation.set_unchecked(first, input, effects, active);
    else if (active)
        operation.set_unchecked(first, input, effects);
}

/// Shared admission/maintenance shell. The derived operation owns its complete
/// specialized packet traversal; neither rows nor child payloads are erased here.
/// Effect capacity is per active original row. Hooks cannot fail or suspend.
/// The mutation callback itself must inline: inlining maintain() alone does not
/// stop Clang from outlining its callable and passing a native payload by address.
template <class Derived, class Input, unsigned Rows> class mutation_commands {
    const Derived& operation() const {
        return static_cast<const Derived&>(*this);
    }
    bool reserve(std::size_t& capacity, std::uint64_t active) const {
        const auto per_row = operation().effect_capacity();
        const unsigned count = std::popcount(active);
        if (per_row && count > capacity / per_row)
            return false;
        capacity -= count * per_row;
        return true;
    }

  public:
    using input_type = Input;
    static constexpr unsigned rows = Rows;
    [[nodiscard]] std::expected<void, error> admit(std::size_t first, std::span<const Input> input,
                                                   selection selected, std::size_t capacity) const {
        const auto count = range_rows<Rows>(operation().size(), first, input.size());
        if (!count || !selected.covers(first, *count))
            return std::unexpected(error::range);
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto active =
                selected_window<Rows>(selected, first + i * Rows, *count - i * Rows);
            if (!detail::accepts(operation(), input[i], active))
                return std::unexpected(error::value);
            if (!reserve(capacity, active))
                return std::unexpected(error::capacity);
        }
        return {};
    }
    /// Checks all active rows before changing data, effects or maintenance.
    /// Bit r names first+r; inactive input slots need no valid code values.
    template <class Maintenance = no_maintenance>
    [[nodiscard, gnu::always_inline]] std::expected<void, error>
    set(std::size_t first, const Input& input, source_write_journal& effects, std::uint64_t active,
        Maintenance&& maintenance = {}) const {
        if (!valid_window<Rows>(operation().size(), first, active) ||
            !maintenance.covers(first, std::bit_width(active)))
            return std::unexpected(error::range);
        if (!detail::accepts(operation(), input, active))
            return std::unexpected(error::value);
        auto capacity = effects.remaining();
        if (effects.used > effects.storage.size() || !reserve(capacity, active))
            return std::unexpected(error::capacity);
        maintain<Rows>(first, active, maintenance, [&]() __attribute__((always_inline)) {
            detail::assign(operation(), first, input, effects, active);
        });
        return {};
    }
    template <class Maintenance = no_maintenance>
        requires(!std::is_integral_v<std::remove_cvref_t<Maintenance>>)
    [[nodiscard, gnu::always_inline]] std::expected<void, error>
    set(std::size_t first, const Input& input, source_write_journal& effects,
        Maintenance&& maintenance = {}) const {
        return set(first, input, effects, all_rows<Rows>, std::forward<Maintenance>(maintenance));
    }
    /// Packet i starts at first+i*Rows. A final short packet uses only remaining
    /// rows; selection retains original coordinates. Failure leaves the whole
    /// call unchanged, including effects and maintenance.
    template <class Maintenance = no_maintenance>
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const Input> input, source_write_journal& effects,
            selection selected = selection::all(), Maintenance&& maintenance = {}) const {
        if (effects.used > effects.storage.size())
            return std::unexpected(error::capacity);
        if (auto valid = admit(first, input, selected, effects.remaining()); !valid)
            return valid;
        const auto count = *range_rows<Rows>(operation().size(), first, input.size());
        if (!maintenance.covers(first, count))
            return std::unexpected(error::range);
        replace_unchecked(first, input, selected, effects, maintenance);
        return {};
    }
    template <class Coverage, class Maintenance = no_maintenance>
    void replace_unchecked(std::size_t first, std::span<const Input> input, selection selected,
                           Coverage& effects, Maintenance&& maintenance = {}) const {
        const auto count = *range_rows<Rows>(operation().size(), first, input.size());
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto row = first + i * Rows;
            const auto active = selected_window<Rows>(selected, row, count - i * Rows);
            maintain<Rows>(row, active, maintenance, [&]() __attribute__((always_inline)) {
                detail::assign(operation(), row, input[i], effects, active);
            });
        }
    }
};
} // namespace detail
} // namespace ikea::tuplepack
