#include "early-bench.h"
#include <random>
namespace early {
unsigned iterations=30000,repeats=5,only_width=0;
int only_op=-1;
bool check_only=false;
std::string only_pattern,only_variant,only_module;
word observed=0;
bool oracle(unsigned op,word x,word q) {
    switch(op) {case 0:return x==q;case 1:return x<q;case 2:return x<=q;case 3:return x>q;default:return x>=q;}
}
Fixture::Fixture(unsigned k,unsigned h,const char* name)
    :width(k),head(h),storage(2*(k-h)*windows+63),head_storage(16*windows+63),truth(16*windows),queries(windows),pattern(name) {
    bytes=std::span(storage).subspan((-std::uintptr_t(storage.data()))&63,2*(k-h)*windows);
    heads=std::span(head_storage).subspan((-std::uintptr_t(head_storage.data()))&63,16*windows);
    std::mt19937_64 rng(0xced012341234ULL+k);
    word limit=~word{0}>>(64-k);
    unsigned first=h?8:(k>=8?8:1),lower=k-first;
    word lowmask=lower?~word{0}>>(64-lower):0;
    for(unsigned w=0;w<windows;++w) {
        word q=rng()&limit;
        if(pattern=="ordered1") q=limit/100;
        if(pattern=="ordered99") q=limit-limit/100;
        queries[w]=q;
        bool hard=pattern=="shared_prefix" || (pattern=="mixed10"&&rng()%10==0) ||
                  (pattern=="mixed50"&&rng()%2==0);
        for(unsigned lane=0;lane<16;++lane) {
            word x=rng()&limit;
            if(hard) x=(q&~lowmask)|(x&lowmask);
            else if(pattern=="reject_first"||pattern=="one_match"||pattern=="half_match"||pattern=="mixed10"||pattern=="mixed50")
                x=((q^(word{1}<<(k-1)))&~lowmask)|(x&lowmask);
            if(pattern=="all_equal"||(pattern=="one_match"&&lane==w%16)||
               (pattern=="half_match"&&((lane+w)%16)<8)) x=q;
            truth[w*16+lane]=x;
            heads[w*16+lane]=h?x>>(k-h):0;
            unsigned payload=k-h;
            auto* tile=bytes.data()+w*2*payload+(lane/8)*payload;
            for(unsigned b=0;b<payload/8;++b) tile[(lane%8)*(payload/8)+b]=x>>(payload%8+8*b);
            for(unsigned b=0;b<payload%8;++b) tile[8*(payload/8)+b]|=((x>>b)&1)<<(lane%8);
        }
    }
}
void describe(Fixture& f,unsigned op) {
    unsigned selected=0,resolved=0;
    unsigned first=f.head?8:(f.width>=8?8:1),shift=f.width-first;
    // Actual deterministic dependent traversal, outside timed code. These are
    // trace counts, not hardware branch/retirement counters.
    unsigned cursor=0;
    for(unsigned i=0;i<32768;++i) {
        unsigned mask=0,equal=0;
        auto q=f.queries[cursor];
        for(unsigned lane=0;lane<16;++lane) {
            auto x=f.truth[cursor*16+lane];
            mask|=unsigned(oracle(op,x,q))<<lane;
            equal+=((x>>shift)==(q>>shift));
        }
        selected+=std::popcount(mask); resolved+=equal==0;
        cursor=next(cursor,mask,i);
    }
    f.selected=double(selected)/(32768*16); f.resolved=double(resolved)/32768;
}
void widths_a();void widths_b();void widths_c();void widths_d();
void tuples1();void tuples3();void tuples5();void tuples8();
}
#ifndef EARLY_NO_MAIN
int main(int argc,char** argv) {
    using namespace early;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        if(a=="--check") check_only=true;
        else if(a=="--width") only_width=std::atoi(argv[++i]);
        else if(a=="--op") only_op=std::atoi(argv[++i]);
        else if(a=="--pattern") only_pattern=argv[++i];
        else if(a=="--variant") only_variant=argv[++i];
        else if(a=="--module") only_module=argv[++i];
        else if(a=="--iterations") iterations=std::atoi(argv[++i]);
        else if(a=="--repeats") repeats=std::atoi(argv[++i]);
        else return 2;
    }
    std::puts("module,width,op,pattern,variant,metric,rep,ns_window,selected_fraction,first_resolved_fraction");
    widths_a();widths_b();widths_c();widths_d();tuples1();tuples3();tuples5();tuples8();
    std::fprintf(stderr,"oracle checks passed; checksum=%llu\n",(unsigned long long)observed);
}
#endif
