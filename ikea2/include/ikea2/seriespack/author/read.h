#pragma once
#include <ikea2/seriespack/author/expression.h>

namespace ikea2::seriespack::composition {
namespace detail {
struct admitted_value {};
/// Inspect only leaves actually used by this expression, including substituted
/// leaves. Admission belongs outside the executing kernel and reads no bytes.
struct admission {
    std::size_t count;
    bool valid = true;
    template <class S, field_kind F>
    admitted_value read(const leaf<S, F>& source, std::size_t, bool) {
        const auto extent = [&] {
            if constexpr (requires { source.source.count; })
                return source.source.count;
            else
                return source.source.size();
        }();
        valid &= count <= extent;
        return {};
    }
    template <unsigned Shift> admitted_value join(admitted_value, admitted_value) {
        return {};
    }
};
} // namespace detail

template <class Expression> class prepared {
    Expression expression_;
    std::size_t count_;
    prepared(Expression e, std::size_t count) : expression_(std::move(e)), count_(count) {}

  public:
    static std::expected<prepared, error> bind(Expression e, std::size_t count) {
        detail::admission check{count};
        composition::read(check, e, std::size_t{0}, true);
        if (!check.valid)
            return std::unexpected(error::range);
        return prepared{std::move(e), count};
    }
    std::size_t size() const {
        return count_;
    }
    const Expression& expression() const {
        return expression_;
    }
#if defined(__aarch64__) || defined(__AVX2__)
    template <class Prefilter>
    std::uint64_t sum_unchecked(std::size_t first, std::size_t count, std::uint64_t cutoff,
                                Prefilter&& mask) const {
        return sum_regions(expression_, first, count, cutoff, std::forward<Prefilter>(mask));
    }
    template <class Prefilter>
    [[nodiscard]] std::expected<std::uint64_t, error>
    sum(std::size_t first, std::size_t count, std::uint64_t cutoff, Prefilter&& mask) const {
        if (first > count_ || count > count_ - first)
            return std::unexpected(error::range);
        return sum_unchecked(first, count, cutoff, std::forward<Prefilter>(mask));
    }
#endif
};
template <class E> auto prepare(E expression, std::size_t count) {
    return prepared<E>::bind(std::move(expression), count);
}
} // namespace ikea2::seriespack::composition
