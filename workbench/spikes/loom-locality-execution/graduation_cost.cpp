#include "graduation_cost.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

CostPolicy policy() {
#if LOOM_GRADUATED_POLICY
  auto policy=CostPolicy::bind({32,32,2,2,32});
  if(!policy)std::abort();
  return std::move(*policy);
#else
  return {};
#endif
}
void observe(CostPolicy& policy,const CostIdentity& identity,std::uint64_t epoch,
    std::uint64_t elapsed) {
#if LOOM_GRADUATED_POLICY
  const bool accepted=policy.observe(identity,epoch,{elapsed,128});
#else
  const bool accepted=policy.observe(identity,epoch,{loom_locality::Path::direct,elapsed,128,0,1},{32,32,2,2});
#endif
  if(!accepted)std::abort();
}
int main(int argc,char** argv) {
  const unsigned repeats=argc>1?std::stoul(argv[1]):5;
  constexpr std::uint64_t count=1000000;
  const CostIdentity identity{1,1,1,1,1};
  auto hot=policy(),cold=policy();
  observe(hot,identity,1,1024);observe(hot,identity,2,1024);observe(cold,identity,2,25600);
  std::puts("case,rep,operations,ns_per_operation,checksum,policy_bytes,graduated");
  for(unsigned rep=0;rep<repeats;++rep)for(unsigned order=0;order<4;++order) {
    const auto arm=(order+rep)%4;std::uint64_t checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(std::uint64_t i=0;i<count;++i) {
      asm volatile(""::"r"(&hot),"r"(&cold),"r"(&identity):"memory");
      auto& state=arm==2?cold:hot;
      // Expired state must be reconstructed for each outer case below. This
      // loop measures the same ordinary unknown selection after its first call.
      checksum+=graduation_select(state,identity,arm==3?65538:2,arm==0);
    }
    const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/count;
    constexpr std::array names{"known_direct","cheap","expensive","expired"};
    std::printf("%s,%u,%llu,%.6f,%llu,%zu,%u\n",names[arm],rep,(unsigned long long)count,
        ns,(unsigned long long)checksum,sizeof(CostPolicy),unsigned(LOOM_GRADUATED_POLICY));
    hot=policy();cold=policy();
    observe(hot,identity,1,1024);observe(hot,identity,2,1024);observe(cold,identity,2,25600);
  }
}
