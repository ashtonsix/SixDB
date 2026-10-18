#include "early-kernels.h"
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>
namespace early {
template<unsigned K> void guarded() {
    auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto* allocation=static_cast<std::uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(allocation!=MAP_FAILED);
    assert(!mprotect(allocation+page,page,PROT_NONE));
    auto* p=allocation+page-2*K;
    std::array<word,16> values{};
    constexpr word limit=~word{0}>>(64-K);
    for(unsigned lane=0;lane<16;++lane) {
        word x=(word(lane+1)*0x9e3779b97f4a7c15ULL)&limit;values[lane]=x;
        auto* tile=p+(lane/8)*K;
        for(unsigned b=0;b<K/8;++b) tile[(lane%8)*(K/8)+b]=x>>(K%8+b*8);
        for(unsigned b=0;b<K%8;++b) tile[8*(K/8)+b]|=((x>>b)&1)<<(lane%8);
    }
    asm volatile("" : "+r"(p) :: "memory");
    sp::detail::each<5>([&](auto op){
        constexpr auto predicate=static_cast<C>(decltype(op)::value);
        for(word q:{word(0),word(1),limit/2,limit,limit+1,~word(0)})
            for(unsigned a:{0u,1u,0xa53cu,65535u}) {
                unsigned expect=0;
                for(unsigned lane=0;lane<16;++lane)
                    if((a>>lane&1)&&ikea::compare<predicate>(values[lane],q)) expect|=1u<<lane;
                assert((progressive<K,op,false>(p,q,a)==expect));
                assert((progressive<K,op,true>(p,q,a)==expect));
                assert((fused<K,op>(p,q,a)==expect));
                assert((gate<K,op>(p,q,a)==expect));
            }
        auto* inaccessible=allocation+page;
        assert((progressive<K,op,true>(inaccessible,0,0)==0));
        if constexpr(K>8) {
            alignas(64) std::array<std::uint8_t,16> heads{};
            word q=word(255)<<(K-8);
            auto* head_pointer=heads.data();
            asm volatile("" : "+r"(head_pointer), "+r"(q) :: "memory");
            auto expect=(op==1||op==2)?65535u:0u;
            assert((head_split<K,op,true,false>(inaccessible,head_pointer,q,65535)==expect));
            assert((head_split<K,op,true,true>(inaccessible,head_pointer,q,65535)==expect));
        }
    });
    assert(!munmap(allocation,page*2));
}
}
int main() {
    using namespace early;
    guarded<1>();guarded<3>();guarded<7>();guarded<8>();guarded<9>();guarded<12>();
    guarded<15>();guarded<16>();guarded<24>();guarded<31>();guarded<32>();guarded<40>();
    guarded<48>();guarded<56>();guarded<63>();guarded<64>();
}
