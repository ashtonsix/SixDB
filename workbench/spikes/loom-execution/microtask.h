#pragma once

#include "prefetch.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace loom_spike::microtask {

using Vector = std::uint64_t __attribute__((vector_size(16)));
enum class Prefetch { none, first_line, pages };
struct Dependency {
    const void* address = nullptr;
    std::size_t bytes = 0;
};
struct SpinBudget {
    std::uint64_t visits = 128;
    std::uint64_t elapsed_ns = 0;
    unsigned clock_interval = 16;
};
struct Counters {
    std::uint64_t accepted = 0, refused = 0, completed = 0, visits = 0;
    std::uint64_t abandoned = 0;
    std::uint64_t promoted = 0, budget_stops = 0, prefetches = 0;
    std::uint64_t clock_samples = 0;
    std::uint64_t high_ring = 0, high_overflow = 0;
};

void issue_prefetch(Dependency, Prefetch, std::size_t page_bytes, Counters&);
std::uint64_t monotonic_ns();

#if defined(__x86_64__) || defined(__aarch64__)
#if defined(LOOM_MICROTASK_ORDINARY_ABI)
#define LOOM_MICROTASK_CC
#else
#define LOOM_MICROTASK_CC __attribute__((preserve_none))
#endif
#else
#error "The experimental microtask ABI requires x86-64 or AArch64"
#endif

