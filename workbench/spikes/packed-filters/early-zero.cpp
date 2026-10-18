#include "early-zero-kernel.h"
namespace early {
template<unsigned K,int Op> void zero_case() {
    for(const char* pattern:{"uniform","reject_first","shared_prefix","mixed50","all_equal"}) {
        Fixture f(K,8,pattern);describe(f,Op);
        run_variant<K,Op,8,0>(f);run_variant<K,Op,8,1>(f);
        run_variant<K,Op,8,2>(f);run_variant<K,Op,8,3>(f);run_variant<K,Op,8,4>(f);
        auto call=[&](unsigned w) __attribute__((always_inline)) {
            asm("" : "+r"(w));
            return vector_zero<K,Op>(f.bytes.data()+w*2*(K-8),f.heads.data()+w*16,f.queries[w]);
        };
        constexpr auto limit=~word{0}>>(64-K);
        for(unsigned w=0;w<windows;++w) for(word q:{f.queries[w],word{0},limit,limit+1,~word{0}}) {
            unsigned expected=0;
            for(unsigned lane=0;lane<16;++lane) expected|=unsigned(oracle(Op,f.truth[w*16+lane],q))<<lane;
            if(vector_zero<K,Op>(f.bytes.data()+w*2*(K-8),f.heads.data()+w*16,q)!=expected) std::abort();
        }
        measure(f,Op,"head8","vector_zero",call);
    }
}
}
int main(int argc,char**) {
    using namespace early;iterations=500000;repeats=7;
    check_only=argc>1;
    std::puts("module,width,op,pattern,variant,metric,rep,ns_window,selected_fraction,first_resolved_fraction");
    zero_case<12,0>();zero_case<64,0>();zero_case<12,1>();zero_case<64,1>();
    std::fprintf(stderr,"vector zero checks passed; checksum=%llu\n",(unsigned long long)observed);
}
