#include "early-zero-kernel.h"
#include "early-wide-kernel.h"

namespace early {
template <unsigned K, int Op> void graduate_case() {
    for (const char* pattern : {"uniform", "reject_first", "shared_prefix", "mixed50", "all_equal"}) {
        Fixture f(K, 8, pattern);
        describe(f, Op);
        run_variant<K, Op, 8, 0>(f);
        run_variant<K, Op, 8, 1>(f);
        run_variant<K, Op, 8, 2>(f);
        run_variant<K, Op, 8, 3>(f);
        run_variant<K, Op, 8, 4>(f);
        using F = sp::layout<K, sp::geometry::local, 8>;
        const auto source = sp::const_view<F>::attach_unchecked(
            windows * 16, {{{std::as_bytes(std::span{f.bytes}), F::tile_bytes},{std::as_bytes(std::span{f.heads}), F::tile_rows},{}}});
        const auto expression = sp::author::guarded(sp::author::expression(source));
        auto call = [&](unsigned w) __attribute__((always_inline)) {
            asm("" : "+r"(w));
            return sp::author::filter16_unchecked<C(Op)>(expression, w * 16, f.queries[w]);
        };
        for (unsigned w = 0; w < windows; ++w) {
            unsigned expected = 0;
            for (unsigned lane = 0; lane < 16; ++lane)
                expected |= unsigned(oracle(Op, f.truth[w * 16 + lane], f.queries[w])) << lane;
            if (call(w) != expected)
                std::abort();
        }
        measure(f, Op, "head8", "graduated", call);
        // Same production body with placement visible inside the measured
        // consumer, matching the independent controls' contiguous binding.
        auto inlined = [&](unsigned w) __attribute__((always_inline)) {
            asm("" : "+r"(w));
            const auto dense = sp::const_view<F>::attach_unchecked(
                windows * 16, {{{std::as_bytes(std::span{f.bytes}), F::tile_bytes},{std::as_bytes(std::span{f.heads}), F::tile_rows},{}}});
            return sp::author::filter16_unchecked<C(Op)>(
                sp::author::guarded(sp::author::expression(dense)), w * 16, f.queries[w]);
        };
        for (unsigned w = 0; w < windows; ++w)
            if (inlined(w) != call(w))
                std::abort();
        measure(f, Op, "head8", "graduated_inline", inlined);
        if constexpr (Op < 2) {
            auto zero = [&](unsigned w) __attribute__((always_inline)) {
                asm("" : "+r"(w));
                return vector_zero<K, Op>(f.bytes.data() + w * 2 * (K - 8),
                                          f.heads.data() + w * 16, f.queries[w]);
            };
            measure(f, Op, "head8", "vector_zero", zero);
        }
#if defined(__AVX512BW__) && defined(__AVX512DQ__)
        if constexpr (K == 40) {
            wide_variant<K, Op, 8, false>(f);
            wide_variant<K, Op, 8, true>(f);
        }
#endif
    }
}
}

int main(int argc, char**) {
    using namespace early;
    iterations = 500000;
    repeats = 7;
    check_only = argc > 1;
    std::puts("module,width,op,pattern,variant,metric,rep,ns_window,selected_fraction,first_resolved_fraction");
    sp::detail::each<5>([](auto op) {
        graduate_case<12, op>();
        graduate_case<40, op>();
        graduate_case<64, op>();
    });
    std::fprintf(stderr, "graduated controls passed; checksum=%llu\n", (unsigned long long)observed);
}
