#pragma once
#include "layer.h"
#include <cstdlib>
#include <stdexcept>

// Lean read paths for timing. An Image flattens one Store into a 64-byte-aligned
// arena; each Object is a 64-byte record found by name in one open-addressing
// table, holding its Part offsets. A visit is one probe, usually one line.
// The views below read exactly the layouts Tree and Natural publish.
namespace segdir {
class Image {
public:
  static constexpr unsigned max_parts=13;
  struct alignas(64) Record {
    std::uint64_t name;
    std::uint32_t parts;
    std::uint32_t offset[max_parts];  // in 64-byte units
  };
  static_assert(sizeof(Record)==64);

  explicit Image(const Store& s) {
    std::size_t objects=0,lines=0;
    s.each([&](std::uint64_t,const Object& o) {
      ++objects;
      for(auto& p:o.parts) lines+=(p->bytes.size()+63)/64;
    });
    slots_=std::bit_ceil(std::max<std::size_t>(16,objects*2));
    table_=static_cast<Record*>(std::aligned_alloc(64,slots_*sizeof(Record)));
    arena_=static_cast<std::byte*>(std::aligned_alloc(64,std::max<std::size_t>(64,lines*64)));
    std::memset(table_,0,slots_*sizeof(Record));
    std::size_t at=0;
    s.each([&](std::uint64_t name,const Object& o) {
      if(o.parts.size()>max_parts) throw std::length_error("image: too many Parts");
      auto& r=table_[probe(name)];
      r.name=name;
      r.parts=std::uint32_t(o.parts.size());
      for(std::size_t i=0;i<o.parts.size();++i) {
        r.offset[i]=std::uint32_t(at);
        std::memcpy(arena_+at*64,o.parts[i]->bytes.data(),o.parts[i]->bytes.size());
        at+=(o.parts[i]->bytes.size()+63)/64;
      }
    });
  }
  Image(const Image&)=delete;
  Image& operator=(const Image&)=delete;
  ~Image() {std::free(table_);std::free(arena_);}

  const Record* get(std::uint64_t name) const {
    auto& r=table_[probe(name)];
    return r.name==name?&r:nullptr;
  }
  const std::byte* part(const Record& r,unsigned i) const {return arena_+std::size_t(r.offset[i])*64;}
  template<class T> const T* plane(const Record& r,unsigned i) const {
    return reinterpret_cast<const T*>(part(r,i));
  }
private:
  Record* table_{};
  std::byte* arena_{};
  std::size_t slots_{};
  std::size_t probe(std::uint64_t name) const {
    auto i=std::size_t((name*UINT64_C(0x9e3779b97f4a7c15))>>20)&(slots_-1);
    while(table_[i].name&&table_[i].name!=name) i=(i+1)&(slots_-1);
    return i;
  }
};

namespace detail {
constexpr unsigned block=16;
// First index in [begin, n) where less() fails, optionally narrowed to one
// 16-entry block through a summary of every 16th entry.
template<class SummaryLess,class Less>
inline unsigned first_not(bool summary,unsigned begin,unsigned n,SummaryLess sl,Less less) {
  unsigned lo=begin,hi=n;
  if(summary&&n>block) {
    unsigned blo=1,bhi=(n+block-1)/block;
    while(blo<bhi) {auto mid=(blo+bhi)/2;if(sl(mid)) blo=mid+1;else bhi=mid;}
    lo=std::max(begin,(blo-1)*block);
    hi=std::min(n,blo*block);
  }
  while(lo<hi) {auto mid=(lo+hi)/2;if(less(mid)) lo=mid+1;else hi=mid;}
  return lo;
}
}

class TreeView {
public:
  using Record=Image::Record;
  struct Cursor {
    const Record* stack[12];
    unsigned index[12];
    unsigned depth{};
    const Record* leaf{};
    unsigned rank{},count{};
    explicit operator bool() const {return leaf&&rank<count;}
  };
  TreeView(const Image& im,const Tree& t)
    :im_(&im),c_(t.config()),root_(t.root()),page_base_(t.page_name(0)),
     level_(t.page_level()),split_(t.page_split()),child_(c_.fenced()?3:2),dirent_(c_.fenced()?4:2),
     stride_(c_.inline_hash?72:16) {}

