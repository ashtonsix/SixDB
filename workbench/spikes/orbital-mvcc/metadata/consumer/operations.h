#pragma once
#include "candidates.h"

namespace orbital_mvcc::metadata_consumer {
// A complete fixed point/chain consumer over the same encoded bytes and Ikea
// transfer bodies. Stationary endpoints live only within this call. The schema,
// matching qualified count and immutable backing remain protected; keys are
// sorted. Output is disjoint from all source bytes and live controls/metadata.
// Outcome is caller-owned payload: no optional return, record array, ownership
// packet, shared scan scratch or special calling convention is introduced.
inline void read_chain_into(const v::Schema& schema, ExplicitTuple16 state, Key key,
                            unsigned steps, orbital_bench::Outcome& output) {
  output = {};
  if (!steps) return;
  const auto source = state.view(schema);
  const auto reads = v::native_detail::tuple_reads(schema, source);
  const auto read_key = [&](std::size_t row) {
    return v::native_detail::tuple_value<8>(reads[0], row);
  };
  for (unsigned step = 0; step < steps; ++step) {
    const auto row = v::native_detail::seek(state.size(), key, read_key);
    if (row == state.size() || read_key(row) != key) {
      orbital_bench::consume_absent(output, key);
      break;
    }
    const auto record = v::native_detail::tuple_record(reads, row);
    orbital_bench::consume_row(output, key, record);
    key = record.next;
  }
}
inline void read_chain8_into(const v::TupleControl& control, QualifiedTuple8 state, Key key,
                             unsigned steps, orbital_bench::Outcome& output) {
  read_chain_into(control.schema, state.with(control), key, steps, output);
}
inline void read_point8_into(const v::TupleControl& control, QualifiedTuple8 state, Key key,
                             orbital_bench::Outcome& output) {
  read_chain8_into(control, state, key, 1, output);
}
} // namespace orbital_mvcc::metadata_consumer
