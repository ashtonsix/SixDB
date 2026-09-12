#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#if defined(__aarch64__) || defined(__x86_64__)
#define IKEA_CHAIN_CC __attribute__((preserve_none))
namespace ikea::detail {
template <class Native, class Mask, unsigned Slots, class Error, class Sequence> class native_chain;
template <class Native, class Mask, unsigned Slots, class Error, std::size_t... I>
class native_chain<Native, Mask, Slots, Error, std::index_sequence<I...>> {
    static_assert(Slots >= 2 && std::has_single_bit(Slots));
    template <std::size_t> using argument = typename Native::vector;

  public:
    struct slot;
    // Flatten register values at the ABI. Passing the C++ aggregate itself can create a
    // hidden memory argument even when every useful value fits in registers.
    typedef IKEA_CHAIN_CC void (*function)(const slot*, void*, std::size_t, Mask, argument<I>...);
    struct slot {
        function call;
    };
    static_assert(sizeof(slot) == 8);
    static constexpr std::size_t bytes = Slots * sizeof(slot),
                                 alignment = std::max<std::size_t>(64, bytes);
    struct result {
        Native values;
        Mask active;
        bool stop = false;
    };

  private:
    alignas(alignment) std::array<slot, Slots> slots_;
    native_chain() = default;

  public:
    static std::expected<native_chain, Error> prepare(std::span<const function> stages,
                                                      function completion) {
        if (stages.empty() || stages.size() >= Slots || !completion)
            return std::unexpected(Error::range);
        native_chain plan;
        plan.slots_.fill({completion});
        for (std::size_t i = 0; i < stages.size(); ++i) {
            if (!stages[i])
                return std::unexpected(Error::value);
            plan.slots_[i] = {stages[i]};
        }
        return plan;
    }
    [[gnu::always_inline]] static const slot* completion_slot(const slot* next) {
        // Called only by nonterminal stages: next is still inside this table.
        const auto base = reinterpret_cast<std::uintptr_t>(next) & ~std::uintptr_t(bytes - 1);
        return reinterpret_cast<const slot*>(base + (Slots - 1) * sizeof(slot));
    }
    [[gnu::always_inline]] static unsigned ordinal(const slot* next) {
        return (reinterpret_cast<std::uintptr_t>(next) & (bytes - 1)) / sizeof(slot) - 1;
    }
    template <auto Body>
    static IKEA_CHAIN_CC void stage(const slot* next, void* bindings, std::size_t row, Mask active,
                                    argument<I>... values) {
        // Body is an inline-friendly authored operation. It owns no lifetime
        // that needs to survive this hop; bindings and the plan are caller-owned.
        auto invoke = [&] [[clang::always_inline]] () -> result {
            [[clang::always_inline]] return Body(bindings, row, active, Native{{values...}},
                                                 ordinal(next));
        };
        const auto updated = invoke();
        const auto* destination = updated.stop ? completion_slot(next) : next;
        [[clang::musttail]] return destination->call(destination + 1, bindings, row, updated.active,
                                                     updated.values.v[I]...);
    }
    template <auto Finish>
    static IKEA_CHAIN_CC void completion(const slot*, void* bindings, std::size_t row, Mask active,
                                         argument<I>... values) {
        [[clang::always_inline]] Finish(bindings, row, active, Native{{values...}});
    }
#if defined(__aarch64__)
  private:
    // Clang 21 can keep a realigned caller frame base in x19 across
    // preserve_none, although stages may clobber it. A fixed-frame AAPCS
    // entry saves the caller's registers. Flatten vectors here too: an aggregate
    // exceeding four NEON vectors would otherwise become a memory argument.
    // Excluding just this shim from instrumentation keeps its frame fixed;
    // preparation, authored stages and completion remain instrumented.
    __attribute__((noinline, no_sanitize("address", "undefined"))) static void
    enter(function first, const slot* next, void* bindings, std::size_t row, Mask active,
          argument<I>... values) {
        first(next, bindings, row, active, values...);
    }

  public:
#endif
    [[gnu::always_inline]] void run(void* bindings, std::size_t row, Mask active,
                                    Native values) const {
#if defined(__aarch64__)
        enter(slots_[0].call, slots_.data() + 1, bindings, row, active, values.v[I]...);
#else
        slots_[0].call(slots_.data() + 1, bindings, row, active, values.v[I]...);
#endif
    }
};
} // namespace ikea::detail
#undef IKEA_CHAIN_CC
#endif
