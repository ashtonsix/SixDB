#include <benchmark/benchmark.h>
#include "coalescing.h"
#include "verify.h"
#include "../../ikea-composition/probes/ikea-integers/local.h"
#include "../../ikea-composition/probes/ikea-integers/scan.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace sp=ikea::seriespack;
namespace prior=ikea::integers;
constexpr std::size_t N=8192;
using fn=void(*)(const std::uint8_t*,std::uint8_t*,std::size_t);

template<unsigned W,bool Stripe,unsigned Arm,bool Encode>
[[gnu::noinline]] void kernel(const std::uint8_t* __restrict in,
                              std::uint8_t* __restrict out,std::size_t count) {
    constexpr auto G=Stripe?sp::geometry::striped:sp::geometry::local8;
    constexpr auto T=sp::payload_layout<W,G>::tile_values,B=sp::payload_layout<W,G>::tile_bytes;
    __builtin_assume(count%256==0);
    if constexpr(Arm==0) {
        if constexpr(Encode)sp::neon::encode_low_tiles<W,G>(in,out,count/T);
        else sp::neon::decode_tiles<W,G>(in,out,count/T);
    } else if constexpr(Arm==1) {
        for(std::size_t i=0;i<count;i+=64)sp::detail::static_for<4>([&](auto pair){
            if constexpr(Encode)sp::neon::encode_low_local_pair<W>(in+i+pair*16,out+(i/8+pair*2)*W);
            else sp::neon::decode_local_pair<W>(in+(i/8+pair*2)*W,out+i+pair*16);
        });
    } else if constexpr(Arm==2 || Arm==3) {
        constexpr unsigned Step=Arm==2?64:256;
        for(std::size_t i=0;i<count;i+=Step)sp::detail::static_for<Step/64>([&](auto block){
            if constexpr(Encode)candidate::encode64<W>(in+i+block*64,out+(i/8+block*8)*W);
            else candidate::decode64<W>(in+(i/8+block*8)*W,out+i+block*64);
        });
    } else if constexpr(Arm==4 || Arm==5) {
        constexpr unsigned Step=Arm==4?64:256;
        for(std::size_t i=0;i<count;i+=Step) {
            if constexpr(Stripe) {
                if constexpr(Encode)prior::scan_encode<W,Step>(in+i,out+i/8*W);
                else prior::scan_decode<W,Step>(in+i/8*W,out+i);
            } else {
                if constexpr(Encode)prior::local_neon_encode<W,Step>(in+i,out+i/8*W);
                else prior::local_neon_decode<W,Step>(in+i/8*W,out+i);
            }
        }
    } else if constexpr(Arm==6) {
        for(std::size_t i=0;i<count;i+=256) {
            if constexpr(Encode)sp::neon::encode_low_tiles<W,G>(in+i,out+i/T*B,256/T);
            else sp::neon::decode_tiles<W,G>(in+i/T*B,out+i,256/T);
        }
    }
}

template<unsigned W,bool Stripe>
struct buffers {
    alignas(64) std::array<std::uint8_t,N> input{},decoded{};
    alignas(64) std::array<std::uint8_t,N/8*W> encoded{},expected{};
    buffers() {
        std::uint64_t state=0x5187c98b4a1a4d43ULL;
        for(auto& v:input){state^=state>>12;state^=state<<25;state^=state>>27;v=(state*0x2545f4914f6cdd1dULL)&((1u<<W)-1);}
        if constexpr(Stripe) {
            constexpr unsigned T=sp::payload_layout<W,sp::geometry::striped>::tile_values;
            constexpr unsigned B=sp::payload_layout<W,sp::geometry::striped>::tile_bytes;
            for(unsigned i=0;i<N;i+=T)sp::detail::encode_tile<W,sp::geometry::striped>(input.data()+i,expected.data()+i/T*B);
        } else {
            for(unsigned i=0;i<N;++i)for(unsigned bit=0;bit<W;++bit)
                expected[i/8*W+bit]|=((input[i]>>bit)&1u)<<(i%8);
        }
        encoded=expected;
    }
};
template<unsigned W,bool Stripe> buffers<W,Stripe>& shared(){static buffers<W,Stripe> v;return v;}

template<unsigned W,bool Stripe,unsigned Arm,bool Encode>
void measure(benchmark::State& state) {
    auto& b=shared<W,Stripe>();
    auto* in=Encode?b.input.data():b.expected.data();
    auto* out=Encode?b.encoded.data():b.decoded.data();
    auto f=&kernel<W,Stripe,Arm,Encode>;
    f(in,out,N);
    if constexpr(Encode){if(b.encoded!=b.expected)std::abort();}
    else {if(b.decoded!=b.input)std::abort();}
    for(auto _:state){f(in,out,N);benchmark::DoNotOptimize(out);benchmark::ClobberMemory();}
    state.SetItemsProcessed(state.iterations()*N);
    state.counters["values_per_batch"]=N;
    state.counters["encoded_bytes"]=N/8*W;
    state.counters["source_bytes"]=N;
    state.counters["input_mod64"]=reinterpret_cast<std::uintptr_t>(in)%64;
    state.counters["output_mod64"]=reinterpret_cast<std::uintptr_t>(out)%64;
}

template<unsigned W,bool Stripe,unsigned Arm>
void add(const char* arm) {
    for(bool encode:{false,true}) {
        char name[128];std::snprintf(name,sizeof(name),"grain/%s/k%u/%s/%s",Stripe?"striped":"local",W,arm,encode?"encode":"decode");
        if(encode)benchmark::RegisterBenchmark(name,&measure<W,Stripe,Arm,true>);
        else benchmark::RegisterBenchmark(name,&measure<W,Stripe,Arm,false>);
    }
}

[[maybe_unused]]const bool registered=[] {
    sp::detail::static_for<5>([](auto i){constexpr unsigned w=i+3;
        candidate::verify64<w>();
        add<w,false,0>("native");add<w,false,1>("pairs64");add<w,false,2>("coalesced64");
        add<w,false,3>("coalesced256");add<w,false,4>("predecessor64");add<w,false,5>("predecessor256");
    });
    add<4,true,0>("native");add<4,true,6>("native256");add<4,true,4>("predecessor64");add<4,true,5>("predecessor256");
    add<6,true,0>("native");add<6,true,6>("native256");add<6,true,5>("predecessor256");
    return true;
}();
}
