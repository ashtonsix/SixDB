#pragma once
#include "data.h"

namespace orbital_bench::versioned {
inline constexpr orbital::ProgramId data_program{0x4f4256455253494fULL, 1};
struct Costs {
  KernelCosts kernel;
  std::uint64_t requested_body_bytes{}, requested_parts{}, copied_bytes{}, copied_spans{};
};
struct Update { Key key{}; Record record; };
constexpr std::size_t write_batch_limit = 64;

} // namespace orbital_bench::versioned
