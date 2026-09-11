#include "heads.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <stdexcept>
#include <vector>

namespace {
namespace trial = head_encode_experiment;
namespace sp = ikea::seriespack;
using function = void (*)(const void*,std::uint8_t*,std::uint8_t*,std::uint8_t*,std::size_t);
struct arm { const char* name; function call; };
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes = nullptr;
    explicit buffer(std::size_t n) {
        void* p = nullptr;
        require(posix_memalign(&p,64,std::max<std::size_t>(64,(n+63)&~std::size_t{63})) == 0,"allocation");
        bytes = static_cast<std::uint8_t*>(p); std::memset(bytes,0,n);
    }
    ~buffer() { std::free(bytes); }
    buffer(const buffer&) = delete;
};

// One shared payload function per target/shape/carrier is held unchanged across
// the whole-operation head arms. No checked endpoint/effect accounting is timed.
template<class Ops,unsigned W,unsigned T,class U>
[[gnu::noinline]] void payload(const U* in,std::uint8_t* out,std::size_t n) {
    constexpr auto G = T == 8 ? sp::geometry::local8 : sp::geometry::striped;
    __builtin_assume(n % T == 0);
    if constexpr (W != 0) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        if constexpr (Ops::bytes == 64) sp::avx512::encode_low_tiles<W,G>(in,out,n/T);
        else
#endif
            sp::avx2::encode_low_tiles<W,G>(in,out,n/T);
    }
}

template<class Ops,unsigned W,unsigned H,unsigned T,class U,unsigned Mode,bool Whole>
[[gnu::noinline]] void operation(const void* input,std::uint8_t* p,
    std::uint8_t* h0,std::uint8_t* h1,std::size_t n) {
    const auto* values = static_cast<const U*>(input);
    if constexpr (Whole) payload<Ops,W,T>(values,p,n);
    trial::encode<Ops,W,H,T,U,Mode>(values,n,h0,h1);
}

[[gnu::noinline]] double elapsed(function f,const void* in,std::uint8_t* p,
    std::uint8_t* h0,std::uint8_t* h1,std::size_t n,std::size_t passes) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for (std::size_t i=0;i<passes;++i) { f(in,p,h0,h1,n); asm volatile("":::"memory"); }
    return std::chrono::duration<double,std::nano>(clock::now()-start).count();
}
std::size_t checks = 0;
template<unsigned W,unsigned H,unsigned T,class U,bool Whole>
void run(std::size_t count,bool check_only) {
    constexpr auto G = T == 8 ? sp::geometry::local8 : sp::geometry::striped;
    constexpr auto arms = std::array{
        arm{"separate_avx2",operation<trial::avx2,W,H,T,U,0,Whole>},
        arm{"joint_tile_avx2",operation<trial::avx2,W,H,T,U,1,Whole>},
        arm{"joint_compact16_avx2",operation<trial::avx2,W,H,T,U,2,Whole>},
        arm{"joint_fulltile_control_avx2",operation<trial::avx2,W,H,T,U,3,Whole>},
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        arm{"separate_avx512",operation<trial::avx512,W,H,T,U,0,Whole>},
        arm{"joint_tile_avx512",operation<trial::avx512,W,H,T,U,1,Whole>},
        arm{"joint_compact32_avx512",operation<trial::avx512,W,H,T,U,2,Whole>},
        arm{"joint_fulltile_control_avx512",operation<trial::avx512,W,H,T,U,3,Whole>},
#endif
    };
    buffer input(count*sizeof(U)),payload_bytes(count/8*W),head0(count),head1(count);
    std::vector<U> oracle(count);
    std::vector<std::uint8_t> expected0(count),expected1(count),expected_payload(count/8*W);
    constexpr std::uint64_t mask = (std::uint64_t{1} << (W+H))-1;
    for (std::size_t i=0;i<count;++i) {
        std::uint64_t value=i+0x857263fabc519004ULL;
        value ^= value >> 17; value *= 0x9e3779b97f4a7c15ULL; value ^= value >> 31;
        const auto carrier = static_cast<U>(value & mask);
        oracle[i] = carrier;
        expected0[i] = std::uint8_t(std::uint64_t(carrier) >> (W+H-8));
        if constexpr (H == 16) expected1[i] = std::uint8_t(std::uint64_t(carrier) >> W);
    }
    std::memcpy(input.bytes,oracle.data(),count*sizeof(U));
    if constexpr (Whole && W != 0)
        for (std::size_t i=0;i<count/T;++i)
            sp::detail::encode_low_tile<W,G>(oracle.data()+i*T,expected_payload.data()+i*sp::payload_layout<W,G>::tile_bytes);
    const auto validate = [&](const arm& a) {
        std::memset(head0.bytes,0xa7,count); std::memset(head1.bytes,0xa7,count);
        a.call(input.bytes,payload_bytes.bytes,head0.bytes,head1.bytes,count);
        require(std::memcmp(head0.bytes,expected0.data(),count)==0,"head0 oracle");
        if constexpr (H == 16) require(std::memcmp(head1.bytes,expected1.data(),count)==0,"head1 oracle");
        else for (std::size_t i=0;i<count;++i) require(head1.bytes[i]==0xa7,"unused head1 changed");
        if constexpr (Whole && W != 0)
            require(std::memcmp(payload_bytes.bytes,expected_payload.data(),expected_payload.size())==0,"payload scalar equivalence");
        ++checks;
    };
    for (const auto& a:arms) validate(a);
    if (check_only) return;
    std::size_t passes=1;
    while (elapsed(arms[0].call,input.bytes,payload_bytes.bytes,head0.bytes,head1.bytes,count,passes)<20000000.0) passes*=2;
    // Sixteen rotations balance both the four- and eight-arm profile ceilings.
    for (unsigned repetition=0;repetition<16;++repetition)
        for (unsigned position=0;position<arms.size();++position) {
            const auto& a=arms[(repetition+position)%arms.size()];
            for (unsigned warm=0;warm<16;++warm) a.call(input.bytes,payload_bytes.bytes,head0.bytes,head1.bytes,count);
            const auto ns=elapsed(a.call,input.bytes,payload_bytes.bytes,head0.bytes,head1.bytes,count,passes);
            validate(a);
            std::printf("{\"width\":%u,\"payload_width\":%u,\"head_bits\":%u,\"tile_values\":%u,"
                "\"input_bits\":%zu,\"values\":%zu,\"scope\":\"%s\",\"arm\":\"%s\","
                "\"passes\":%zu,\"repetition\":%u,\"position\":%u,\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,"
                "\"entry_mod64\":%zu,\"input_page_offset\":%zu,\"payload_page_offset\":%zu,"
                "\"head0_page_offset\":%zu,\"head1_page_offset\":%zu}\n",
                W+H,W,H,T,sizeof(U)*8,count,Whole?"with_payload":"heads_only",a.name,passes,repetition,position,
                ns,ns/(double(passes)*count),reinterpret_cast<std::uintptr_t>(a.call)%64,
                reinterpret_cast<std::uintptr_t>(input.bytes)%4096,reinterpret_cast<std::uintptr_t>(payload_bytes.bytes)%4096,
                reinterpret_cast<std::uintptr_t>(head0.bytes)%4096,reinterpret_cast<std::uintptr_t>(head1.bytes)%4096);
        }
}

