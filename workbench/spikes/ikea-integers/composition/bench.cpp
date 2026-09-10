#include "fixture.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sched.h>

namespace c=ikea::integers::composition;
int main(int argc,char** argv) {
    const unsigned tiles=argc>1?std::stoul(argv[1]):4096;
    if(tiles==0 || tiles>(1u<<22))return 2;
    cpu_set_t allowed;CPU_ZERO(&allowed);
    if(sched_getaffinity(0,sizeof(allowed),&allowed))return 3;
    unsigned cpu=0;while(cpu<CPU_SETSIZE && !CPU_ISSET(cpu,&allowed))++cpu;
    if(const auto* requested=std::getenv("SIXDB_CPU"))cpu=std::stoul(requested);
    cpu_set_t one;CPU_ZERO(&one);CPU_SET(cpu,&one);
    if(sched_setaffinity(0,sizeof(one),&one))return 4;
    std::cerr<<"pinned_cpu,"<<cpu<<"\nseed,0x120064\ncutoff,2048\n";
    std::vector<std::uint16_t> values(64*tiles);
    std::mt19937 rng(0x120064);
    for(auto& v:values)v=rng()&4095;
    const auto truth=c::fixture::sum(values,2048);
    const c::Column<c::Local4> local{101,{}};const auto scan=local.with_tail(c::Scan4{},202);
    const c::Column<c::Scan4> middle{303,{}};
    auto local_storage=std::make_shared<c::Storage>(32+96*tiles),scan_storage=std::make_shared<c::Storage>(32+96*tiles),
         middle_storage=std::make_shared<c::Storage>(32+96*tiles);
    for(unsigned tile=0;tile<tiles;++tile) {
        const auto input=std::span<const std::uint16_t,64>(values.data()+64*tile,64);
        c::fixture::encode(c::ParentKind::packets8,input,local_storage->bytes.get()+32+96*tile);
        c::fixture::encode(c::ParentKind::body64_tail32,input,scan_storage->bytes.get()+32+96*tile);
        c::fixture::encode(c::ParentKind::body32_tail32_body32,input,middle_storage->bytes.get()+32+96*tile);
    }
    std::array<c::Prepared,6> cases;std::string error;
    const auto lp=c::place(local_storage,c::ParentKind::packets8,101,32,tiles);
    const auto sp=c::place(scan_storage,c::ParentKind::body64_tail32,202,32,tiles);
    const auto mp=c::place(middle_storage,c::ParentKind::body32_tail32_body32,303,32,tiles);
    if(!c::prepare(local,lp,2048,c::Execution::authored_inline,cases[0],error) ||
       !c::prepare(local,lp,2048,c::Execution::cps,cases[1],error) ||
       !c::prepare(scan,sp,2048,c::Execution::authored_inline,cases[2],error) ||
       !c::prepare(scan,sp,2048,c::Execution::cps,cases[3],error) ||
       !c::prepare(middle,mp,2048,c::Execution::authored_inline,cases[4],error) ||
       !c::prepare(middle,mp,2048,c::Execution::cps,cases[5],error)) {std::cerr<<error<<'\n';return 5;}
    const char* names[]={"local-inline","local-cps","scan-inline","scan-cps","scan-middle-inline","scan-middle-cps"};
    std::cout<<"case,tiles,values,bytes,phase,rep,sweeps,nanos,ns_per_value,checksum\n";
    for(unsigned which=0;which<cases.size();++which) {
        auto& prepared=cases[which];if(prepared()!=truth)return 6;
        const auto run=[&](std::uint64_t sweeps) {
            std::uint64_t checksum=0;const auto begin=std::chrono::steady_clock::now();
            for(std::uint64_t i=0;i<sweeps;++i) {checksum+=prepared();asm volatile("" : "+r"(checksum) :: "memory");}
            const auto nanos=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            return std::pair{nanos,checksum};
        };
        std::uint64_t sweeps=1;while(run(sweeps).first<20000000)sweeps*=2;
        for(unsigned rep=0;rep<5;++rep) {
            const auto [nanos,checksum]=run(sweeps);
            std::cout<<names[which]<<','<<tiles<<','<<64*tiles<<','<<96*tiles<<",32,"<<rep<<','<<sweeps<<','<<nanos<<','
                     <<double(nanos)/(double(sweeps)*64*tiles)<<','<<checksum<<'\n';
        }
    }
}
