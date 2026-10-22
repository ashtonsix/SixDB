#pragma once
#include "kernels.h"
#include <array>
namespace frame_filter {
template <unsigned H, unsigned First> struct catalogue {
    static const std::array<operations, 8> entries;
};
#define FRAME_CATALOGUE(H, K) template <> const std::array<operations, 8> catalogue<H, K>::entries;
#define FRAME_LAYOUT(H) \
    FRAME_CATALOGUE(H, 1) FRAME_CATALOGUE(H, 9) FRAME_CATALOGUE(H, 17) FRAME_CATALOGUE(H, 25) \
    FRAME_CATALOGUE(H, 33) FRAME_CATALOGUE(H, 41) FRAME_CATALOGUE(H, 49) FRAME_CATALOGUE(H, 57)
FRAME_LAYOUT(0)
FRAME_LAYOUT(8)
FRAME_LAYOUT(16)
#undef FRAME_LAYOUT
#undef FRAME_CATALOGUE
}
