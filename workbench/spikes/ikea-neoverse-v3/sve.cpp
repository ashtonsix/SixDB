#include "kernels.h"
#include <arm_sve.h>
#include <arm_neon_sve_bridge.h>
namespace v3_spike {
[[gnu::always_inline]] inline uint8x16x4_t load_sve(const bc::source& input) {
    if (input.storage().size() >= 64)
        return vld1q_u8_x4(reinterpret_cast<const std::uint8_t*>(input.storage().data()));
    const auto* data = reinterpret_cast<const std::uint8_t*>(input.storage().data());
    const unsigned bytes = input.bytes();
    // Avoid forming pointers beyond the admitted C++ object, even for inactive lanes.
    auto part = [&](unsigned offset) {
        const auto pg = svwhilelt_b8(std::uint64_t(offset), std::uint64_t(bytes));
        const auto* address = offset < bytes ? data + offset : data;
        return svget_neonq(svld1_u8(pg, address));
    };
    return {{part(0), part(16), part(32), part(48)}};
}
[[gnu::noinline]] std::uint64_t consume_sve(const bc::source& a, const bc::source& b,
                                          const bc::byte* qa, const bc::byte* qb) {
    return finish(bc::native::join(decode_loaded<0>(start(load_sve(a), a)),
                                   decode_loaded<0>(start(load_sve(b), b))), qa, qb);
}
[[gnu::noinline]] std::uint64_t consume_sve_interleaved(const bc::source& a, const bc::source& b,
                                                      const bc::byte* qa, const bc::byte* qb) {
    return finish(interleaved(start(load_sve(a), a), start(load_sve(b), b)), qa, qb);
}
}
