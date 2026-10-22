#pragma once
#include <ikea/intervals/search.h>

namespace ikea::intervals::detail {
enum class prediction_kind : std::uint8_t { plain, constant, interpolation };
struct numeric_preparation {
    std::uint64_t first{}, last{};
    double scale{};
    std::uint8_t window_log{};
    prediction_kind kind = prediction_kind::plain;
};
// Cold source preparation. The admitted native unsigned array is ordered,
// unchanged and readable. Byte width is2/4/8; loads may be unaligned.
numeric_preparation prepare_numeric(const void*, unsigned count, unsigned bytes);

template <class T> struct prediction_model {
    T first{}, last{};
    double scale{};
    std::uint8_t window_log{};
    prediction_kind kind = prediction_kind::plain;
};

template <edge E, class T>
[[gnu::always_inline]] rank_window checked_bracket(const T* data, unsigned count, T point,
                                                   rank_window proposed) {
    if (meets<E>(read_element(data, proposed.first), point))
        return {0, proposed.first};
    if (!meets<E>(read_element(data, proposed.limit - 1), point))
        return {proposed.limit, count};
    return {proposed.first + 1, proposed.limit - 1};
}

template <class T, edge E> struct numeric_search {
    const T* data;
    T point;
    unsigned count, first, limit;
    bool predicted;
    bool has_local_read() const noexcept {
        return data != nullptr;
    }
    std::span<const std::byte> local_window() const noexcept {
        return data ? std::span<const std::byte>(reinterpret_cast<const std::byte*>(data) +
                                                     std::size_t(first) * sizeof(T),
                                                 std::size_t(limit - first) * sizeof(T))
                    : std::span<const std::byte>{};
    }
    // hi is a known qualifying position or the count sentinel. Predictions are
    // never evidence: check both edges before using the narrowed interval.
    [[gnu::always_inline]] rank_window corrected_interval() const noexcept {
        if (!data || !predicted)
            return {first, limit};
        return checked_bracket<E>(data, count, point, {first, limit});
    }
    [[gnu::always_inline]] unsigned finish() const noexcept {
        const auto r = corrected_interval();
        return hybrid_window<E, true>(data, count, r.first, r.limit, point);
    }
};

template <class T> class numeric_binding {
    direct_source<T> source_;
    prediction_model<T> model_;

    // One classification/model law, with immediate and suspended consumers.
    // Ready answers are continuations, not null-pointer phases forced through
    // the ordinary search's convergence path.
    template <edge E, std::integral Q, class Ready, class Search>
    [[gnu::always_inline]] auto choose(Q point, Ready ready, Search search) const {
        const auto n = size();
        const prepared_query<T, Q> q(point);
        if (q.relation)
            return ready(q.relation < 0 ? 0 : n);
        if (!n)
            return ready(0);
        if (model_.kind == prediction_kind::plain)
            return search(q.value, rank_window{0, n}, false);
        if (meets<E>(model_.first, q.value))
            return ready(0);
        if (!meets<E>(model_.last, q.value))
            return ready(n);
        const auto width = 1u << model_.window_log;
        // The in-domain estimate is at most n-1 plus double rounding error.
        // n<=UINT_MAX leaves a whole integer of headroom, so conversion is safe;
        // clamp in integers rather than extending the FP dependency chain.
        const auto estimate =
            std::min(n - 1, unsigned(double(T(q.value - model_.first)) * model_.scale));
        const auto base = std::min(estimate > width / 2 ? estimate - width / 2 : 0, n - width);
        return search(q.value, rank_window{base, base + width}, true);
    }

  public:
    explicit numeric_binding(direct_source<T> source) : source_(source) {
        if (source.count < 4096)
            return;
        const auto p = prepare_numeric(source.data, source.count, sizeof(T));
        model_ = {T(p.first), T(p.last), p.scale, p.window_log, p.kind};
    }
    unsigned size() const noexcept {
        return source_.count;
    }
    T at(unsigned rank) const noexcept {
        return source_.at(rank);
    }
    template <edge E, class Q> [[gnu::always_inline]] auto prepare(const Q& point) const {
        if constexpr (!std::integral<Q>)
            return ready_search{source_.template boundary<E>(point)};
        else {
            const auto n = size();
            auto ready = [&](unsigned rank) __attribute__((always_inline)) {
                return numeric_search<T, E>{nullptr, T{}, n, rank, rank, false};
            };
            return choose<E>(
                point, ready,
                [&](T q, rank_window r, bool predicted) __attribute__((always_inline)) {
                    return numeric_search<T, E>{source_.data, q, n, r.first, r.limit, predicted};
                });
        }
    }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q& point) const {
        if constexpr (!std::integral<Q>)
            return source_.template boundary<E>(point);
        else
            return choose<E>(
                point, [](unsigned rank) { return rank; },
                [&](T q, rank_window r, bool predicted) __attribute__((always_inline)) {
                    if (predicted)
                        r = checked_bracket<E>(source_.data, size(), q, r);
                    return hybrid_window<E, true>(source_.data, size(), r.first, r.limit, q);
                });
    }
};
} // namespace ikea::intervals::detail
