#pragma once
#include "layer.h"
#include <algorithm>
#include <map>
#include <random>
#include <unordered_set>
#include <vector>

// Synthetic directory populations shared by the checks, counters and timings.
namespace segdir {
using Rng=std::mt19937_64;

inline Key key(std::uint64_t v) {
  Key k;
  for(int i=0;i<8;++i) k.b[i]=std::uint8_t(v>>(56-8*i));
  return k;
}
inline std::uint64_t value(const Key& k) {
  std::uint64_t v=0;
  for(int i=0;i<8;++i) v=v<<8|k.b[i];
  return v;
}
inline Dirent dirent(std::uint32_t prefix,std::uint64_t version) {
  Dirent d;
  for(unsigned i=0;i<8;++i) d.w[i]=(std::uint64_t(prefix)<<16|version)*(2*i+1);
  return d;
}

struct Workload {
  unsigned layer;
  std::size_t count;
  bool dense;  // half the prefixes of one region of 2*count, else spread over the layer
  std::uint64_t lo{},hi{};  // birth domain [lo, hi)
};

// Prefixes in order with monotone logical fences. One gap in twenty is zero,
// so equal keys cross Segment (and so partition) boundaries.
inline std::map<std::uint32_t,Entry> populate(Workload& w,Rng& rng) {
  auto domain=std::uint64_t(1)<<(8*w.layer);
  std::vector<std::uint32_t> ps;
  if(w.dense) {
    w.lo=std::min<std::uint64_t>(domain-2*w.count,UINT64_C(0x12000000)>>(32-8*w.layer));
    w.hi=w.lo+2*w.count;
    std::vector<std::uint32_t> all(2*w.count);
    for(std::size_t i=0;i<all.size();++i) all[i]=std::uint32_t(w.lo+i);
    std::shuffle(all.begin(),all.end(),rng);
    ps.assign(all.begin(),all.begin()+w.count);
  } else {
    w.lo=0;w.hi=domain;
    std::unordered_set<std::uint32_t> seen;
    while(seen.size()<w.count) seen.insert(std::uint32_t(rng()%domain));
    ps.assign(seen.begin(),seen.end());
  }
  std::sort(ps.begin(),ps.end());
  std::map<std::uint32_t,Entry> out;
  std::uint64_t v=0;
  for(auto p:ps) {
    v+=rng()%20?1+rng()%(UINT64_C(1)<<30):0;
    Entry e{p,key(v),{},dirent(p,0)};
    v+=rng()%(UINT64_C(1)<<28);
    e.max=key(v);
    out.emplace(p,e);
  }
  return out;
}

// Fences for a new Segment between its ordered neighbours.
inline Entry born(const std::map<std::uint32_t,Entry>& o,std::uint32_t p,std::uint64_t version) {
  auto next=o.lower_bound(p);
  std::uint64_t lo=next==o.begin()?0:value(std::prev(next)->second.max);
  std::uint64_t hi=next==o.end()?lo+(UINT64_C(1)<<31):value(next->second.min);
  return {p,key(lo+(hi-lo)/3),key(lo+2*(hi-lo)/3),dirent(p,version)};
}
// Widened fences for an existing Segment that gained records at both ends.
inline Entry widened(const std::map<std::uint32_t,Entry>& o,std::uint32_t p,std::uint64_t version) {
  auto it=o.find(p);
  auto e=it->second;
  std::uint64_t lo=it==o.begin()?0:value(std::prev(it)->second.max);
  std::uint64_t hi=std::next(it)==o.end()?value(e.max)+(UINT64_C(1)<<31):value(std::next(it)->second.min);
  e.min=key(lo+(value(e.min)-lo)/2);
  e.max=key(value(e.max)+(hi-value(e.max))/2);
  e.dirent=dirent(p,version);
  return e;
}
inline std::uint32_t absent(const std::map<std::uint32_t,Entry>& o,const Workload& w,Rng& rng) {
  for(;;) {
    auto p=std::uint32_t(w.lo+rng()%(w.hi-w.lo));
    if(!o.contains(p)) return p;
  }
}
inline std::uint32_t present(const std::map<std::uint32_t,Entry>& o,const std::vector<std::uint32_t>& keys,Rng& rng) {
  for(;;) {
    auto p=keys[rng()%keys.size()];
    if(o.contains(p)) return p;
  }
}
}
