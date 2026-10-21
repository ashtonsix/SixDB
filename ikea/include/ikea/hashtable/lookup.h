#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace ikea::hashtable {
/// Execution hint only. Neither choice promises presence or absence.
enum class lookup_hint { predictable, unpredictable };

struct lookup_statistics {
    std::uint64_t predictable = 0, unpredictable = 0, probes = 0, switches = 0;
};
namespace detail {
struct lookup_clock {
    std::uint64_t operator()() const {
#if defined(__x86_64__)
        _mm_lfence();
        const auto ticks = __rdtsc();
        _mm_lfence();
        return ticks;
#else
        return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
    }
};
} // namespace detail

/// Caller-owned, unsynchronized state for one stream of comparable operations.
/// Selects a whole compiled operation, including its consumer, never each key.
class lookup_selector {
    bool unpredictable_ = false;
    std::uint64_t predictable_ticks_ = 0, unpredictable_ticks_ = 0;
    lookup_statistics stats_;
    unsigned epoch_ = 0;

    bool sampling() const { return epoch_ % period < 2; }
    bool method() const {
        const auto phase = epoch_ % period;
        return phase < 2 ? bool(phase) : unpredictable_;
    }
    void observed(bool method, std::uint64_t ticks, std::size_t count) {
        (method ? stats_.unpredictable : stats_.predictable) += count;
        if (sampling()) {
            ++stats_.probes;
            // Bound synthetic/very long clock intervals before fixed-point
            // normalization and the preference comparison. No per-key division.
            constexpr auto limit = (std::numeric_limits<std::uint64_t>::max() / 110) >> 10;
            (method ? unpredictable_ticks_ : predictable_ticks_) =
                (std::min(ticks, limit) << 10) / count;
            if (epoch_ % period == 1) {
                const bool next = unpredictable_ticks_ * 110 < predictable_ticks_ * 100;
                stats_.switches += next != unpredictable_;
                unpredictable_ = next;
            }
        }
        ++epoch_;
    }

  public:
    static constexpr unsigned block = 256, period = 64;
    lookup_hint preferred_hint() const {
        return unpredictable_ ? lookup_hint::unpredictable : lookup_hint::predictable;
    }
    lookup_statistics statistics() const { return stats_; }
    void reset() { *this = {}; }

    /// Calls operation(hint_constant, first, count) exactly once per disjoint
    /// range, in input order. Keep prefetch lookahead relative to the full input,
    /// not the internal ranges. Empty input invokes nothing. State survives calls.
    /// The operation must not reenter/reset this selector. If it throws, already
    /// performed caller effects remain; the incomplete range is not observed.
    /// An optional monotonic unsigned clock supports caller timing and tests.
    template <class Operation, class Clock = detail::lookup_clock>
    void run(std::size_t count, Operation&& operation, Clock clock = {}) {
        for (std::size_t first = 0; first < count;) {
            const auto size = std::min<std::size_t>(count - first, block);
            const bool selected = method(), sample = sampling();
            const auto start = sample ? clock() : 0;
            if (selected)
                std::invoke(operation,
                            std::integral_constant<lookup_hint, lookup_hint::unpredictable>{},
                            first, size);
            else
                std::invoke(operation,
                            std::integral_constant<lookup_hint, lookup_hint::predictable>{},
                            first, size);
            const auto ticks = sample ? clock() - start : 0;
            observed(selected, ticks, size);
            first += size;
        }
    }
};
static_assert(sizeof(lookup_selector) == 64);
} // namespace ikea::hashtable
