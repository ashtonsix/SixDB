#include "locality.h"
#include "contiguous.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace loom_locality;
extern "C" Decision locality_adaptive_decision(const Record&,const Tag&,Epoch,Paths,const Policy&) noexcept;
extern "C" Decision locality_known_direct_decision(const Record&,const Tag&,Epoch,Paths,const Policy&) noexcept;
int main(int argc,char** argv) {
  const unsigned repeats=argc>1?std::stoul(argv[1]):7;
  constexpr std::uint64_t count=1000000;
  const Tag tag{1,1,1,1,1};const Policy policy{32,32,2,2};const Paths paths{true,true,32,32};
  Record hot,cold;
  if(!hot.observe(tag,1,{Path::direct,1024,128,0,1},policy)||
     !hot.observe(tag,2,{Path::direct,1024,128,0,1},policy)||
     !cold.observe(tag,2,{Path::direct,25600,128,0,1},policy))return 1;
  std::puts("case,rep,operations,ns_per_operation,checksum,record_bytes");
  for(unsigned rep=0;rep<repeats;++rep)for(unsigned order=0;order<4;++order) {
    const unsigned arm=(order+rep)%4;std::uint64_t checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(std::uint64_t i=0;i<count;++i) {
      asm volatile(""::"r"(&hot),"r"(&cold),"r"(&tag):"memory");
      Decision d;
      if(arm==0)d=locality_known_direct_decision(hot,tag,2,paths,policy);
      else if(arm==1)d=locality_adaptive_decision(hot,tag,2,paths,policy);
      else if(arm==2)d=locality_adaptive_decision(cold,tag,2,paths,policy);
      else d=locality_adaptive_decision(hot,tag,65538,paths,policy);
      checksum+=unsigned(d.path)+unsigned(d.reason);
    }
    const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/count;
    constexpr std::array names{"known_direct","cheap","expensive","expired"};
    std::printf("%s,%u,%llu,%.6f,%llu,%zu\n",names[arm],rep,(unsigned long long)count,ns,(unsigned long long)checksum,sizeof(Record));
  }
}
