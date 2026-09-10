#include "prepared.h"
#include "transport.h"

namespace ikea::integers::composition {
template<class Tail,ParentKind Parent> std::uint64_t run_inline(const Binding& b) {
    std::uint64_t acc=0;
    for(std::size_t tile=0;tile<b.tiles;++tile) {
        NativeOps<Tail,Parent> ops{b.first+96*tile};
        for(unsigned group=0;group<64;group+=16)
            acc+=filtered_sum16(ops,Column<Tail>{0,{}},group,b.cutoff);
    }
    return acc;
}
extern "C" __attribute__((noinline)) std::uint64_t ikea_i12_inline_local(const void* p) {
    return run_inline<Local4,ParentKind::packets8>(*static_cast<const Binding*>(p));
}
extern "C" __attribute__((noinline)) std::uint64_t ikea_i12_inline_scan(const void* p) {
    return run_inline<Scan4,ParentKind::body64_tail32>(*static_cast<const Binding*>(p));
}
extern "C" __attribute__((noinline)) std::uint64_t ikea_i12_inline_scan_middle(const void* p) {
    return run_inline<Scan4,ParentKind::body32_tail32_body32>(*static_cast<const Binding*>(p));
}
extern "C" __attribute__((noinline)) std::uint64_t ikea_i12_cps(const void* p) {
    const auto& b=*static_cast<const Binding*>(p);
    const auto first=b.first;const auto tiles=b.tiles;const auto cutoff=b.cutoff;
    const auto* program=b.program->stages.data();const auto entry=program->execute;
    std::uint64_t acc=0;
    for(std::size_t tile=0;tile<tiles;++tile) {
        for(unsigned group=0;group<64;group+=16) {
#if defined(__aarch64__)
            const Values16 unused{vdupq_n_u16(0),vdupq_n_u16(0)};
#else
            const auto unused=_mm256_setzero_si256();
#endif
            acc=entry(program+1,first+96*tile,group,cutoff,acc,0,I12_VALUES_EXPAND(unused));
        }
    }
    return acc;
}
} // namespace ikea::integers::composition
