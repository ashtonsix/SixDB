#pragma once
#include <ikea/seriespack/native_neon.h>

namespace byte_grain {
namespace sp = ikea::seriespack;
namespace bytes = sp::detail::neon::body_detail;

// The existing native eight-value body gather. Its admitted source is exactly
// 64 bytes; the result's low eight bytes hold the projected byte values.
[[gnu::always_inline]] inline uint8x16_t gather8(const std::uint64_t* input) {
    std::array<uint8x16_t, 4> source;
    sp::detail::static_for<4>([&](auto part) {
        source[part] = vld1q_u8(reinterpret_cast<const std::uint8_t*>(input) + part * 16);
    });
    static constexpr uint8x16_t select = {0,8,16,24,32,40,48,56,255,255,255,255,255,255,255,255};
    return bytes::table<0, 4>(source, select);
}

template<unsigned Values, bool JoinStores>
[[gnu::always_inline]] inline void region(const std::uint64_t* __restrict input,
                                         std::uint8_t* __restrict output) {
    static_assert(Values == 32 || Values == 64 || Values == 256);
    if constexpr (JoinStores) {
        sp::detail::static_for<Values / 16>([&](auto pair) {
            const auto a = gather8(input + pair * 16);
            const auto b = gather8(input + pair * 16 + 8);
            vst1q_u8(output + pair * 16, vcombine_u8(vget_low_u8(a), vget_low_u8(b)));
        });
    } else {
        sp::detail::static_for<Values / 8>([&](auto packet) {
            vst1_u8(output + packet * 8, vget_low_u8(gather8(input + packet * 8)));
        });
    }
}

template<unsigned Values, bool JoinStores>
[[gnu::noinline]] void array(const std::uint64_t* __restrict input,
                             std::uint8_t* __restrict output, std::size_t count) {
    __builtin_assume(count % Values == 0);
    // Keep the named region as the loop grain; changing it is an explicit arm.
#pragma clang loop unroll(disable)
    for (std::size_t i = 0; i < count; i += Values)
        region<Values, JoinStores>(input + i, output + i);
}

[[gnu::noinline]] inline void native8(const std::uint64_t* __restrict input,
                                     std::uint8_t* __restrict output, std::size_t count) {
    __builtin_assume(count % 8 == 0);
    sp::neon::encode_low_tiles<8, sp::geometry::local8>(input, output, count / 8);
}
}
