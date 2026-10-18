#include "early-wide-kernel.h"
int main() {
#if defined(__AVX512BW__) && defined(__AVX512DQ__)
    using namespace early;
    iterations=300000;repeats=7;
    std::puts("module,width,op,pattern,variant,metric,rep,ns_window,selected_fraction,first_resolved_fraction");
    sp::detail::each<5>([](auto op){wide_case<32,op,0>();wide_case<64,op,0>();wide_case<40,op,8>();});
    std::fprintf(stderr,"ZMM controls passed; checksum=%llu\n",(unsigned long long)observed);
#endif
}
