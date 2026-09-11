#include "array.h"
#include "local.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <stdexcept>
#include <vector>

namespace {
using function = void (*)(const void*,std::uint8_t*,std::size_t);
struct arm { const char* name; function call; };
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct buffer {
    std::uint8_t* bytes = nullptr;
    explicit buffer(std::size_t n) {
        void* p = nullptr;
        require(posix_memalign(&p,64,(n+63)&~std::size_t{63}) == 0,"allocation");
        bytes = static_cast<std::uint8_t*>(p);
        std::memset(bytes,0,n);
    }
    ~buffer() { std::free(bytes); }
    buffer(const buffer&) = delete;
};

template<unsigned Mode,class U>
[[gnu::noinline]] void operation(const void* __restrict input,std::uint8_t* __restrict output,std::size_t n) {
    local4_experiment::encode<Mode>(static_cast<const U*>(input),output,n);
}

// Same fixed 256-value predecessor helper and array contract as the retained
// main comparator. There is no extra per-cell call, bridge, or narrowing array.
[[gnu::noinline]] void predecessor(const void* __restrict input,std::uint8_t* __restrict output,std::size_t n) {
    const auto* in = static_cast<const std::uint8_t*>(input);
    __builtin_assume(n % 256 == 0);
    for (; n; n -= 256,in += 256,output += 128)
        ikea::integers::local_encode<4>(in,output);
}

[[gnu::noinline]] double elapsed(function f,const void* input,std::uint8_t* output,std::size_t n,std::size_t passes) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for (std::size_t i=0;i<passes;++i) { f(input,output,n); asm volatile("":::"memory"); }
    return std::chrono::duration<double,std::nano>(clock::now()-start).count();
}
std::size_t checks = 0;
template<class U>
void run(std::size_t n,bool check_only) {
    std::vector<arm> arms{
        {"current32",operation<0,U>},
        {"projected32",operation<1,U>},
        {"current64_grain_control",operation<2,U>},
        {"projected64_grain_control",operation<3,U>}
    };
    if constexpr (sizeof(U)==1) arms.push_back({"direct_predecessor",predecessor});
    buffer input(n*sizeof(U)),output(n/2);
    std::vector<std::uint8_t> expected(n/2,0);
    for (std::size_t i=0;i<n;++i) {
        std::uint64_t x = i+0x4e9c76d2b1358a0fULL;
        x ^= x >> 17; x *= 0x9e3779b97f4a7c15ULL; x ^= x >> 31;
        const U value = static_cast<U>(x & 15);
        std::memcpy(input.bytes+i*sizeof(U),&value,sizeof(U));
        for (unsigned b=0;b<4;++b)
            expected[i/8*4+b] |= std::uint8_t((std::uint64_t(value)>>b&1)<<(i%8));
    }
    const auto validate = [&](const arm& a) {
        std::memset(output.bytes,0xa7,n/2);
        a.call(input.bytes,output.bytes,n);
        require(std::memcmp(output.bytes,expected.data(),n/2)==0,"independent bit oracle");
        ++checks;
    };
    for (const auto& a:arms) validate(a);
    if (check_only) return;
    std::size_t passes=1;
    while (elapsed(arms[0].call,input.bytes,output.bytes,n,passes)<20000000.0) passes*=2;
    // Twenty rotations balance both four-carrier arms and the five u8 arms.
    for (unsigned repetition=0;repetition<20;++repetition)
        for (unsigned position=0;position<arms.size();++position) {
            const auto& a=arms[(repetition+position)%arms.size()];
            for (unsigned warm=0;warm<16;++warm) a.call(input.bytes,output.bytes,n);
            const auto ns=elapsed(a.call,input.bytes,output.bytes,n,passes);
            validate(a);
            std::printf("{\"width\":4,\"input_bits\":%zu,\"values\":%zu,\"arm\":\"%s\","
                "\"passes\":%zu,\"repetition\":%u,\"position\":%u,\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,"
                "\"entry_mod64\":%zu,\"input_page_offset\":%zu,\"output_page_offset\":%zu}\n",
                sizeof(U)*8,n,a.name,passes,repetition,position,ns,ns/(double(passes)*n),
                reinterpret_cast<std::uintptr_t>(a.call)%64,
                reinterpret_cast<std::uintptr_t>(input.bytes)%4096,
                reinterpret_cast<std::uintptr_t>(output.bytes)%4096);
        }
}
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
    bool check_only=false;
    for (int i=1;i<argc;++i) {
        if (std::strcmp(argv[i],"--check-only")==0) check_only=true;
        else require(false,"usage: local4-projection-bench [--check-only]");
    }
    std::printf("{\"diagnostic\":\"seriespack-local4-projection\",\"cpu\":%d,\"repetitions\":20,"
        "\"shared_buffers\":true,\"residency\":\"unestablished\",\"profile\":\"avx2-only\"}\n",pin());
    for (std::size_t n:{256u,8192u,65536u}) {
        run<std::uint8_t>(n,check_only); run<std::uint16_t>(n,check_only);
        run<std::uint32_t>(n,check_only); run<std::uint64_t>(n,check_only);
    }
    if (check_only) std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n",checks);
}
