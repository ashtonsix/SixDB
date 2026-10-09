#include "ranges.h"
#include "runtime.h"
#include "../baseline/src/internal.h"
#include <unordered_set>

namespace orbital_versioned_performance {
RangeManifest diagnostic_ranges(const Orbital& store) {
  namespace d=orbital::object::detail;
  RangeManifest result;
  std::unordered_set<const void*> visited;
  const auto add=[&](const void* address,std::size_t bytes,RangeRole role,const char* record) {
    if (bytes && visited.insert(address).second) result.ranges.push_back({address,bytes,role,record});
  };
  const auto record=[&]<class T>(const T* value,const char* name) {add(value,sizeof(T),RangeRole::metadata,name);};
  const auto vector=[&](const auto& value,const char* name) {
    add(value.data(),value.size()*sizeof(value[0]),RangeRole::metadata,name);
  };
  const auto part=[&](const d::Part* p) {
    if (!p || visited.contains(p)) return;
    record(p,"Part");vector(p->interpretation,"Part.interpretation");
    add(p->leaf.body.data(),p->leaf.body.size(),RangeRole::body,"Part.body");
  };
  const auto rep=[&](const d::Rep* r) {
    if (!r || visited.contains(r)) return;
    record(r,"Rep");vector(r->parts,"Rep.parts");vector(r->leaves,"Rep.leaves");vector(r->lookup,"Rep.lookup");
    auto* info=r->info.get();
    if (info && !visited.contains(info)) {
      record(info,"Info");vector(info->logical,"Info.logical");vector(info->interpretation,"Info.interpretation");
    }
    for (const auto& p:r->parts) part(p.get());
  };
  const auto* c=d::Api::catalog(store.store_).get();
  if (!c) return result;
  record(c,"Catalog");record(c->domain,"Domain");
  for (const auto& entry:c->entries) {
    record(&entry,"Catalog.entry-value");
    for (auto* version=entry.second.head;version;version=version->older) {
      record(version,"Version");rep(version->representation.get());rep(version->alternative.get());
    }
  }
  for (auto* snapshot=c->first_snapshot;snapshot;snapshot=snapshot->next) {
    record(snapshot,"Snapshot");vector(snapshot->captured,"Snapshot.captured");
    for (const auto& captured:snapshot->captured) rep(captured.get());
  }
  result.uncovered={"unordered_map bucket array and allocation/node headers",
      "allocator headers, free lists and new per-batch Snapshot allocations",
      "Pool/worker implementation, shared_ptr control blocks and external dependencies",
      "caller trace/output/Schema pages and code/TLB state"};
  if (c->pending) result.uncovered.push_back("pending acceptance/private edit closure");
  if (c->domain->queued) result.uncovered.push_back("queued retirement derived records");
  return result;
}
} // namespace orbital_versioned_performance