template<unsigned W,unsigned H,unsigned T,class U>
void scopes(std::size_t n,bool check_only) { run<W,H,T,U,false>(n,check_only); run<W,H,T,U,true>(n,check_only); }
int pin() {
    cpu_set_t allowed; CPU_ZERO(&allowed);
    require(sched_getaffinity(0,sizeof allowed,&allowed)==0,"get affinity");
    int cpu=-1;
    if (const auto* value=std::getenv("SIXDB_CPU")) cpu=std::atoi(value);
    else for (int i=0;i<CPU_SETSIZE;++i) if (CPU_ISSET(i,&allowed)) { cpu=i;break; }
    require(cpu>=0&&cpu<CPU_SETSIZE&&CPU_ISSET(cpu,&allowed),"allowed CPU");
    cpu_set_t one; CPU_ZERO(&one); CPU_SET(cpu,&one);
    require(sched_setaffinity(0,sizeof one,&one)==0,"set affinity"); return cpu;
}
}
int main(int argc,char** argv) {
    bool check_only=false,focus=false;
    for (int i=1;i<argc;++i) {
        if (std::strcmp(argv[i],"--check-only")==0) check_only=true;
        else if (std::strcmp(argv[i],"--focus")==0) focus=true;
        else require(false,"usage: head-encode-bench [--check-only] [--focus]");
    }
    std::printf("{\"diagnostic\":\"seriespack-head-encode\",\"cpu\":%d,\"repetitions\":16,"
                "\"shared_buffers\":true,\"residency\":\"unestablished\"}\n",pin());
    for (std::size_t n:{256u,8192u,65536u}) {
        scopes<1,16,256,std::uint64_t>(n,check_only);
        scopes<7,16,256,std::uint64_t>(n,check_only);
        scopes<40,16,8,std::uint64_t>(n,check_only);
        if (!focus) {
            scopes<1,16,8,std::uint64_t>(n,check_only); scopes<7,16,8,std::uint64_t>(n,check_only);
            scopes<8,16,8,std::uint64_t>(n,check_only); scopes<8,8,8,std::uint64_t>(n,check_only);
            scopes<1,16,256,std::uint32_t>(n,check_only); scopes<7,16,256,std::uint32_t>(n,check_only);
            scopes<1,16,256,std::uint16_t>(n,check_only); scopes<7,16,256,std::uint8_t>(n,check_only);
            scopes<8,8,8,std::uint16_t>(n,check_only);
        }
    }
    if (check_only) std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n",checks);
}
