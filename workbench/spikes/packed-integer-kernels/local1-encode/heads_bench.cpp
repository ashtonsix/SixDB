#include <ikea/seriespack.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <stdexcept>
#include <vector>

namespace {
namespace sp=ikea::seriespack;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct buffer {
    std::uint8_t* data=nullptr;std::size_t size;
    explicit buffer(std::size_t n):size(n){void* p=nullptr;require(posix_memalign(&p,64,std::max<std::size_t>(64,(n+63)&~std::size_t{63}))==0,"allocation");data=static_cast<std::uint8_t*>(p);std::memset(data,0,n);}
    ~buffer(){std::free(data);}
    buffer(const buffer&)=delete;
    std::span<std::byte> span(){return{reinterpret_cast<std::byte*>(data),size};}
};
[[gnu::noinline]] void operation(const sp::bound_encoder& encoder,const std::uint8_t* input,std::size_t count){
    encoder.encode(sp::input_values{std::span(input,count)});
}
[[gnu::noinline]] double elapsed(const sp::bound_encoder& encoder,const std::uint8_t* input,std::size_t n,std::size_t passes){
    using clock=std::chrono::steady_clock;const auto start=clock::now();
    for(std::size_t i=0;i<passes;++i){operation(encoder,input,n);asm volatile("":::"memory");}
    return std::chrono::duration<double,std::nano>(clock::now()-start).count();
}
std::size_t checks=0;
void run(unsigned h,bool gaps,std::size_t n,bool check_only){
    const auto tiles=n/8;
    const unsigned stride0=gaps?13:8,stride1=gaps?23:8;
    buffer input(n),payload(tiles),head0(h?(tiles-1)*stride0+8:0),head1(h==16?(tiles-1)*stride1+8:0);
    std::vector<std::uint8_t> expected_payload(tiles,0),expected0(head0.size,0xa7),expected1(head1.size,0xa7);
    for(std::size_t i=0;i<n;++i){
        std::uint64_t x=i+0x9347ea6d82bc051fULL;x^=x>>17;x*=0x9e3779b97f4a7c15ULL;x^=x>>31;
        const auto value=std::uint8_t(x&(h?255:1));input.data[i]=value;
        expected_payload[i/8]|=std::uint8_t((value&1)<<(i%8));
        if(h)expected0[i/8*stride0+i%8]=std::uint8_t(std::uint64_t(value)>>(h-7));
        if(h==16)expected1[i/8*stride1+i%8]=std::uint8_t(value>>1);
    }
    const auto view=sp::mutable_view::attach({1+h,h,sp::geometry::local8},n,
        {{payload.span(),1},{{{head0.span(),h?stride0:0},{head1.span(),h==16?stride1:0}}}});
    require(view.has_value(),"attach");
    const auto encoder=sp::bind_encoder(*view,sp::execution_target::avx512);require(encoder.has_value(),"bind AVX512");
    const auto validate=[&]{
        std::memset(payload.data,0xa7,payload.size);std::memset(head0.data,0xa7,head0.size);std::memset(head1.data,0xa7,head1.size);
        operation(*encoder,input.data,n);
        require(std::memcmp(payload.data,expected_payload.data(),payload.size)==0,"payload oracle");
        if(h)require(std::memcmp(head0.data,expected0.data(),head0.size)==0,"head0 oracle/gaps");
        if(h==16)require(std::memcmp(head1.data,expected1.data(),head1.size)==0,"head1 oracle/gaps");
        ++checks;
    };
    validate();if(check_only)return;
    std::size_t passes=1;while(elapsed(*encoder,input.data,n,passes)<20000000.0)passes*=2;
    for(unsigned repetition=0;repetition<5;++repetition){
        for(unsigned warm=0;warm<16;++warm)operation(*encoder,input.data,n);
        const auto ns=elapsed(*encoder,input.data,n,passes);validate();
        std::printf("{\"width\":%u,\"head_bits\":%u,\"input_bits\":8,\"head_placement\":\"%s\",\"values\":%zu,"
            "\"passes\":%zu,\"repetition\":%u,\"elapsed_ns\":%.0f,\"ns_per_value\":%.9f,"
            "\"input_page_offset\":%zu,\"payload_page_offset\":%zu,\"head0_page_offset\":%zu,\"head1_page_offset\":%zu}\n",
            1+h,h,gaps?"strided":"dense",n,passes,repetition,ns,ns/(double(passes)*n),
            reinterpret_cast<std::uintptr_t>(input.data)%4096,reinterpret_cast<std::uintptr_t>(payload.data)%4096,
            reinterpret_cast<std::uintptr_t>(head0.data)%4096,reinterpret_cast<std::uintptr_t>(head1.data)%4096);
    }
}
int pin(){
    cpu_set_t allowed;CPU_ZERO(&allowed);require(sched_getaffinity(0,sizeof allowed,&allowed)==0,"affinity");
    int cpu=-1;if(const auto* v=std::getenv("SIXDB_CPU"))cpu=std::atoi(v);else for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&allowed)){cpu=i;break;}
    require(cpu>=0&&cpu<CPU_SETSIZE&&CPU_ISSET(cpu,&allowed),"allowed CPU");cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);
    require(sched_setaffinity(0,sizeof one,&one)==0,"pin");return cpu;
}
}
int main(int argc,char** argv){
    const bool check_only=argc==2&&std::strcmp(argv[1],"--check-only")==0;
    require(argc==1||check_only,"usage: heads_bench [--check-only]");
    const char* text=std::getenv("SERIESPACK_BULK_VALUES");const auto count=text?std::strtoull(text,nullptr,10):8192;
    require(count>=256&&count<=65536&&count%256==0,"count");
    std::printf("{\"diagnostic\":\"local1-u8-public-heads\",\"cpu\":%d,\"values\":%llu,\"scope\":\"bound full construction, no effects\"}\n",pin(),count);
    run(0,false,count,check_only);for(unsigned h:{8u,16u}){run(h,false,count,check_only);run(h,true,count,check_only);}
    if(check_only)std::printf("{\"checks\":\"passed\",\"cases\":%zu}\n",checks);
}
