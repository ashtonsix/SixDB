#pragma once
#include "early-kernels.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace early {
constexpr unsigned windows=256;
extern unsigned iterations,repeats,only_width;
extern int only_op;
extern bool check_only;
extern std::string only_pattern,only_variant,only_module;
extern word observed;
struct Fixture {
    unsigned width,head;
    std::vector<std::uint8_t> storage,head_storage;
    std::span<std::uint8_t> bytes,heads;
    std::vector<word> truth,queries;
    std::string pattern;
    double selected=0,resolved=0;
    Fixture(unsigned k,unsigned h,const char* pattern);
};
bool oracle(unsigned op,word x,word q);
void describe(Fixture&,unsigned op);
inline unsigned next(unsigned cursor,word result,unsigned salt) {
    // All result bits feed the next address; eight chains are independent.
    return unsigned(((word(cursor+salt+1)*0xd6e8feb86659fd93ULL) ^
                     (result*0x9e3779b97f4a7c15ULL))>>56);
}
template<class Call> [[gnu::noinline]] void measure(const Fixture& f,unsigned op,
        const char* module,const char* variant,Call call) {
    if(check_only || (!only_variant.empty()&&only_variant!=variant) ||
       (!only_module.empty()&&only_module!=module)) return;
    for(bool serial:{true,false}) for(unsigned rep=0;rep<repeats;++rep) {
        std::array<unsigned,8> cursor{0,31,63,95,127,159,191,255};
        for(unsigned i=0;i<1024;++i) for(unsigned j=0;j<8;++j)
            cursor[j]=next(cursor[j],call(cursor[j]),i+j*0x9e37u);
        auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i) {
            if(serial) cursor[0]=next(cursor[0],call(cursor[0]),i);
            else sp::detail::each<8>([&](auto j){cursor[j]=next(cursor[j],call(cursor[j]),i+unsigned(j)*0x9e37u);});
        }
        auto end=std::chrono::steady_clock::now();
        for(auto c:cursor) observed+=c;
        double ns=std::chrono::duration<double,std::nano>(end-start).count()/(double(iterations)*(serial?1:8));
        std::printf("%s,%u,%u,%s,%s,%s,%u,%.6f,%.6f,%.6f\n",module,f.width,op,
            f.pattern.c_str(),variant,serial?"dependent":"independent8",rep,ns,f.selected,f.resolved);
    }
}
template<unsigned K,int Op,unsigned H,int Mode>
[[gnu::always_inline]] inline unsigned candidate(const std::uint8_t* p,const std::uint8_t* h,word q,unsigned active,unsigned w) {
    if constexpr(H==0) {
        if constexpr(Mode==0) return current<K,Op>(p,q,active);
        if constexpr(Mode==1) return fused<K,Op>(p,q,active);
        if constexpr(Mode==2) return progressive<K,Op,false>(p,q,active);
        if constexpr(Mode==3) return progressive<K,Op,true>(p,q,active);
        if constexpr(Mode==4) return gate<K,Op>(p,q,active);
    } else {
        if constexpr(Mode==0) return head_current<K,Op>(p,h,q,active,w);
        if constexpr(Mode==1) return head_split<K,Op,false,true>(p,h,q,active);
        if constexpr(Mode==2) return head_split<K,Op,false,false>(p,h,q,active);
        if constexpr(Mode==3) return head_split<K,Op,true,false>(p,h,q,active);
        if constexpr(Mode==4) return head_split<K,Op,true,true>(p,h,q,active);
    }
}
template<unsigned K,int Op,unsigned H,int Mode> void run_variant(Fixture& f) {
    auto call=[&](unsigned w) __attribute__((always_inline)) {
        asm("" : "+r"(w));
        return candidate<K,Op,H,Mode>(f.bytes.data()+w*2*(K-H),f.heads.data()+w*16,f.queries[w],65535,w);
    };
    constexpr word limit=~word{0}>>(64-K);
    for(unsigned w=0;w<windows;++w) {
        std::array<word,7> queries{f.queries[w],0,1,limit/2,limit,limit+1,~word{0}};
        for(auto q:queries) for(unsigned active:{0u,1u,0x8000u,0xa53cu,65535u}) {
            unsigned expected=0;
            for(unsigned lane=0;lane<16;++lane)
                if((active>>lane&1)&&oracle(Op,f.truth[w*16+lane],q)) expected|=1u<<lane;
            auto actual=candidate<K,Op,H,Mode>(f.bytes.data()+w*2*(K-H),f.heads.data()+w*16,q,active,w);
            if(actual!=expected) {
                std::fprintf(stderr,"FAIL K=%u H=%u op=%d mode=%d pattern=%s window=%u q=%llu active=%u actual=%u expected=%u\n",
                    K,H,Op,Mode,f.pattern.c_str(),w,(unsigned long long)q,active,actual,expected);
                std::abort();
            }
        }
    }
    const char* name=Mode==0?"current":Mode==1?"fused":Mode==2?"progressive":Mode==3?"early":H?"fused_early":"gate";
    measure(f,Op,H?"head8":"local",name,call);
}
template<unsigned K,int Op,unsigned H> void series_case() {
    if(only_op>=0 && only_op!=Op) return;
    if(!only_module.empty()&&only_module!=(H?"head8":"local")) return;
    for(const char* pattern:{"uniform","reject_first","shared_prefix","all_equal","one_match","half_match",
                            "mixed10","mixed50","ordered1","ordered99"}) {
        if(!only_pattern.empty() && only_pattern!=pattern) continue;
        Fixture f(K,H,pattern); describe(f,Op);
        run_variant<K,Op,H,0>(f); run_variant<K,Op,H,1>(f);
        run_variant<K,Op,H,2>(f); run_variant<K,Op,H,3>(f);
        run_variant<K,Op,H,4>(f);
    }
}
template<unsigned K> void series_width() {
    if(only_width && only_width!=K) return;
    sp::detail::each<5>([](auto op){
        series_case<K,op,0>();
        if constexpr(K>8) series_case<K,op,8>();
    });
    std::fprintf(stderr,"checked/measured SeriesPack K%u\n",K);
}
}
