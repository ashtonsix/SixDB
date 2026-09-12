#pragma once
#include <ikea/tuplepack/detail/plan.h>
#include <ikea/tuplepack/detail/native/types.h>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <ikea/tuplepack/detail/native/memory.h>
#if defined(__aarch64__)
namespace ikea::tuplepack::native::native_detail {
template <unsigned I>
[[gnu::always_inline]] inline vector16 apply16(native_packet source, const detail::shuffle& p) {
    const auto index = vld1q_u8(p.index.data() + 16 * I);
    uint8x16_t value;
    if (p.routes <= 1)
        value = vqtbl1q_u8(source.a, index);
    else if (p.routes <= 3)
        value = vqtbl2q_u8({{source.a, source.b}}, index);
    else if (p.routes <= 7)
        value = vqtbl3q_u8({{source.a, source.b, source.c}}, index);
    else
        value = vqtbl4q_u8({{source.a, source.b, source.c, source.d}}, index);
    if (p.shifting)
        value = vshlq_u8(value, vld1q_s8(p.shift.data() + 16 * I));
    if (p.masking)
        value = vandq_u8(value, vld1q_u8(p.mask.data() + 16 * I));
    return value;
}
} // namespace ikea::tuplepack::native::native_detail
#endif