  const Dirent* find(std::uint32_t p) const {
    if(c_.inline_hash) return reinterpret_cast<const Dirent*>(page(p));
    const Record* o;
    if(c_.hash_index) {
      auto* slot=page(p);
      if(!slot) return nullptr;
      std::uint64_t leaf;
      std::memcpy(&leaf,slot,8);
      o=im_->get(leaf);
    } else {
      o=im_->get(root_);
      if(!o) return nullptr;
      while(height(*o)) o=im_->get(child(*o,route(*o,p)));
    }
    auto n=size(*o);
    auto r=lower(*o,p);
    if(r==n||im_->plane<std::uint32_t>(*o,1)[r]!=p) return nullptr;
    return dirent(*o,r);
  }
  Cursor seek(std::uint32_t p) const {
    Cursor c;
    auto* o=im_->get(root_);
    if(!o) return c;
    while(height(*o)) {
      auto i=route(*o,p);
      push(c,o,i);
      o=im_->get(child(*o,i));
    }
    return settle(c,o,lower(*o,p));
  }
  Cursor seek(const Key& low) const {
    Cursor c;
    auto* o=im_->get(root_);
    if(!o) return c;
    while(height(*o)) {
      auto i=route(*o,low);
      push(c,o,i);
      o=im_->get(child(*o,i));
    }
    return settle(c,o,lower(*o,low));
  }
  bool next(Cursor& c) const {
    if(++c.rank<c.count) return true;
    while(c.depth) {
      auto* o=c.stack[c.depth-1];
      auto& i=c.index[c.depth-1];
      if(++i<size(*o)) {
        auto* ch=im_->get(child(*o,i));
        while(height(*ch)) {push(c,ch,0);ch=im_->get(child(*ch,0));}
        c.leaf=ch;c.rank=0;c.count=size(*ch);
        return true;
      }
      --c.depth;
    }
    c.leaf=nullptr;
    return false;
  }
  std::uint32_t prefix(const Cursor& c) const {return im_->plane<std::uint32_t>(*c.leaf,1)[c.rank];}
  const Dirent* read(const Cursor& c) const {
    if(c_.inline_hash) return reinterpret_cast<const Dirent*>(page(prefix(c)));
    return dirent(*c.leaf,c.rank);
  }

private:
  const Image* im_;
  Config c_;
  std::uint64_t root_,page_base_;
  unsigned level_;
  std::uint64_t split_;
  unsigned child_,dirent_,stride_;

