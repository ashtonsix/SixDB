#pragma once
namespace ikea::hashtable {
// A construction-time choice; native lookup contains no kernel dispatch.
// Specialized recipes retain a portable exact fallback when their ISA is absent.
enum class search_kernel {
    generic,
    exact30,
    fused64,
    zero30,
    body30,
    neon64,
    neon30,
    exact_small512,
    body_small256,
    body_small_neon,
    exact_small256
};
// Selected once by the caller's staged consumer. The eight-byte lookahead
// prefetches primary values/references and consumes an exact hit in that visit.
// It changes scheduling, never the representation or the direct lookup path.
enum class staged_values { dependent, lookahead8 };
} // namespace ikea::hashtable
