#include "probe.h"
#include <loom/ring.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <sched.h>

namespace {
void require(bool x) { if (!x) std::abort(); }
template<class T> void layout(const char* name) {
  std::printf("{\"layout\":\"%s\",\"bytes\":%zu,\"alignment\":%zu,\"trivial_copy\":%s,\"trivial_destructor\":%s}\n",
    name, sizeof(T), alignof(T), std::is_trivially_copyable_v<T>?"true":"false",
    std::is_trivially_destructible_v<T>?"true":"false");
}
}
int main(int argc, char** argv) {
  using namespace cb;
  layout<Pair>("Pair"); layout<std::expected<Pair, Error>>("expected<Pair>");
  layout<Triple>("Triple"); layout<std::expected<Triple, Error>>("expected<Triple>");
  layout<Owner>("Owner"); layout<NativeOwner>("NativeOwner");
  layout<std::expected<NativeOwner, Error>>("expected<NativeOwner>");
  layout<HistoricalProgress32>("HistoricalProgress32");
  layout<orbital::Progress>("Progress-current-runtime");
  layout<CompactProgress>("CompactProgress");
  layout<orbital::Result<orbital::allocation::Allocation>>("Result<Allocation>");
  layout<orbital::Result<orbital::allocation::Hold>>("Result<Hold>");
  layout<orbital::Result<loom::detail::RingStorage>>("Result<RingStorage>");
  using Use = Word(*)(Word);
  const Use uses[]{use_expected,use_output,use_callback,use_native_callback,use_reg_expected,use_inline};
  const char* names[]{"expected","output","callback","native_callback","reg_expected","inline_callback"};
  orbital::Signal signal;
  for (Word n=0; n<4096; ++n) {
    // Independent algebraic result, not another candidate's result.
    const Word expected = n%8==0 ? 0 : ((4*n+8) ^ (n ^ 0x9e3779b97f4a7c15ULL));
    for (auto use: uses) require(use(n)==expected);
    Triple sentinel{11,22,33};
    if (output(n,sentinel)) require(sentinel.a==11 && sentinel.b==22 && sentinel.c==33);
    require(live_default(n,2,3,5,7,11)==3*n+29);
    require(live_none(n,2,3,5,7,11)==3*n+29);
    require(live_most(n,2,3,5,7,11)==3*n+29);
    const Word progress_expected=(n%4)==1 ? reinterpret_cast<Word>(&signal)^n : (n%4)==3 ? Word(orbital::Error::capacity) : n%4;
    require(use_progress_result(n,signal)==progress_expected);
    require(use_progress_current_result(n,signal)==progress_expected);
    require(use_progress_output(n,signal)==progress_expected);
    require(use_progress_compact(n,signal)==progress_expected);
  }
  const auto before=drop_total();
  { auto a=owner(19); auto b=native_owner(23); auto c=wrapped_native_owner(25);
    auto d=wrapped_native_owner(24); require(a.value==19 && b.value==23 && c && c->value==25 && !d); }
  require(drop_total()==before+67);
  std::printf("{\"checks\":\"returns-refusal-ownership\",\"passed\":true,\"inputs\":4096}\n");
  const auto rings=ring_checks();
  std::printf("{\"checks\":\"real-ikea-ring\",\"passed\":true,\"cases\":%u}\n",rings);
  if (argc>1) {
    const Word count=std::strtoull(argv[1],nullptr,10);
    cpu_set_t allowed; CPU_ZERO(&allowed); require(sched_getaffinity(0,sizeof(allowed),&allowed)==0);
    int cpu=0; while(cpu<CPU_SETSIZE && !CPU_ISSET(cpu,&allowed)) ++cpu;
    cpu_set_t chosen; CPU_ZERO(&chosen); CPU_SET(cpu,&chosen);
    require(sched_setaffinity(0,sizeof(chosen),&chosen)==0);
    // Rotate candidate order; timing is only a local VM boundary control.
    for (unsigned rep=0; rep<9; ++rep) for (unsigned j=0; j<6; ++j) {
      unsigned k=(j+rep)%6; Word checksum=0;
      const auto start=std::chrono::steady_clock::now();
      for (Word n=0;n<count;++n) checksum+=uses[k](n+1);
      const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
      std::printf("{\"timing\":\"%s\",\"repeat\":%u,\"calls\":%llu,\"elapsed_ns\":%lld,\"checksum\":%llu,\"cpu\":%d}\n",
        names[k],rep,(unsigned long long)count,(long long)elapsed,(unsigned long long)checksum,sched_getcpu());
    }
  }
}
