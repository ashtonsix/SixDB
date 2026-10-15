#pragma once
#include "locality.h"
#include <loom/execution.h>

#if LOOM_GRADUATED_POLICY
using CostPolicy=loom::execution::Policy;
using CostIdentity=loom::execution::Identity;
#else
using CostPolicy=loom_locality::Record;
using CostIdentity=loom_locality::Tag;
#endif

extern "C" unsigned graduation_select(CostPolicy&,const CostIdentity&,std::uint64_t,bool) noexcept;
