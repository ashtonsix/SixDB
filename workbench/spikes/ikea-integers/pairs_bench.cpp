#include "scan_pairs.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <vector>

using namespace ikea::integers;
#if defined(__AVX512BW__)
struct Buffer {
    struct alignas(64) Lane {std::array<uint8_t,64> bytes;bool operator==(const Lane&) const=default;};
    std::vector<Lane> lanes;
    explicit Buffer(unsigned bytes):lanes(bytes/64) {assert(bytes%64==0);}
    uint8_t* data() {return reinterpret_cast<uint8_t*>(lanes.data());}
    unsigned size() const {return unsigned(lanes.size()*64);}
    bool operator==(const Buffer&) const=default;
};
using Fn=void(*)(const uint8_t*,uint8_t*);
template<unsigned K,bool Wide,bool Encode> __attribute__((noinline))
void kernel(const uint8_t* in,uint8_t* out) {
    if constexpr(Wide&&Encode) scan_pairs_encode<K,512>(in,out);
    if constexpr(Wide&&!Encode) scan_pairs_decode<K,512>(in,out);
    if constexpr(!Wide&&Encode) scan_encode<K,512>(in,out);
    if constexpr(!Wide&&!Encode) scan_decode<K,512>(in,out);
}
__attribute__((noinline)) void repeat(Fn fn,const uint8_t* in,uint8_t* out,unsigned in_stride,unsigned out_stride,unsigned tiles,unsigned rounds) {
    asm volatile("" : "+r"(fn) : : "memory");
    for(unsigned r=0;r<rounds;++r) for(unsigned t=0;t<tiles;++t) fn(in+t*in_stride,out+t*out_stride);
}
template<unsigned K> void measure() {
    constexpr unsigned tiles=512,rounds=8192;
    Buffer values(tiles*512),packed(tiles*64*K),other(packed.size()),decoded(values.size());
    uint64_t state=813675;
    for(unsigned i=0;i<values.size();++i) {state^=state<<13;state^=state>>7;state^=state<<17;values.data()[i]=state&((1u<<K)-1);}
    for(unsigned t=0;t<tiles;++t) {
        scan_encode<K,512>(values.data()+t*512,packed.data()+t*64*K);
        scan_pairs_encode<K,512>(values.data()+t*512,other.data()+t*64*K);
        scan_pairs_decode<K,512>(packed.data()+t*64*K,decoded.data()+t*512);
    }
    assert(packed==other&&values==decoded);
    for(bool encode:{false,true}) for(bool wide:{false,true}) {
        const Fn fn=encode?(wide?kernel<K,true,true>:kernel<K,false,true>):(wide?kernel<K,true,false>:kernel<K,false,false>);
        auto* in=encode?values.data():packed.data();auto* out=encode?other.data():decoded.data();
        const unsigned input_stride=encode?512:64*K,output_stride=encode?64*K:512;
        repeat(fn,in,out,input_stride,output_stride,tiles,1);
        for(unsigned rep=0;rep<5;++rep) {
            const auto start=std::chrono::steady_clock::now();
            repeat(fn,in,out,input_stride,output_stride,tiles,rounds);
            const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
            assert(encode?packed==other:values==decoded);
            std::printf("%u,%s,%u,%u,%u,%u,%.8f\n",K,encode?"encode":"decode",wide?64:32,rep,tiles,rounds,ns/(tiles*rounds));
        }
    }
}
#endif
int main() {
#if defined(__AVX512BW__)
    cpu_set_t allowed;CPU_ZERO(&allowed);assert(sched_getaffinity(0,sizeof(allowed),&allowed)==0);
    unsigned cpu=0;while(!CPU_ISSET(cpu,&allowed))++cpu;
    cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);assert(sched_setaffinity(0,sizeof(one),&one)==0);
    std::puts("width,operation,native_bytes,repetition,tiles,rounds,ns_per512values");
    measure<1>();measure<2>();measure<4>();
#else
    std::puts("AVX-512 paired-stripe implementation not applicable to this target");
#endif
}
