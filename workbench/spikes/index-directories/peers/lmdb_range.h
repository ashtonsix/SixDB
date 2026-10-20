#pragma once
#include <lmdb.h>
#include <cstdint>
#include <cstring>
#include "../include/scan_mask.h"

// Workbench's qualified native-range recipe; retained support is referenced in
// FINDINGS. The caller owns the actual INTEGERKEY txn/cursor for this scan.
namespace index_spike::lmdb_range {
struct Work { std::uint64_t calls{},positioned{},boundary{},visited{}; };
template<class Visit> int scan(MDB_cursor* cursor,std::uint32_t low,std::uint32_t high,
    Work& work,Visit&& visit) {
  if (low>high) return MDB_SUCCESS;
  auto bound=low;
  MDB_val key{sizeof(bound),&bound},value{};
  ++work.calls;
  int status=mdb_cursor_get(cursor,&key,&value,MDB_SET_RANGE);
  while (status==MDB_SUCCESS) {
    ++work.positioned;
    if (key.mv_size!=sizeof(bound)) return MDB_CORRUPTED;
    std::uint32_t prefix; std::memcpy(&prefix,key.mv_data,sizeof(prefix));
    if (prefix>high) { ++work.boundary; return MDB_SUCCESS; }
    ++work.visited;
    visit(prefix,value); // ready-mask check precedes payload consumption
    ++work.calls;
    status=mdb_cursor_get(cursor,&key,&value,MDB_NEXT);
  }
  return status==MDB_NOTFOUND?MDB_SUCCESS:status;
}
// Actual cursor range seeks driven by selected ORIGINAL-prefix runs. One
// enclosing txn; no gets or seek per material word. Work includes all seeks,
// NEXTs and actual positioned/boundary entries. A gap cannot end later runs.
template<class Visit> int masked_runs(MDB_cursor* cursor,const GraphScanRequest& request,
    Work& work,Visit&& visit) {
  MaskRuns runs(request);
  std::uint32_t low{},high{},positioned{};
  bool have_position=false;
  MDB_val key{},value{};
  while (runs.next(low,high)) {
    bool newly_positioned=false;
    if (!have_position || positioned<low) {
      auto bound=low; key={sizeof(bound),&bound}; ++work.calls;
      const int status=mdb_cursor_get(cursor,&key,&value,MDB_SET_RANGE);
      if (status==MDB_NOTFOUND) return MDB_SUCCESS;
      if (status) return status;
      if (key.mv_size!=sizeof(positioned)) return MDB_CORRUPTED;
      std::memcpy(&positioned,key.mv_data,sizeof(positioned));
      ++work.positioned; have_position=true; newly_positioned=true;
    }
    if (positioned>high) {
      if (newly_positioned) ++work.boundary;
      continue; // carry a gap's successor into later mask runs
    }
    while (true) {
      ++work.visited; visit(positioned,value);
      if (positioned==high) { have_position=false; break; }
      ++work.calls;
      const int status=mdb_cursor_get(cursor,&key,&value,MDB_NEXT);
      if (status==MDB_NOTFOUND) return MDB_SUCCESS;
      if (status) return status;
      if (key.mv_size!=sizeof(positioned)) return MDB_CORRUPTED;
      std::memcpy(&positioned,key.mv_data,sizeof(positioned));
      ++work.positioned;
      if (positioned>high) { ++work.boundary; break; }
    }
  }
  return MDB_SUCCESS;
}
}
