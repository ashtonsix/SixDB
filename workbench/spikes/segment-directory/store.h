#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Copy-on-write Object model with exact directory-only counters. Objects keep
// stable names across versions, as Orbital Objects do; a version is a list of
// immutable Parts and unchanged Parts are shared with the previous version.
// Writes count new Part bytes. Reads count Object visits and distinct 64-byte
// lines, assuming each Part starts on a line boundary.
namespace segdir {
struct Part {
  std::uint64_t id{};
  std::vector<std::byte> bytes;
};
using PartPtr=std::shared_ptr<const Part>;
struct Object {std::vector<PartPtr> parts;};
using ObjectPtr=std::shared_ptr<const Object>;

struct Written {
  std::uint64_t bytes{},parts{},objects{},erased{};
  Written operator-(const Written& o) const {return {bytes-o.bytes,parts-o.parts,objects-o.objects,erased-o.erased};}
};

struct Trace {
  std::unordered_set<std::uint64_t> lines;
  std::uint64_t visits{};
  void clear() {lines.clear();visits=0;}
};
inline Trace* trace=nullptr;

// Timing builds define SEGDIR_TRACE=0 so reads carry no counting branches.
#ifndef SEGDIR_TRACE
#define SEGDIR_TRACE 1
#endif

inline void touch([[maybe_unused]] const Part& p,[[maybe_unused]] std::size_t offset,[[maybe_unused]] std::size_t length) {
#if SEGDIR_TRACE
  if(!trace||!length) return;
  for(auto line=offset>>6;line<=(offset+length-1)>>6;++line) trace->lines.insert(p.id<<24|line);
#endif
}
template<class T> T load(const Part& p,std::size_t index) {
  T value;
  touch(p,index*sizeof(T),sizeof(T));
  std::memcpy(&value,p.bytes.data()+index*sizeof(T),sizeof(T));
  return value;
}
inline const std::byte* bytes(const Part& p,std::size_t offset,std::size_t length) {
  touch(p,offset,length);
  return p.bytes.data()+offset;
}

template<class T> std::vector<std::byte> encode(const std::vector<T>& values) {
  std::vector<std::byte> out(values.size()*sizeof(T));
  if(!values.empty()) std::memcpy(out.data(),values.data(),out.size());
  return out;
}
template<class T> std::vector<T> decode(const Part& p) {
  std::vector<T> out(p.bytes.size()/sizeof(T));
  if(!out.empty()) std::memcpy(out.data(),p.bytes.data(),p.bytes.size());
  return out;
}

class Store {
  std::unordered_map<std::uint64_t,ObjectPtr> objects_;
  std::uint64_t next_part_{1};
public:
  Written written;
  ObjectPtr get(std::uint64_t name) const {
#if SEGDIR_TRACE
    if(trace) ++trace->visits;
#endif
    return peek(name);
  }
  // Writers inspect their basis without charging read counters.
  ObjectPtr peek(std::uint64_t name) const {
    auto it=objects_.find(name);
    return it==objects_.end()?nullptr:it->second;
  }
  PartPtr part(std::vector<std::byte> b) {
    written.bytes+=b.size();
    ++written.parts;
    return std::make_shared<Part>(Part{next_part_++,std::move(b)});
  }
  // Keeps the basis Part at the same index when its bytes are unchanged.
  PartPtr part(const ObjectPtr& basis,std::size_t i,std::vector<std::byte> b) {
    if(basis&&i<basis->parts.size()&&basis->parts[i]->bytes==b) return basis->parts[i];
    return part(std::move(b));
  }
  void put(std::uint64_t name,std::vector<PartPtr> parts) {
    auto& slot=objects_[name];
    if(slot&&slot->parts.size()==parts.size()) {
      bool same=true;
      for(std::size_t i=0;i<parts.size();++i) same&=slot->parts[i]==parts[i];
      if(same) return;
    }
    ++written.objects;
    slot=std::make_shared<Object>(Object{std::move(parts)});
  }
  void erase(std::uint64_t name) {
    if(objects_.erase(name)) ++written.erased;
  }
  template<class F> void each(F&& f) const {for(auto& [name,o]:objects_) f(name,*o);}
  std::uint64_t resident_bytes(std::uint64_t name_space) const {
    std::uint64_t total=0;
    for(auto& [name,o]:objects_) if(name>>40==name_space) for(auto& p:o->parts) total+=p->bytes.size();
    return total;
  }
  std::uint64_t resident_objects(std::uint64_t name_space) const {
    std::uint64_t total=0;
    for(auto& [name,o]:objects_) total+=name>>40==name_space;
    return total;
  }
};
}
