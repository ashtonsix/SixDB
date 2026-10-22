// Scheduling diagnostics for literal admitted paths, never benchmark results.
#include <ikea/intervals/detail/native.h>
#include <ikea/boundlist/detail/prefix.h>
#include "control/rejected_native.h"
extern "C" {
unsigned prefix47_before(const std::uint8_t *p,unsigned stride) {
    return ikea::boundlist::detail::prefix16_native(p,16)+
           ikea::boundlist::detail::prefix16_native(p+stride,16)+
           ikea::boundlist::detail::prefix16_native(p+2*stride,15);
}
unsigned prefix47_accumulate(const std::uint8_t *p,unsigned stride) {
    return intervals_rejected::prefix<64>(p,stride,47);
}
unsigned prefix47_current(const std::uint8_t *p,unsigned stride) {
#if defined(__aarch64__) || defined(__AVX2__)
    return ikea::intervals::native_detail::prefix64(p,stride,47);
#else
    return ikea::intervals::native_detail::prefix_serial<64>(p,stride,47);
#endif
}
#if defined(__aarch64__)
unsigned owner64_tournament(const std::uint8_t *p,unsigned stride,unsigned offset) {
    return intervals_rejected::owner64_tournament(p,stride,offset);
}
#endif
unsigned directory4_before(const std::uint16_t *p,unsigned n,std::uint16_t q) {
    return ikea::boundlist::detail::checkpoint_boundary<ikea::boundlist::edge::after,4>(p,n,q);
}
unsigned directory4_rejected(const std::uint16_t *p,unsigned n,std::uint16_t q) {
    return intervals_rejected::checkpoint_boundary<ikea::boundlist::edge::after,4>(p,n,q);
}
}
