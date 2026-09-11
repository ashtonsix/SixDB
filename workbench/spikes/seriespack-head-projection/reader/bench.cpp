#include "region.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <stdexcept>
#include <vector>
namespace {
namespace trial = headed_region_experiment;
using function = void (*)(const std::uint8_t*,const std::uint8_t*,const std::uint8_t*,void*,std::size_t);
struct arm { const char* name; function call; };
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes=nullptr;
    explicit buffer(std::size_t n) {
        void* p=nullptr;
        require(posix_memalign(&p,64,(n+63)&~std::size_t{63})==0,"allocation");
        bytes=static_cast<std::uint8_t*>(p);std::memset(bytes,0,n);
    }
    ~buffer(){std::free(bytes);}
    buffer(const buffer&)=delete;
};
// One input section permits linker-only relocation of every timed function.
// The identical object is linked with 0/16/32/48 bytes before this section.
template<unsigned W,unsigned H,class UInt,unsigned Mode>
[[gnu::noinline,gnu::section(".seriespack_headed_diag"),gnu::aligned(16)]]
void operation(const std::uint8_t* p,const std::uint8_t* h0,const std::uint8_t* h1,void* out,std::size_t n) {
    trial::decode<W,H,UInt,Mode>(p,h0,h1,static_cast<UInt*>(out),n);
}
[[gnu::noinline]] double elapsed(function f,const std::uint8_t* p,const std::uint8_t* h0,const std::uint8_t* h1,
    void* out,std::size_t count,std::size_t passes) {
    using clock=std::chrono::steady_clock;
    const auto start=clock::now();
    for(std::size_t i=0;i<passes;++i){f(p,h0,h1,out,count);asm volatile("":::"memory");}
    return std::chrono::duration<double,std::nano>(clock::now()-start).count();
}
std::size_t checks=0;
template<unsigned W,unsigned H,class UInt>
void run(std::size_t count,bool check_only) {
    constexpr auto arms=std::array{
        arm{"tile8_avx2",operation<W,H,UInt,0>},
        arm{"region32_avx2",operation<W,H,UInt,1>},
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        arm{"tile8_avx512",operation<W,H,UInt,2>},
        arm{"region32_avx512",operation<W,H,UInt,3>},
        arm{"region64_avx512",operation<W,H,UInt,4>},
#endif
    };
    buffer payload(count/8*W),head0(count),head1(count),output(count*sizeof(UInt));
    std::vector<UInt> oracle(count);
    for(std::size_t i=0;i<count;++i){
        std::uint64_t value=i+0x38751299ab58fd02ULL;
        value^=value>>17;value*=0x9e3779b97f4a7c15ULL;value^=value>>31;
        value&=(std::uint64_t{1}<<(W+H))-1;
        oracle[i]=static_cast<UInt>(value);
        for(unsigned bit=0;bit<W;++bit)payload.bytes[i/8*W+bit]|=std::uint8_t((value>>bit&1)<<(i%8));
        head0.bytes[i]=static_cast<std::uint8_t>(value>>(W+H-8));
        if constexpr(H==16)head1.bytes[i]=static_cast<std::uint8_t>(value>>W);
    }
    const auto validate=[&](const arm& a){
        a.call(payload.bytes,head0.bytes,head1.bytes,output.bytes,count);
        require(std::memcmp(output.bytes,oracle.data(),count*sizeof(UInt))==0,"oracle mismatch");++checks;
    };
    for(const auto& a:arms)validate(a);
    if(check_only)return;
    std::size_t passes=1;
    while(elapsed(arms[0].call,payload.bytes,head0.bytes,head1.bytes,output.bytes,count,passes)<20000000.0)passes*=2;
    for(unsigned repetition=0;repetition<10;++repetition)
        for(unsigned position=0;position<arms.size();++position){
            const auto& a=arms[(position+repetition)%arms.size()];
            for(unsigned warm=0;warm<16;++warm)a.call(payload.bytes,head0.bytes,head1.bytes,output.bytes,count);
            const auto ns=elapsed(a.call,payload.bytes,head0.bytes,head1.bytes,output.bytes,count,passes);
            validate(a);
            std::printf("{\"width\":%u,\"payload_width\":%u,\"head_bits\":%u,\"carrier_bits\":%zu,"
                "\"values\":%zu,\"arm\":\"%s\",\"passes\":%zu,\"repetition\":%u,\"position\":%u,"
                "\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,\"function_address_mod64\":%zu,"
                "\"payload_page_offset\":%zu,\"head0_page_offset\":%zu,\"head1_page_offset\":%zu,\"output_page_offset\":%zu}\n",
                W+H,W,H,sizeof(UInt)*8,count,a.name,passes,repetition,position,ns,ns/(double(passes)*count),
                reinterpret_cast<std::uintptr_t>(a.call)%64,reinterpret_cast<std::uintptr_t>(payload.bytes)%4096,
                reinterpret_cast<std::uintptr_t>(head0.bytes)%4096,reinterpret_cast<std::uintptr_t>(head1.bytes)%4096,reinterpret_cast<std::uintptr_t>(output.bytes)%4096);
        }
}
int pin(){
    cpu_set_t allowed;CPU_ZERO(&allowed);require(sched_getaffinity(0,sizeof allowed,&allowed)==0,"get affinity");
    int cpu=-1;
    if(const auto* value=std::getenv("SIXDB_CPU"))cpu=std::atoi(value);
    else for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&allowed)){cpu=i;break;}
    require(cpu>=0&&cpu<CPU_SETSIZE&&CPU_ISSET(cpu,&allowed),"allowed CPU");
    cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);require(sched_setaffinity(0,sizeof one,&one)==0,"set affinity");return cpu;
}
}
int main(int argc,char** argv){
    bool check_only=false,focus=false;
    for(int i=1;i<argc;++i){
        if(std::strcmp(argv[i],"--check-only")==0)check_only=true;
        else if(std::strcmp(argv[i],"--focus-k10")==0)focus=true;
        else require(false,"usage: headed-bench [--check-only] [--focus-k10]");
    }
    const auto cpu=pin();
    std::printf("{\"diagnostic\":\"seriespack-headed-local-regions\",\"cpu\":%d,\"focus_k10\":%s,"
        "\"repetitions\":10,\"shared_buffers\":true,\"residency\":\"unestablished\"}\n",cpu,focus?"true":"false");
    for(std::size_t count:{256u,8192u,65536u}){
        run<2,8,std::uint16_t>(count,check_only);
        if(!focus){
            run<1,8,std::uint16_t>(count,check_only);run<5,8,std::uint16_t>(count,check_only);
            run<7,8,std::uint16_t>(count,check_only);run<2,8,std::uint32_t>(count,check_only);
            run<2,8,std::uint64_t>(count,check_only);run<1,16,std::uint32_t>(count,check_only);
            run<2,16,std::uint32_t>(count,check_only);run<5,16,std::uint32_t>(count,check_only);
            run<7,16,std::uint32_t>(count,check_only);run<2,16,std::uint64_t>(count,check_only);
        }
    }
    if(check_only)std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n",checks);
}
