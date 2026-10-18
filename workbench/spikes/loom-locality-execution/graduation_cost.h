#pragma once
#include "locality.h"
#include <loom/execution.h>

// This caller microbenchmark keeps its Tag constant for each Policy's lifetime;
// it excludes changing-cohort detection/reset. graduation.cpp pays that caller
// work explicitly. Frozen cost measurements used the previous identity-bearing
// module API and do not qualify the current interface or caller reset cost.
using CostIdentity=loom_locality::Tag;
#if LOOM_GRADUATED_POLICY
using CostPolicy=loom::execution::Policy;
#else
using CostPolicy=loom_locality::Record;
#endif

extern "C" unsigned graduation_select(CostPolicy&,const CostIdentity&,std::uint64_t,bool) noexcept;
