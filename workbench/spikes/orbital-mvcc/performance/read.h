#pragma once
#include "workload.h"
#include <array>

namespace orbital_versioned_performance {
// Real direct caller shared by the native comparator and the routed consumer.
// One selected read/transaction is acquired outside for the entire offered
// batch. This function never refreshes selection during a dependent chain.
template<class Read>
orbital_bench::Outcome read_operation(const Read& read, const orbital_bench::Operation& op) {
  using namespace orbital_bench;
  Outcome result{};
  if (op.kind==Kind::scan) {
    if (op.count>16) throw std::invalid_argument("this fixture's scan scratch bound is 16");
    std::array<Row,16> rows;
    const auto count=read.scan(op.key,std::span(rows).first(op.count));
    for (std::size_t i=0;i<count;++i) consume_row(result,rows[i].key,rows[i].record);
  } else if (op.kind==Kind::read || op.kind==Kind::chain) {
    auto key=op.key;
    for (auto left=op.kind==Kind::chain?op.count:1u;left;--left) {
      const auto record=read.get(key);
      if (!record) { consume_absent(result,key); break; }
      consume_row(result,key,*record); key=record->next;
    }
  } else throw std::invalid_argument("read-only caller received a write");
  return result;
}
template<class Read>
void read_batch(const Read& read, std::span<const orbital_bench::Operation> input,
                std::span<orbital_bench::Outcome> output) {
  if (input.size()!=output.size() || input.size()>64) throw std::invalid_argument("read batch extent mismatch");
  for (std::size_t i=0;i<input.size();++i) output[i]=read_operation(read,input[i]);
}
} // namespace orbital_versioned_performance