// Two independently typed ABI families make scalar versus live-vector cost
// observable. Native values are separate arguments, never an ABI aggregate.
template <class Sequence> class Ring;
template <std::size_t... I> class Ring<std::index_sequence<I...>> {
    template <std::size_t> using vector_argument = Vector;

  public:
    struct Slot;
    typedef LOOM_MICROTASK_CC void (*Function)(Ring*, Slot*, std::uint64_t, std::uint64_t,
                                               std::uint64_t, std::uint64_t,
                                               vector_argument<I>...);
    struct Arguments {
        std::uint64_t a = 0, b = 0, c = 0, d = 0;
        std::array<Vector, sizeof...(I)> vectors{};
    };
    struct Slot {
        Function function = nullptr;
        Arguments arguments;
        Dependency dependency;
        std::size_t home = 0, inventory = 0;
        prefetch::Class admission_class = prefetch::Class::none;
        bool completed = false;
    };

    // All ring/overflow storage is allocated here. Enqueue and switching allocate
    // nothing. This local object and borrowed caller state cannot move while live.
    Ring(std::size_t ring_slots, std::size_t overflow_slots, Prefetch,
         std::size_t page_bytes = 4096);
    Ring(std::size_t ring_slots, std::size_t overflow_slots, prefetch::Binding);
    ~Ring() { assert(!pending() && "accepted microtasks must be drained before ring destruction"); }
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    Ring(Ring&&) = delete;
    Ring& operator=(Ring&&) = delete;
    bool push(Function, Arguments, Dependency);
    bool push(Function, Arguments, Dependency, prefetch::Class admission_class);
    std::uint64_t spin(SpinBudget);
    // At an owner-thread spin-return boundary, discard physical frames. The
    // owner still owes any larger admitted operation its logical disposition.
    // Borrowed input/state must remain valid until this call has returned.
    std::size_t discard();
    // Experimental owner-boundary cleanup, not a public closure/owner ABI.
    // The typed predicate may only inspect arguments and must not mutate this
    // ring. Accepted borrowers stay alive until this call returns. Survivors
    // retain their native state and overflow FIFO order; only removed frames
    // count as abandoned. No owner test is added to the native switch path.
    template <class Predicate> std::size_t release_if(Predicate predicate) {
        static_assert(std::is_nothrow_invocable_r_v<bool, Predicate, const Arguments&>);
        if (spinning_)
            throw std::logic_error("release_if requires an owner spin-return boundary");
        spinning_ = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{spinning_};
        std::size_t removed = 0;
        for (std::size_t home = 0; home < ring_.size(); ++home) {
            auto* slot = ring_[home];
            if (slot && predicate(std::as_const(slot->arguments))) {
                ring_[home] = nullptr;
                --live_;
                free_.push_back(slot->inventory);
                vacant_.push_back(home);
                ++removed;
            }
        }
        // Logical write position never overtakes read position, including a
        // wrapped full FIFO, so existing pointer storage suffices to compact.
        auto read = overflow_head_, write = overflow_head_;
        const auto previous_size = overflow_size_;
        std::size_t survivors = 0;
        const auto increment = [&](std::size_t& cursor) {
            if (++cursor == overflow_.size())
                cursor = 0;
        };
        for (std::size_t i = 0; i < previous_size; ++i) {
            auto* slot = overflow_[read];
            increment(read);
            if (predicate(std::as_const(slot->arguments))) {
                free_.push_back(slot->inventory);
                ++removed;
            } else {
                overflow_[write] = slot;
                increment(write);
                ++survivors;
            }
        }
        overflow_size_ = survivors;
        overflow_tail_ = write;
        while (overflow_size_ && !vacant_.empty()) {
            auto* slot = overflow_[overflow_head_];
            increment(overflow_head_);
            --overflow_size_;
            const auto home = vacant_.back();
            vacant_.pop_back();
            slot->home = home;
            ring_[home] = slot;
            ++live_;
            ++counters_.promoted;
            prefetch_admitted(*slot);
        }
        counters_.abandoned += removed;
        return removed;
    }
    std::size_t pending() const { return live_ + overflow_size_; }
    std::size_t storage_bytes() const {
        return sizeof(*this) + inventory_.capacity() * sizeof(Slot) +
               free_.capacity() * sizeof(std::size_t) +
               vacant_.capacity() * sizeof(std::size_t) +
               (ring_.capacity() + overflow_.capacity()) * sizeof(Slot*);
    }
    const Counters& counters() const { return counters_; }

    // A callback supplies the next qualified dependency. A prefetch is a hint;
    // resumption neither tests cache readiness nor acquires memory permissions.
    void depend(Slot& slot, Dependency dependency) {
        (void)slot;
        issue_prefetch(dependency, prefetch_, page_bytes_, counters_);
    }
    template <prefetch::Class Kind>
    [[gnu::always_inline]] void depend_known(Slot& slot, Dependency dependency) {
        (void)slot;
        prefetch::issue<Kind>(dependency.address, hint_binding_.page_bytes);
    }
    [[gnu::always_inline]] void depend_runtime(Slot& slot, Dependency dependency, prefetch::Class kind) {
        (void)slot;
        const auto region = prefetch::at<prefetch::Class::page1>(dependency.address, hint_binding_.page_bytes);
        prefetch::issue_runtime(kind, region);
    }
    void complete(Slot& slot) { slot.completed = true; }

    static LOOM_MICROTASK_CC void yield(Ring* ring, Slot* current, std::uint64_t a,
                                       std::uint64_t b, std::uint64_t c, std::uint64_t d,
                                       vector_argument<I>... vectors) {
        if (current) {
            if (current->completed) {
                [[clang::musttail]] return retire_yield(ring, current, a, b, c, d, vectors...);
            }
            current->arguments = {a, b, c, d, {{vectors...}}};
        }
        [[clang::musttail]] return resume<true>(ring, nullptr, a, b, c, d, vectors...);
    }

  private:
    std::vector<Slot> inventory_;
    std::vector<std::size_t> free_, vacant_;
    std::vector<Slot*> ring_, overflow_;
    std::size_t cursor_ = 0, live_ = 0, overflow_head_ = 0, overflow_tail_ = 0, overflow_size_ = 0;
    Prefetch prefetch_;
    std::size_t page_bytes_;
    prefetch::Binding hint_binding_;
    prefetch::Function admitted_hint_ = nullptr;
    Counters counters_;
    SpinBudget budget_;
    std::uint64_t spin_visits_ = 0, deadline_ = 0;
    unsigned clock_countdown_ = 1;
    bool spinning_ = false;
    void retire(Slot&);
    void prefetch_admitted(const Slot&);
    Slot* next() {
        const auto increment = [&] {
            if (++cursor_ == ring_.size())
                cursor_ = 0;
        };
        while (!ring_[cursor_])
            increment();
        auto* slot = ring_[cursor_];
        increment();
        return slot;
    }
    // The common continuation contains no ordinary call. Completed frames and
    // due clock samples leave it through same-ABI tail calls, so their stack
    // frames are paid only at those boundaries. resume<false> skips precisely
    // the sample already handled by clock_yield; it does not save state again.
    template <bool CheckClock>
    __attribute__((always_inline)) static LOOM_MICROTASK_CC void
    resume(Ring* ring, Slot*, std::uint64_t a, std::uint64_t b, std::uint64_t c,
           std::uint64_t d, vector_argument<I>... vectors) {
        if (!ring->live_)
            return;
        if (ring->spin_visits_ >= ring->budget_.visits) {
            ++ring->counters_.budget_stops;
            return;
        }
        if constexpr (CheckClock) {
            if (ring->budget_.elapsed_ns && ring->spin_visits_ && !--ring->clock_countdown_) {
                [[clang::musttail]] return clock_yield(ring, nullptr, a, b, c, d, vectors...);
            }
        }
        auto* next = ring->next();
        ++ring->spin_visits_;
        const auto& args = next->arguments;
        [[clang::musttail]] return next->function(ring, next, args.a, args.b, args.c, args.d,
                                                 args.vectors[I]...);
    }
    __attribute__((noinline)) static LOOM_MICROTASK_CC void
    retire_yield(Ring* ring, Slot* slot, std::uint64_t a, std::uint64_t b, std::uint64_t c,
                 std::uint64_t d, vector_argument<I>... vectors) {
        ring->retire(*slot);
        [[clang::musttail]] return resume<true>(ring, nullptr, a, b, c, d, vectors...);
    }
    __attribute__((noinline)) static LOOM_MICROTASK_CC void
    clock_yield(Ring* ring, Slot*, std::uint64_t a, std::uint64_t b, std::uint64_t c,
                std::uint64_t d, vector_argument<I>... vectors) {
        ring->clock_countdown_ = ring->budget_.clock_interval;
        ++ring->counters_.clock_samples;
        if (monotonic_ns() >= ring->deadline_) {
            ++ring->counters_.budget_stops;
            return;
        }
        [[clang::musttail]] return resume<false>(ring, nullptr, a, b, c, d, vectors...);
    }
#if defined(__aarch64__) && !defined(LOOM_MICROTASK_ORDINARY_ABI)
    // Same pinned-Clang frame-base hazard as Ikea's native chain: the entry is
    // ordinary AAPCS, fixed-frame and uninstrumented; callbacks remain checked.
    __attribute__((noinline, no_sanitize("address", "undefined"))) static void
    enter(Function first, Ring* ring, Slot* slot, std::uint64_t a, std::uint64_t b,
          std::uint64_t c, std::uint64_t d, vector_argument<I>... vectors) {
        first(ring, slot, a, b, c, d, vectors...);
    }
#endif
};

using ScalarRing = Ring<std::make_index_sequence<0>>;
using VectorRing = Ring<std::make_index_sequence<2>>;
extern template class Ring<std::make_index_sequence<0>>;
extern template class Ring<std::make_index_sequence<2>>;

} // namespace loom_spike::microtask

// Intentionally remains defined for authored callbacks of this private ABI.
