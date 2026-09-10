#include "inline_ops.h"
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <random>
#include <sched.h>
#include <stdexcept>

using namespace ikea::composition;
namespace {
void pin_cpu() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if(sched_getaffinity(0,sizeof(allowed),&allowed)) throw std::runtime_error("get affinity failed");
    int chosen=-1;
    if(const char* env=std::getenv("SIXDB_CPU")) chosen=std::stoi(env);
    else for(int cpu=0;cpu<CPU_SETSIZE;++cpu) if(CPU_ISSET(cpu,&allowed)){chosen=cpu;break;}
    if(chosen<0 || chosen>=CPU_SETSIZE || !CPU_ISSET(chosen,&allowed)) throw std::runtime_error("CPU is not in allowed set");
    cpu_set_t one; CPU_ZERO(&one); CPU_SET(chosen,&one);
    if(sched_setaffinity(0,sizeof(one),&one) || sched_getcpu()!=chosen) throw std::runtime_error("pinning failed");
    std::cerr<<"cpu="<<chosen<<" seed=0x256bec09; synthetic input, no accuracy measurement\n";
}
using Clock=std::chrono::steady_clock;
void measure(const char* name,const char* layout,std::size_t tiles,std::size_t stride,
             const std::function<std::uint64_t()>& function) {
    auto run=[&](std::size_t sweeps) {
        std::uint64_t checksum=0;
        const auto start=Clock::now();
        for(std::size_t i=0;i<sweeps;++i) {
            asm volatile("" ::: "memory");
            checksum+=function();
        }
        const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
        asm volatile("" : : "r"(checksum) : "memory");
        return std::pair{nanos,checksum};
    };
    std::size_t sweeps=1;
    while(run(sweeps).first<20'000'000 && sweeps<(1u<<26)) sweeps*=2;
    for(unsigned repetition=0;repetition<5;++repetition) {
        auto [nanos,checksum]=run(sweeps);
        std::cout<<name<<','<<layout<<','<<tiles<<','<<stride<<','<<repetition<<','<<sweeps<<','
                 <<nanos<<','<<double(nanos)/double(sweeps*tiles)<<','<<checksum<<'\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        pin_cpu();
        const auto tiles=argc>1?std::stoull(argv[1]):4096;
        if(!tiles || tiles>(1u<<24)) throw std::runtime_error("tile count outside [1,2^24]");
        std::cout<<"case,layout,tiles,stride,repetition,sweeps,nanoseconds,ns_per_tile,checksum\n";
        for(auto layout:{Layout::contiguous,Layout::strided}) {
            auto segment=std::make_shared<Segment>();
            const std::size_t stride=layout==Layout::contiguous?32:48;
            const char* label=layout==Layout::contiguous?"contiguous":"stride48";
            segment->description={{101,"field.membership.child.tiles"},layout,256,0,stride,tiles};
            segment->bytes.resize(tiles*stride);
            std::mt19937_64 random(0x256bec09ULL);
            for(std::size_t i=0;i<tiles;++i) {
                auto* tile=segment->bytes.data()+i*stride;
                for(unsigned j=0;j<32;++j) {
                    auto x=random();
                    // Include dense, sparse, empty/full and byte-run structure.
                    switch(i%8) {
                        case 0:x&=random()&random()&random();break;
                        case 1:x|=random()|random()|random();break;
                        case 2:x=(j<i%33)?255:0;break;
                        case 3:x=(random()&1)?255:0;break;
                        default:break;
                    }
                    tile[j]=static_cast<std::uint8_t>(x);
                }
            }
            auto graph=record_function(segment->description.child);
            auto fused=graph; fuse_load_features(fused);
            Prepared named,function,cps,cps_fused; std::string error;
            if(!prepare(segment,graph,Execution::inline_named,named,error) ||
               !prepare(segment,graph,Execution::inline_function,function,error) ||
               !prepare(segment,graph,Execution::cps,cps,error) ||
               !prepare(segment,fused,Execution::cps,cps_fused,error)) throw std::runtime_error(error);
            if(named()!=function() || named()!=cps() || named()!=cps_fused()) throw std::runtime_error("bound executions disagree");
            measure("author_A_inline",label,tiles,stride,[&]{return named();});
            measure("author_B_inline",label,tiles,stride,[&]{return function();});
            measure("cps_separate",label,tiles,stride,[&]{return cps();});
            measure("cps_load_features_fused",label,tiles,stride,[&]{return cps_fused();});

            // These loops inline the same free-standing bodies; the timed field
            // extraction has no per-tile function-pointer call overhead.
            const auto* bytes=segment->bytes.data();
            auto sweep=[&](auto operation) {
                std::uint64_t sum=0;
                for(std::size_t i=0;i<tiles;++i) sum+=operation(bytes+i*stride);
                return sum;
            };
            measure("features2",label,tiles,stride,[&]{return sweep([](const void* p){return features_from_bytes(p);});});
            measure("features3_transitions",label,tiles,stride,[&]{return sweep([](const void* p){return features_with_transitions(p);});});
            measure("features4_quadrants",label,tiles,stride,[&]{return sweep([](const void* p){return features_with_quadrants(p);});});
            measure("predict2",label,tiles,stride,[&]{return sweep([](const void* p){return ikea::bec_predictor::predict_size(features_from_bytes(p));});});
            measure("predict3_transitions",label,tiles,stride,[&]{return sweep([](const void* p){return ikea::bec_predictor::predict_size_transitions(features_with_transitions(p));});});
            measure("predict4_quadrants",label,tiles,stride,[&]{return sweep([](const void* p){return ikea::bec_predictor::predict_size_quadrants(features_with_quadrants(p));});});
            if(layout==Layout::contiguous && tiles%2==0)
                measure("features2_512grain",label,tiles,stride,[&]{
                    std::uint64_t sum=0;
                    for(std::size_t i=0;i<tiles;i+=2) {
                        auto pair=features_from_64_bytes(bytes+i*32); sum+=pair.first+pair.second;
                    }
                    return sum;
                });
        }
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
