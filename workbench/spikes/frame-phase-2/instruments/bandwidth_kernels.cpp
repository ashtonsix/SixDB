#include "bandwidth_kernels.h"
#include <array>
#include <utility>

namespace frame_instruments {
std::uint64_t scan_once(const std::uint64_t* values, std::size_t count) {
    std::uint64_t answer = 0;
    for (std::size_t i = 0; i < count; ++i) answer += values[i];
    return answer;
}

namespace {
template<std::size_t N, std::size_t... I>
void chase_body(word* cursors, std::uint64_t steps, std::index_sequence<I...>) {
    std::array<word, N> at;
    ((at[I] = cursors[I]), ...);
    for (std::uint64_t s = 0; s < steps; ++s)
        ((at[I] = *reinterpret_cast<const word*>(at[I])), ...);
    ((cursors[I] = at[I]), ...);
}
template<std::size_t N> void chase(word* cursors, std::uint64_t steps) {
    chase_body<N>(cursors, steps, std::make_index_sequence<N>{});
}
} // namespace
chase_fn chase_for(unsigned chains) {
    switch (chains) {
    case 1: return chase<1>;
    case 4: return chase<4>;
    case 16: return chase<16>;
    case 32: return chase<32>;
    case 64: return chase<64>;
    default: return nullptr;
    }
}
} // namespace frame_instruments
