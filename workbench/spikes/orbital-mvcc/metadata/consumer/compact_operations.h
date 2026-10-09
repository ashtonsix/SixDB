#pragma once
#include "compact_law.h"
#include <utility>

namespace orbital_mvcc::metadata_consumer::compact_law {
using orbital_bench::Key;
using orbital_bench::Outcome;

namespace operations_detail {
[[gnu::always_inline]] inline Key read_key(const ReadLaw& law, Part state, std::size_t row) {
  // Binary search proves row<state.size(). This scalar is the kernel result,
  // not a chain/scan scratch array. The existing fixed field body does the load.
  Key value;
  const auto status = state.field(law, v::Field::key, row, {&value, 1});
  if (!status) std::unreachable();
  return value;
}
[[gnu::always_inline]] inline std::size_t seek(const ReadLaw& law, Part state, Key key) {
  std::size_t first = 0, last = state.size();
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    if (read_key(law, state, middle) < key) first = middle + 1;
    else last = middle;
  }
  return first;
}
} // namespace operations_detail

// Complete fixed Part call, with caller-owned Outcome and no record array,
// endpoint array or retained Schema. The explicit law owns all four transfers;
// its complete storage cost is separate from the pointer+count Part value.
// The compact qualifier admitted exact canonical encoding/count/body bounds.
// Native use additionally requires sorted keys, the same/equivalent law/count,
// uninterrupted immutable backing protection, and Outcome disjoint from source
// bytes and live metadata. This is not container/navigation/commit authority.
// Payload loads remain Ikea's private fixed-transfer body via compact_law.h.
inline void read_chain_into(const ReadLaw& law, Part state, Key key,
                            unsigned steps, Outcome& output) {
  output = {};
  for (unsigned step = 0; step < steps; ++step) {
    const auto row = operations_detail::seek(law, state, key);
    if (row == state.size() || operations_detail::read_key(law, state, row) != key) {
      orbital_bench::consume_absent(output, key);
      break;
    }
    const auto record = state.row_unchecked(law, row).record;
    orbital_bench::consume_row(output, key, record);
    key = record.next;
  }
}
inline void read_chain8_into(const Control& control, Cursor state, Key key,
                             unsigned steps, Outcome& output) {
  read_chain_into(control.law, state.with(control), key, steps, output);
}
inline void read_point8_into(const Control& control, Cursor state, Key key, Outcome& output) {
  read_chain8_into(control, state, key, 1, output);
}
} // namespace orbital_mvcc::metadata_consumer::compact_law
