#pragma once
#include <ikea/seriespack/detail/dense_read.h>

#if defined(__aarch64__)
#define IKEA_CHAIN_CC __attribute__((preserve_none))
#elif defined(__x86_64__)
#define IKEA_CHAIN_CC __attribute__((preserve_none))
#endif

namespace ikea::seriespack {
#if defined(__aarch64__) || defined(__AVX2__)
namespace detail {
template <class Native, class Mask, unsigned Slots, class Sequence> class native_chain;
template <class Native, class Mask, unsigned Slots, std::size_t... I>
class native_chain<Native, Mask, Slots, std::index_sequence<I...>> {
    static_assert(Slots >= 2 && std::has_single_bit(Slots));
    template <std::size_t> using argument = typename Native::vector;

  public:
    struct slot;
    // Flatten vectors at the ABI. Passing the C++ aggregate itself can create a
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
    static std::expected<native_chain, error> prepare(std::span<const function> stages,
                                                      function completion) {
        if (stages.empty() || stages.size() >= Slots || !completion)
            return std::unexpected(error::range);
        native_chain plan;
        plan.slots_.fill({completion});
        for (std::size_t i = 0; i < stages.size(); ++i) {
            if (!stages[i])
                return std::unexpected(error::value);
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
    [[gnu::always_inline]] void run(void* bindings, std::size_t row, Mask active,
                                    Native values) const {
        slots_[0].call(slots_.data() + 1, bindings, row, active, values.v[I]...);
    }
};
} // namespace detail
/// Provisional straight-through shell, with a type-specific native carrier.
/// Different carriers require a checked bridge; incompatible function casts are
/// not an interface. Stopping/suspension occurs after returning to the owner.
template <unsigned K, unsigned Slots = 8>
using chain = detail::native_chain<native::values<K>, std::uint16_t, Slots,
                                   std::make_index_sequence<native::values<K>::parts>>;

/// A native packet is an execution grain, independent of physical tiling. Keep
/// the flattened carrier within the target's register argument budget (the
/// initial operating points use at most eight vector arguments).
template <unsigned K, unsigned N> struct pipeline_packet {
    static_assert(N >= 16 && N <= 64 && N % 16 == 0);
    using region = native::values<K>;
    using vector = typename region::vector;
    static constexpr unsigned parts = region::parts * (N / 16), rows = N;
    static_assert(parts <= 8, "Choose a smaller pipeline grain to retain register handoff");
    std::array<vector, parts> v;
    template <unsigned P> [[gnu::always_inline]] region get() const {
        region value;
        detail::each<region::parts>([&](auto i) { value.v[i] = v[P * region::parts + i]; });
        return value;
    }
    template <unsigned P> [[gnu::always_inline]] void set(region value) {
        detail::each<region::parts>([&](auto i) { v[P * region::parts + i] = value.v[i]; });
    }
};
template <unsigned K, unsigned N, unsigned Slots = 8>
using packet_chain = detail::native_chain<pipeline_packet<K, N>, std::uint64_t, Slots,
                                          std::make_index_sequence<pipeline_packet<K, N>::parts>>;
#endif
} // namespace ikea::seriespack
#undef IKEA_CHAIN_CC
