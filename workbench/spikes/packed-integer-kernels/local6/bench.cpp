#include "regions.h"
#include "local.h"
#include <ikea/seriespack.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sched.h>
#include <stdexcept>
#include <vector>

namespace {
namespace sp=ikea::seriespack;
using function=void(*)(const std::uint8_t*,std::uint8_t*,std::size_t,const sp::bound_reader&);
struct arm { const char* name; function call; };
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes=nullptr;
    explicit buffer(std::size_t n) {
        void* p=nullptr;
        require(posix_memalign(&p,4096,(n+4095)&~std::size_t{4095})==0,"allocation");
        bytes=static_cast<std::uint8_t*>(p);std::memset(bytes,0,n);
    }
    ~buffer() {std::free(bytes);}
    buffer(const buffer&)=delete;
};

// The same object is linked at four raw-operation placements. The captured
// public core remains in .text and is fixed across those placement variants.
template<unsigned Mode>
[[gnu::noinline,gnu::section(".seriespack_local6_diag"),gnu::aligned(16)]]
void operation(const std::uint8_t* __restrict input,std::uint8_t* __restrict output,
    std::size_t n,const sp::bound_reader& reader) {
    if constexpr (Mode==0) reader.decode({0,n},sp::output_values{std::span(output,n)});
    else if constexpr (Mode==1) local6_experiment::decode<0>(input,output,n);
    else if constexpr (Mode==2) local6_experiment::decode<64>(input,output,n);
    else if constexpr (Mode==3) local6_experiment::decode<256>(input,output,n);
    else if constexpr (Mode==4) local6_experiment::decode<512>(input,output,n);
    else {
        __builtin_assume(n%256==0);
        for (;n;n-=256,input+=192,output+=256)
            ikea::integers::local_decode<6>(input,output);
    }
}
constexpr auto arms=std::array{
    arm{"public_captured",operation<0>},arm{"native_auto",operation<1>},
    arm{"controlled64",operation<2>},arm{"controlled256",operation<3>},
    arm{"controlled512",operation<4>},arm{"direct_predecessor",operation<5>}
};
[[gnu::noinline]] double elapsed(function f,const std::uint8_t* input,std::uint8_t* output,
    std::size_t n,const sp::bound_reader& reader,std::size_t passes) {
    using clock=std::chrono::steady_clock;
    const auto start=clock::now();
    for (std::size_t p=0;p<passes;++p) {f(input,output,n,reader);asm volatile("":::"memory");}
    return std::chrono::duration<double,std::nano>(clock::now()-start).count();
}
std::size_t checks=0;
void run(std::size_t n,unsigned output_offset,bool check_only) {
    buffer packed(n/8*6),storage(n+output_offset);
    auto* output=storage.bytes+output_offset;
    std::vector<std::uint8_t> expected(n);
    for (std::size_t i=0;i<n;++i) {
        std::uint64_t x=i+0x68d92c47abe305f1ULL;
        x^=x>>17;x*=0x9e3779b97f4a7c15ULL;x^=x>>31;
        const auto value=std::uint8_t(x&63);
        expected[i]=value;
        for (unsigned b=0;b<6;++b) packed.bytes[i/8*6+b]|=std::uint8_t((value>>b&1)<<(i%8));
    }
    const sp::description description{6,0,sp::geometry::local8};
    const auto view=sp::const_view::attach(description,n,
        {{std::span(reinterpret_cast<const std::byte*>(packed.bytes),n/8*6),6},{}});
    require(view.has_value(),"attach independent wire");
    const auto reader=sp::bind_reader(*view,sp::execution_target::avx512);
    require(reader.has_value(),"bind captured public reader");
    const auto validate=[&](const arm& a) {
        std::memset(output,0xa7,n);
        a.call(packed.bytes,output,n,*reader);
        require(std::memcmp(output,expected.data(),n)==0,"independent bit oracle");++checks;
    };
    for (const auto& a:arms) validate(a);
    if (check_only) return;
    std::size_t passes=1;
    while (elapsed(arms[1].call,packed.bytes,output,n,*reader,passes)<20000000.0) passes*=2;
    for (unsigned repetition=0;repetition<18;++repetition)
        for (unsigned position=0;position<arms.size();++position) {
            const auto& a=arms[(position+repetition)%arms.size()];
            for (unsigned warm=0;warm<16;++warm) a.call(packed.bytes,output,n,*reader);
            const auto ns=elapsed(a.call,packed.bytes,output,n,*reader,passes);
            validate(a);
            std::printf("{\"width\":6,\"carrier_bits\":8,\"values\":%zu,\"arm\":\"%s\","
                "\"passes\":%zu,\"repetition\":%u,\"position\":%u,\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,"
                "\"entry_mod64\":%zu,\"input_page_offset\":%zu,\"output_page_offset\":%zu}\n",
                n,a.name,passes,repetition,position,ns,ns/(double(passes)*n),
                reinterpret_cast<std::uintptr_t>(a.call)%64,
                reinterpret_cast<std::uintptr_t>(packed.bytes)%4096,
                reinterpret_cast<std::uintptr_t>(output)%4096);
        }
}
int pin() {
    cpu_set_t allowed;CPU_ZERO(&allowed);
    require(sched_getaffinity(0,sizeof allowed,&allowed)==0,"get affinity");
    int cpu=-1;
    if (const auto* value=std::getenv("SIXDB_CPU")) cpu=std::atoi(value);
    else for (int i=0;i<CPU_SETSIZE;++i) if (CPU_ISSET(i,&allowed)) {cpu=i;break;}
    require(cpu>=0&&cpu<CPU_SETSIZE&&CPU_ISSET(cpu,&allowed),"allowed CPU");
    cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);
    require(sched_setaffinity(0,sizeof one,&one)==0,"set affinity");return cpu;
}
}
int main(int argc,char** argv) {
    const bool check_only=argc==2&&std::strcmp(argv[1],"--check-only")==0;
    require(argc==1||check_only,"usage: local6-bench [--check-only]");
    std::printf("{\"diagnostic\":\"seriespack-local6-loop-grain\",\"cpu\":%d,\"repetitions\":18,"
        "\"shared_buffers\":true,\"residency\":\"unestablished\",\"profile\":\"full-avx512\"}\n",pin());
    for (std::size_t n:{256u,8192u,65536u})
        for (unsigned offset:{0u,64u,2048u}) run(n,offset,check_only);
    if (check_only) std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n",checks);
}
