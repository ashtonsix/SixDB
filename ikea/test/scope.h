#pragma once

namespace ikea::test {
/// SIXDB_TEST_SCOPE=fast instantiates width matrices only around byte and
/// lane boundaries, every narrow width and every striped payload width; full
/// instantiates all 64. Each width compiles distinct kernels, so land changes
/// to width-specialized code after a full run.
#if defined(IKEA_TEST_SCOPE_FULL)
inline constexpr bool full_scope = true;
#else
inline constexpr bool full_scope = false;
#endif

constexpr bool sampled_width(unsigned k) {
    if (full_scope || k <= 10 || k == 12 || k == 14 || k == 20)
        return true;
    const unsigned r = k % 8;
    return r == 7 || r == 0 || r == 1;
}
} // namespace ikea::test
