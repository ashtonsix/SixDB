#pragma once
#include "trace.h"
#include <map>

namespace orbital_versioned_performance {
using Golden = std::map<orbital_bench::Key,orbital_bench::Record>;
inline orbital_bench::Outcome oracle(const Golden& rows,const orbital_bench::Operation& op) {
  using namespace orbital_bench;
  Outcome result{};
  const auto mix=[&](std::uint64_t value) { result.digest^=value+0x9e3779b97f4a7c15ull+(result.digest<<6)+(result.digest>>2); };
  const auto consume=[&](Key key,Record record) { mix(key);mix(record.next);mix(record.value);mix(record.tag);++result.rows; };
  if (op.kind==Kind::scan) {
    auto it=rows.lower_bound(op.key);
    for (unsigned n=0;it!=rows.end() && n<op.count;++n,++it) consume(it->first,it->second);
  } else {
    auto key=op.key;
    for (unsigned n=0;n<(op.kind==Kind::chain?op.count:1);++n) {
      const auto it=rows.find(key);
      if (it==rows.end()) {mix(key);mix(0);break;}
      consume(key,it->second);key=it->second.next;
    }
  }
  return result;
}
} // namespace orbital_versioned_performance
