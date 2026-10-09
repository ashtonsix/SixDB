#pragma once
#include "read.h"
#include <chrono>
#include <ctime>
#include <vector>

namespace orbital_versioned_performance {
inline std::uint64_t clock_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::uint64_t caller_cpu_ns() {
  timespec value{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)) throw std::runtime_error("caller CPU clock failed");
  return std::uint64_t(value.tv_sec)*1000000000+value.tv_nsec;
}
struct Interval {
  std::uint64_t wall_ns{}, caller_cpu_ns{}, snapshot_scopes{}, logical_requests{}, logical_rows{};
  std::vector<std::uint64_t> batch_ns;
};
// The factory invokes the actual baseline read selection or mdb_txn_begin.
// Both acquisition and the actual reader's destructor belong to the interval.
// No timers or checking enter the throughput inner loop. Latency is a separate
// replay; its samples are whole selected batches, never inferred request tails.
template<bool Latency, class Acquire>
Interval read_trial(Acquire&& acquire, std::span<const orbital_bench::Operation> input,
                    std::span<orbital_bench::Outcome> output, std::size_t batch) {
  if (!batch || batch>64 || input.size()!=output.size()) throw std::invalid_argument("invalid trial extents");
  Interval interval;
  if constexpr (Latency) interval.batch_ns.reserve((input.size()+batch-1)/batch);
  const auto cpu=caller_cpu_ns(), start=clock_ns();
  for (std::size_t first=0;first<input.size();first+=batch) {
    const auto count=std::min(batch,input.size()-first);
    std::uint64_t batch_start{};
    if constexpr (Latency) batch_start=clock_ns();
    {
      auto selected=acquire();
      read_batch(selected,input.subspan(first,count),output.subspan(first,count));
    }
    if constexpr (Latency) interval.batch_ns.push_back(clock_ns()-batch_start);
    ++interval.snapshot_scopes;
  }
  interval.wall_ns=clock_ns()-start;interval.caller_cpu_ns=caller_cpu_ns()-cpu;
  interval.logical_requests=input.size();
  // Cold logical accounting after timing; output is also checked independently.
  for (const auto& result:output) interval.logical_rows+=result.rows;
  return interval;
}
// One real write transaction/accepted publication per offered group. No copy,
// conversion, freeze or commit work may be deferred past that transaction body.
template<bool Latency, class Publish>
Interval write_trial(Publish&& publish, std::span<const orbital_bench::Operation> input,
                     std::span<orbital_bench::Outcome> output, std::size_t batch) {
  if (!batch || batch>64 || input.size()!=output.size()) throw std::invalid_argument("invalid trial extents");
  Interval interval;
  if constexpr (Latency) interval.batch_ns.reserve((input.size()+batch-1)/batch);
  const auto cpu=caller_cpu_ns(),start=clock_ns();
  for (std::size_t first=0;first<input.size();first+=batch) {
    const auto count=std::min(batch,input.size()-first);
    std::uint64_t batch_start{};
    if constexpr (Latency) batch_start=clock_ns();
    publish(input.subspan(first,count),output.subspan(first,count));
    if constexpr (Latency) interval.batch_ns.push_back(clock_ns()-batch_start);
    ++interval.snapshot_scopes;
  }
  interval.wall_ns=clock_ns()-start;interval.caller_cpu_ns=caller_cpu_ns()-cpu;
  interval.logical_requests=input.size();
  for (const auto& result:output) interval.logical_rows+=result.rows;
  return interval;
}
} // namespace orbital_versioned_performance
