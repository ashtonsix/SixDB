#pragma once
#include "projected.h"

namespace local4_experiment {
namespace sp = ikea::seriespack;

template<bool Projected>
[[gnu::always_inline]] inline void write32(std::uint8_t* out, __m256i values) {
    if constexpr (Projected)
        sp::detail::avx2::encode_body<4,8>(out, sp::avx2::local4_projected_transpose(values));
    else sp::avx2::write_local_region32<4>(out, values);
}

// Mode0 is the current dense helper; Mode1 changes only its region writer.
// Modes2/3 group two current/projected regions per loop as separate grain
// controls. They are not the proposed production change.
template<unsigned Mode, std::unsigned_integral U>
[[gnu::always_inline]] inline void dense(const U* input, std::uint8_t* out, std::size_t tiles) {
    static_assert(Mode < 4);
    if constexpr (Mode == 0) sp::avx2::encode_low_tiles<4,sp::geometry::local8>(input,out,tiles);
    else {
        std::size_t tile = 0;
        if constexpr (Mode >= 2) {
#pragma clang loop unroll(disable)
            for (; tiles - tile >= 8; tile += 8) {
                write32<Mode == 3>(out + tile * 4,
                    sp::avx2::native_detail::load_working<sizeof(U),1>(reinterpret_cast<const std::uint8_t*>(input + tile * 8)));
                write32<Mode == 3>(out + (tile + 4) * 4,
                    sp::avx2::native_detail::load_working<sizeof(U),1>(reinterpret_cast<const std::uint8_t*>(input + (tile + 4) * 8)));
            }
        }
        // Match the original source loop without an added unroll directive.
        for (; tiles - tile >= 4; tile += 4)
            write32<Mode == 1 || Mode == 3>(out + tile * 4,
                sp::avx2::native_detail::load_working<sizeof(U),1>(reinterpret_cast<const std::uint8_t*>(input + tile * 8)));
        for (; tile < tiles; ++tile)
            sp::avx2::encode_low_tile<4,sp::geometry::local8>(input + tile * 8,out + tile * 4);
    }
}

template<unsigned Mode, std::unsigned_integral U>
[[gnu::always_inline]] inline void encode(const U* __restrict input, std::uint8_t* __restrict out,
    std::size_t n, std::size_t stride = 4) {
    const auto full = n / 8;
    if (stride == 4) dense<Mode>(input,out,full);
    else for (std::size_t tile = 0; tile < full; ++tile)
        sp::avx2::encode_low_tile<4,sp::geometry::local8>(input + tile * 8,out + tile * stride);
    if (const auto left = n % 8; left != 0) {
        std::array<U,8> boundary{};
        std::memcpy(boundary.data(),input + full * 8,left * sizeof(U));
        sp::avx2::encode_low_tile<4,sp::geometry::local8>(boundary.data(),out + full * stride);
    }
}
}
