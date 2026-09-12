#pragma once
#include <ikea/tuplepack/write.h>
#include <ikea/detail/overlap.h>
#include <tuple>
#include <utility>

namespace ikea::tuplepack::composition {
namespace detail {
template <class A, class B> bool leaf_conflict(const A& a, const B& b) {
    bool conflict = false;
    a.visit_fields([&](const view& av, unsigned x, byte xm) {
        b.visit_fields([&](const view& bv, unsigned y, byte ym) {
            if (!(xm & ym))
                return;
            const ikea::detail::occupied_run ar{
                reinterpret_cast<std::uintptr_t>(av.storage().data()) + av.offset() + x,
                av.stride(), 1, av.size()};
            const ikea::detail::occupied_run br{
                reinterpret_cast<std::uintptr_t>(bv.storage().data()) + bv.offset() + y,
                bv.stride(), 1, bv.size()};
            conflict |= ikea::detail::overlaps(ar, br);
        });
    });
    return conflict;
}
template <class A, class B> bool conflicting(const A& a, const B& b) {
    bool conflict = false;
    a.visit_leaves([&](const auto& left) {
        b.visit_leaves([&](const auto& right) { conflict |= leaf_conflict(left, right); });
    });
    return conflict;
}
template <std::size_t I = 0, std::size_t J = 1, class Tuple> bool conflicting(const Tuple& parts) {
    constexpr auto size = std::tuple_size_v<Tuple>;
    if constexpr (I >= size)
        return false;
    else if constexpr (J >= size)
        return conflicting<I + 1, I + 2>(parts);
    else
        return conflicting(std::get<I>(parts), std::get<J>(parts)) || conflicting<I, J + 1>(parts);
}
} // namespace detail

/// Read composition retains each child's binding and logical coordinates.
/// Rebuild this small parent after child substitution. Old parents retain their
/// source bindings; aliases observe shared writes. The owner preserves reader
/// editions. Semantic struct assembly belongs to the caller.
template <class... Read> class projection {
    std::tuple<Read...> children_;

  public:
    explicit projection(Read... children) : children_(std::move(children)...) {}
    std::size_t size() const noexcept {
        std::size_t count = ~std::size_t(0);
        std::apply([&](const auto&... child) { ((count = std::min(count, child.size())), ...); },
                   children_);
        return count;
    }
    auto get_unchecked(std::size_t row) const {
        return std::apply(
            [&](const auto&... child) { return std::tuple(child.get_unchecked(row)...); },
            children_);
    }
};

/// One logical update over several code packets/children. All inputs/capacity
/// are admitted before any old observation, effect or store. Completed after
/// observations include every child update, including disjoint codes sharing
/// physical bytes. No dynamic per-child dispatch is introduced by this shell.
template <class... Operation> class mutation_group {
    std::tuple<Operation...> children_;
    explicit mutation_group(Operation... children) : children_(std::move(children)...) {}

  public:
    using input_type = std::tuple<typename Operation::input_type...>;
    [[nodiscard]] static std::expected<mutation_group, error> bind(Operation... children) {
        mutation_group result(std::move(children)...);
        if (detail::conflicting(result.children_))
            return std::unexpected(error::overlap);
        return result;
    }
    std::size_t size() const noexcept {
        std::size_t count = ~std::size_t(0);
        std::apply([&](const auto&... child) { ((count = std::min(count, child.size())), ...); },
                   children_);
        return count;
    }
    std::size_t effect_capacity() const noexcept {
        return std::apply(
            [](const auto&... child) { return (std::size_t(0) + ... + child.effect_capacity()); },
            children_);
    }
    template <class Visit> void visit_leaves(Visit&& visit) const {
        std::apply([&](const auto&... child) { (child.visit_leaves(visit), ...); }, children_);
    }
    template <class Visit> void visit_fields(Visit&& visit) const {
        std::apply([&](const auto&... child) { (child.visit_fields(visit), ...); }, children_);
    }
    bool accepts(const input_type& input) const noexcept {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (std::get<I>(children_).accepts(std::get<I>(input)) && ...);
        }(std::index_sequence_for<Operation...>{});
    }
    template <class Coverage>
    [[gnu::always_inline]] void set_unchecked(std::size_t row, const input_type& input,
                                              Coverage& effects) const {
        assign_unchecked<0>(row, input, effects);
    }
    [[nodiscard]] std::expected<void, error> admit(std::size_t first,
                                                   std::span<const input_type> input,
                                                   selection selected, std::size_t capacity) const {
        if (first > size() || input.size() > size() - first ||
            !selected.covers(first, input.size()))
            return std::unexpected(error::range);
        for (std::size_t row = 0; row < input.size(); ++row) {
            if (!selected.contains(first + row))
                continue;
            if (capacity < effect_capacity())
                return std::unexpected(error::capacity);
            capacity -= effect_capacity();
            if (!accepts(input[row]))
                return std::unexpected(error::value);
        }
        return {};
    }
    template <class Coverage, class Maintenance = no_maintenance>
    void replace_unchecked(std::size_t first, std::span<const input_type> input, selection selected,
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
    template <class Maintenance = no_maintenance>
    [[nodiscard]] std::expected<void, error>
    replace(std::size_t first, std::span<const input_type> input, source_write_journal& effects,
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
    template <class Maintenance = no_maintenance>
    [[nodiscard, gnu::always_inline]] std::expected<void, error>
    set(std::size_t row, const input_type& input, source_write_journal& effects,
        Maintenance&& maintenance = {}) const {
        if (row >= size() || !maintenance.covers(row, 1))
            return std::unexpected(error::range);
        if (!accepts(input))
            return std::unexpected(error::value);
        if (effects.used > effects.storage.size() || effects.remaining() < effect_capacity())
            return std::unexpected(error::capacity);
        auto before = maintenance.before(row);
        set_unchecked(row, input, effects);
        maintenance.after(row, std::move(before));
        return {};
    }

  private:
    template <std::size_t I, class Coverage>
    [[gnu::always_inline]] void assign_unchecked(std::size_t row, const input_type& input,
                                                 Coverage& effects) const {
        if constexpr (I < sizeof...(Operation)) {
            std::get<I>(children_).set_unchecked(row, std::get<I>(input), effects);
            assign_unchecked<I + 1>(row, input, effects);
        }
    }
};
template <class... Operation> auto bind_group(Operation... operations) {
    return mutation_group<Operation...>::bind(std::move(operations)...);
}
} // namespace ikea::tuplepack::composition
