#pragma once
#include <ikea2/seriespack/author/native.h>
namespace ikea2::seriespack {
struct no_summary {
    static constexpr bool needs_before = false;
};
#if defined(__aarch64__) || defined(__AVX2__)
/// One optional local maintenance recipe. Its law is modulo-u64 replacement
/// contribution; it is not a generic floating aggregate or extrema repair rule.
struct sum_change {
    static constexpr bool needs_before = true;
    native::sum_state removed, added;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    zmm::sum_state group_removed, group_added;
    template <unsigned K, unsigned N>
    [[gnu::always_inline]] void observe_group(zmm::values<K, N> before, zmm::values<K, N> after,
                                              std::uint64_t selected) {
        const auto mask = zmm::active_mask<K, N>(selected);
        group_removed.add(zmm::sum(before, mask));
        group_added.add(zmm::sum(after, mask));
    }
#endif
    std::uint64_t scalar_delta = 0;
    void observe_scalar(std::uint64_t before, std::uint64_t after) {
        scalar_delta += after - before;
    }
    template <unsigned K>
    [[gnu::always_inline]] void observe(native::values<K> before, native::values<K> after,
                                        std::uint16_t selected) {
        const auto mask = native::mask16<K>(selected);
        removed.add(native::sum(before, mask));
        added.add(native::sum(after, mask));
    }
    std::uint64_t finish() const {
        auto delta = added.finish() - removed.finish() + scalar_delta;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
        delta += group_added.finish() - group_removed.finish();
#endif
        return delta;
    }
};

#endif
} // namespace ikea2::seriespack