  unsigned size(const Record& r) const {return im_->plane<std::uint32_t>(r,0)[0];}
  unsigned height(const Record& r) const {return im_->plane<std::uint32_t>(r,0)[1];}
  std::uint64_t child(const Record& r,unsigned i) const {return im_->plane<std::uint64_t>(r,child_)[i];}
  const Dirent* dirent(const Record& r,unsigned i) const {
    auto t=c_.tile?i/c_.tile:0,o=c_.tile?i%c_.tile:i;
    return im_->plane<Dirent>(r,dirent_+t)+o;
  }
  static void push(Cursor& c,const Record* o,unsigned i) {c.stack[c.depth]=o;c.index[c.depth++]=i;}
  Cursor& settle(Cursor& c,const Record* o,unsigned r) const {
    c.leaf=o;c.rank=r;c.count=size(*o);
    if(r==c.count) {c.rank=c.count-1;next(c);}
    return c;
  }
  unsigned route(const Record& o,std::uint32_t p) const {
    auto* sum=im_->plane<std::uint32_t>(o,0)+2;
    auto* sep=im_->plane<std::uint32_t>(o,1);
    return detail::first_not(c_.summary,1,size(o),[&](unsigned b) {return sum[b]<=p;},
                             [&](unsigned i) {return sep[i]<=p;})-1;
  }
  unsigned lower(const Record& o,std::uint32_t p) const {
    auto* sum=im_->plane<std::uint32_t>(o,0)+2;
    auto* pre=im_->plane<std::uint32_t>(o,1);
    return detail::first_not(c_.summary,0,size(o),[&](unsigned b) {return sum[b]<p;},
                             [&](unsigned i) {return pre[i]<p;});
  }
  unsigned route(const Record& o,const Key& low) const {
    auto* sum=im_->part(o,o.parts-1);
    auto* sep=im_->part(o,2);
    auto k=c_.k;
    return detail::first_not(c_.summary,1,size(o),[&](unsigned b) {return std::memcmp(sum+b*k,low.b.data(),k)<0;},
                             [&](unsigned i) {return std::memcmp(sep+i*k,low.b.data(),k)<0;})-1;
  }
  unsigned lower(const Record& o,const Key& low) const {
    auto* sum=im_->part(o,o.parts-1);
    auto* max=im_->part(o,3);
    auto k=c_.k;
    return detail::first_not(c_.summary,0,size(o),[&](unsigned b) {return std::memcmp(sum+b*k,low.b.data(),k)<0;},
                             [&](unsigned i) {return std::memcmp(max+i*k,low.b.data(),k)<0;});
  }
  const std::byte* page(std::uint32_t p) const {
    auto h=bucket_hash(p);
    auto b=h&((std::uint64_t(1)<<level_)-1);
    if(b<split_) b=h&((std::uint64_t(2)<<level_)-1);
    auto* o=im_->get(page_base_|b);
    if(!o) return nullptr;
    auto cap=im_->plane<std::uint32_t>(*o,0)[1];
    auto* slots=im_->part(*o,1);
    for(auto s=slot_hash(p)&(cap-1);;s=(s+1)&(cap-1)) {
      auto* slot=slots+s*stride_;
      std::uint32_t key,used;
      std::memcpy(&key,slot,4);
      std::memcpy(&used,slot+4,4);
      if(!used) return nullptr;
      if(key==p) return slot+8;
    }
  }
};

class NaturalView {
public:
  using Record=Image::Record;
  struct Cursor {
    const Record* group{};
    unsigned group_rank{},rank{},count{};
    explicit operator bool() const {return group&&rank<count;}
  };
  NaturalView(const Image& im,const Natural& n):im_(&im),n_(&n),bits_(n.bits()) {
    roster_=im.get(n.roster());
    groups_=im.plane<std::uint32_t>(*roster_,0)[0];
    ids_=im.plane<std::uint32_t>(*roster_,1);
  }
  const Dirent* find(std::uint32_t p) const {
    auto* o=im_->get(n_->name(std::uint32_t(std::uint64_t(p)>>bits_)));
    return o?probe(*o,p):nullptr;
  }
  Cursor seek(std::uint32_t p) const {
    auto g=std::uint32_t(std::uint64_t(p)>>bits_);
    Cursor c;
    auto gr=unsigned(std::lower_bound(ids_,ids_+groups_,g)-ids_);
    if(!enter(c,gr)) return c;
    auto* pre=im_->plane<std::uint32_t>(*c.group,1);
    c.rank=unsigned(std::lower_bound(pre,pre+c.count,p)-pre);
    if(c.rank==c.count) enter(c,gr+1);
    return c;
  }
  bool next(Cursor& c) const {
    if(++c.rank<c.count) return true;
    return enter(c,c.group_rank+1);
  }
  std::uint32_t prefix(const Cursor& c) const {return im_->plane<std::uint32_t>(*c.group,1)[c.rank];}
  const Dirent* read(const Cursor& c) const {return probe(*c.group,prefix(c));}
private:
  const Image* im_;
  const Natural* n_;
  unsigned bits_,groups_{};
  const Record* roster_{};
  const std::uint32_t* ids_{};
  bool enter(Cursor& c,unsigned gr) const {
    if(gr>=groups_) {c.group=nullptr;return false;}
    c.group_rank=gr;
    c.group=im_->get(n_->name(ids_[gr]));
    c.rank=0;
    c.count=im_->plane<std::uint32_t>(*c.group,0)[0];
    return true;
  }
  const Dirent* probe(const Record& o,std::uint32_t p) const {
    auto cap=im_->plane<std::uint32_t>(o,0)[1];
    auto* t=im_->part(o,2);
    for(auto s=slot_hash(p)&(cap-1);;s=(s+1)&(cap-1)) {
      std::uint32_t used,key;
      std::memcpy(&used,t+s*72,4);
      std::memcpy(&key,t+s*72+4,4);
      if(!used) return nullptr;
      if(key==p) return reinterpret_cast<const Dirent*>(t+s*72+8);
    }
  }
};
}
