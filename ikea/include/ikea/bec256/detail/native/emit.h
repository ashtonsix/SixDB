#pragma once
#include <ikea/bec256/detail/pack.h>
#if defined(__AVX512BW__) && defined(__AVX512VL__)
#include <ikea/bec256/detail/native/assemble.h>
#endif

namespace ikea::bec256::detail {

// The destination is exact: readable neighbours do not grant writable slack.
// AVX-512 can assemble the whole bitstream in registers, then issue one masked
// store. Other profiles retain bounded scalar stitching of the same groups.
[[gnu::always_inline]] inline void emit_exact(const std::uint64_t *value,
                                              const std::uint64_t *width, std::uint8_t *out,
                                              unsigned bytes) {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    auto packed = assemble(_mm512_loadu_si512(value), _mm512_loadu_si512(width));
    _mm512_mask_storeu_epi8(out, (std::uint64_t{1} << bytes) - 1, packed);
#else
    stitch_exact(value, width, out, bytes);
#endif
}
} // namespace ikea::bec256::detail
