#include "graduation_cost.h"

extern "C" [[gnu::noinline]] unsigned graduation_select(CostPolicy& policy,
    const CostIdentity& identity,std::uint64_t now,bool known) noexcept {
#if LOOM_GRADUATED_POLICY
  return unsigned(policy.select(identity,now,{true,true,32,32},
      known?loom::execution::Prior::direct:loom::execution::Prior::unknown).path);
#else
  return unsigned(loom_locality::decide(policy,identity,now,{true,true,32,32},{32,32,2,2},
      known?loom_locality::Control::caller_known_direct:loom_locality::Control::adaptive).path);
#endif
}
