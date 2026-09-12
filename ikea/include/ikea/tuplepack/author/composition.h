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
    static constexpr unsigned Rows = ikea::tuplepack::detail::common_rows<Read...>;
    static_assert(((ikea::tuplepack::detail::operation_rows<Read> == Rows) && ...),
                  "Projection children must use the same original-row packet shape");
    std::tuple<Read...> children_;

  public:
    static constexpr unsigned rows = Rows;
    explicit projection(Read... children) : children_(std::move(children)...) {}
    std::size_t size() const noexcept {
        std::size_t count = ~std::size_t(0);
        std::apply([&](const auto&... child) { ((count = std::min(count, child.size())), ...); },
                   children_);
        return count;
    }
    [[gnu::always_inline]] auto
    get_unchecked(std::size_t row,
                  std::uint64_t active = ikea::tuplepack::detail::all_rows<Rows>) const {
        return std::apply(
            [&](const auto&... child) __attribute__((always_inline)) {
                auto read = [&](const auto& c) __attribute__((always_inline)) {
                    if constexpr (requires { c.get_unchecked(row, active); })
                        return c.get_unchecked(row, active);
                    else
                        return c.get_unchecked(row);
                };
                return std::tuple(read(child)...);
            },
            children_);
    }
};

/// One logical update over several code packets/children. All inputs/capacity
/// are admitted before any old observation, effect or store. Completed after
/// observations include every child update, including disjoint codes sharing
/// physical bytes. No dynamic per-child dispatch is introduced by this shell.
template <class... Operation>
class mutation_group
    : public ikea::tuplepack::detail::mutation_commands<
          mutation_group<Operation...>, std::tuple<typename Operation::input_type...>,
          ikea::tuplepack::detail::common_rows<Operation...>> {
    static constexpr unsigned Rows = ikea::tuplepack::detail::common_rows<Operation...>;
    static_assert(((ikea::tuplepack::detail::operation_rows<Operation> == Rows) && ...),
                  "Mutation children must use the same original-row packet shape");
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
    bool accepts(const input_type& input,
                 std::uint64_t active = ikea::tuplepack::detail::all_rows<Rows>) const noexcept {
        return [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            return (ikea::tuplepack::detail::accepts(std::get<I>(children_), std::get<I>(input),
                                                     active) &&
                    ...);
        }(std::index_sequence_for<Operation...>{});
    }
    template <class Coverage>
    [[gnu::always_inline]] void
    set_unchecked(std::size_t row, const input_type& input, Coverage& effects,
                  std::uint64_t active = ikea::tuplepack::detail::all_rows<Rows>) const {
        assign_unchecked<0>(row, input, effects, active);
    }

  private:
    template <std::size_t I, class Coverage>
    [[gnu::always_inline]] void assign_unchecked(std::size_t row, const input_type& input,
                                                 Coverage& effects, std::uint64_t active) const {
        if constexpr (I < sizeof...(Operation)) {
            ikea::tuplepack::detail::assign(std::get<I>(children_), row, std::get<I>(input),
                                            effects, active);
            assign_unchecked<I + 1>(row, input, effects, active);
        }
    }
};
template <class... Operation> auto bind_group(Operation... operations) {
    return mutation_group<Operation...>::bind(std::move(operations)...);
}
} // namespace ikea::tuplepack::composition
