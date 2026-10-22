#include "layer.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <climits>
#include <map>
#include <stdexcept>

namespace segdir {
namespace {
constexpr std::size_t row_bytes=64;
constexpr unsigned summary_block=16;
constexpr std::size_t natural_slot=72;

void require(bool ok,const char* what) {
  if(!ok) throw std::logic_error(what);
}
std::vector<std::byte> header(std::uint32_t a,std::uint32_t b) {
  std::vector<std::byte> h(8);
  std::memcpy(h.data(),&a,4);
  std::memcpy(h.data()+4,&b,4);
  return h;
}
std::pair<std::uint32_t,std::uint32_t> header(const Object& o) {
  return {load<std::uint32_t>(*o.parts[0],0),load<std::uint32_t>(*o.parts[0],1)};
}
std::size_t table_capacity(std::size_t n,double load_factor) {
  return std::bit_ceil(std::max<std::size_t>(8,std::size_t(double(n)/load_factor)+1));
}
}

Tree::Tree(Store& s,Config c,std::uint64_t ns):store_(&s),c_(c),ns_(ns) {}

int Tree::cmp(const Key& a,const Key& b) const {return std::memcmp(a.b.data(),b.b.data(),c_.k);}
int Tree::cmp(const std::byte* a,const Key& b) const {return std::memcmp(a,b.b.data(),c_.k);}

// Leaf parts: header, prefixes, [min, max], dirent tiles.
// Interior parts: header, separators, [logical separators], children, [rows].
Tree::Node Tree::decode(const ObjectPtr& o) const {
  Node n;
  if(!o) return n;
  std::uint32_t size,height;
  std::memcpy(&size,o->parts[0]->bytes.data(),4);
  std::memcpy(&height,o->parts[0]->bytes.data()+4,4);
  n.height=height;
  auto keys=[&](const Part& p) {
    std::vector<Key> out(size);
    for(std::size_t i=0;i<size;++i) std::memcpy(out[i].b.data(),p.bytes.data()+i*c_.k,c_.k);
    return out;
  };
  std::size_t i=1;
  if(!height) {
    n.prefix=segdir::decode<std::uint32_t>(*o->parts[i++]);
    if(c_.fenced()) {n.min=keys(*o->parts[i++]);n.max=keys(*o->parts[i++]);}
    auto end=o->parts.size()-(c_.summary&&c_.fenced());
    for(;i<end;++i) {
      auto tile=segdir::decode<Dirent>(*o->parts[i]);
      n.dirent.insert(n.dirent.end(),tile.begin(),tile.end());
    }
  } else {
    n.sep=segdir::decode<std::uint32_t>(*o->parts[i++]);
    if(c_.fenced()) n.lsep=keys(*o->parts[i++]);
    n.child=segdir::decode<std::uint64_t>(*o->parts[i++]);
    if(c_.exact_bounds) {
      auto& rows=o->parts[i++]->bytes;
      n.last.resize(size);
      n.lmax.resize(size);
      for(std::size_t r=0;r<size;++r) {
        std::memcpy(&n.last[r],rows.data()+r*row_bytes+12,4);
        std::memcpy(n.lmax[r].b.data(),rows.data()+r*row_bytes+32,16);
      }
    }
  }
  return n;
}

void Tree::write(std::uint64_t name,const Node& n) {
  auto basis=store_->peek(name);
  std::vector<PartPtr> parts;
  auto add=[&](std::vector<std::byte> b) {parts.push_back(store_->part(basis,parts.size(),std::move(b)));};
  auto keys=[&](const std::vector<Key>& ks) {
    std::vector<std::byte> b(ks.size()*c_.k);
    for(std::size_t i=0;i<ks.size();++i) std::memcpy(b.data()+i*c_.k,ks[i].b.data(),c_.k);
    return b;
  };
  auto h=header(std::uint32_t(n.size()),n.height);
  const auto& plane=n.height?n.sep:n.prefix;
  if(c_.summary) for(std::size_t i=0;i<plane.size();i+=summary_block) {
    h.resize(h.size()+4);
    std::memcpy(h.data()+h.size()-4,&plane[i],4);
  }
  add(std::move(h));
  if(!n.height) {
    add(encode(n.prefix));
    if(c_.fenced()) {add(keys(n.min));add(keys(n.max));}
    if(!c_.tile) add(encode(n.dirent));
    else for(std::size_t t=0;t<n.dirent.size();t+=c_.tile)
      add(encode(std::vector<Dirent>(n.dirent.begin()+t,n.dirent.begin()+std::min(n.dirent.size(),t+c_.tile))));
  } else {
    add(encode(n.sep));
    if(c_.fenced()) add(keys(n.lsep));
    add(encode(n.child));
    if(c_.exact_bounds) {
      std::vector<std::byte> rows(n.size()*row_bytes);
      for(std::size_t r=0;r<n.size();++r) {
        auto* row=rows.data()+r*row_bytes;
        std::memcpy(row,&n.child[r],8);
        std::memcpy(row+8,&n.sep[r],4);
        std::memcpy(row+12,&n.last[r],4);
        if(c_.fenced()) std::memcpy(row+16,n.lsep[r].b.data(),16);
        std::memcpy(row+32,n.lmax[r].b.data(),16);
      }
      add(std::move(rows));
    }
  }
  if(c_.summary&&c_.fenced()) {
    const auto& ks=n.height?n.lsep:n.max;
    std::vector<Key> sampled;
    for(std::size_t i=0;i<ks.size();i+=summary_block) sampled.push_back(ks[i]);
    add(keys(sampled));
  }
  store_->put(name,std::move(parts));
}

void Tree::set_row(Node& parent,unsigned i,const Node& child) const {
  if(child.height) {
    parent.sep[i]=child.sep[0];
    if(c_.fenced()) parent.lsep[i]=child.lsep[0];
    return;
  }
  parent.sep[i]=child.prefix[0];
  if(c_.fenced()) parent.lsep[i]=child.min[0];
  if(c_.exact_bounds) {
    parent.last[i]=child.prefix.back();
    parent.lmax[i]=c_.fenced()?child.max.back():Key{};
  }
}

void Tree::insert_rows(Node& parent,unsigned at,const std::vector<Node>& children,
                       const std::vector<std::uint64_t>& names) const {
  auto n=children.size();
  parent.sep.insert(parent.sep.begin()+at,n,0);
  if(c_.fenced()) parent.lsep.insert(parent.lsep.begin()+at,n,Key{});
  parent.child.insert(parent.child.begin()+at,names.begin(),names.end());
  if(c_.exact_bounds) {
    parent.last.insert(parent.last.begin()+at,n,0);
    parent.lmax.insert(parent.lmax.begin()+at,n,Key{});
  }
  for(std::size_t i=0;i<n;++i) set_row(parent,unsigned(at+i),children[i]);
}

void Tree::remove_row(Node& parent,unsigned i) const {
  parent.sep.erase(parent.sep.begin()+i);
  if(c_.fenced()) parent.lsep.erase(parent.lsep.begin()+i);
  parent.child.erase(parent.child.begin()+i);
  if(c_.exact_bounds) {
    parent.last.erase(parent.last.begin()+i);
    parent.lmax.erase(parent.lmax.begin()+i);
  }
}

std::vector<Tree::Node> Tree::split(const Node& n,unsigned pieces) const {
  std::vector<std::size_t> bounds;
  for(unsigned p=0;p<=pieces;++p) bounds.push_back(n.size()*p/pieces);
  return split_at(n,bounds);
}

std::vector<Tree::Node> Tree::split_at(const Node& n,const std::vector<std::size_t>& bounds) const {
  std::vector<Node> out(bounds.size()-1);
  for(std::size_t p=0;p+1<bounds.size();++p) {
    auto b=bounds[p],e=bounds[p+1];
    auto& o=out[p];
    o.height=n.height;
    auto slice=[&](auto& to,const auto& from) {if(!from.empty()) to.assign(from.begin()+b,from.begin()+e);};
    slice(o.prefix,n.prefix);slice(o.min,n.min);slice(o.max,n.max);slice(o.dirent,n.dirent);
    slice(o.sep,n.sep);slice(o.lsep,n.lsep);slice(o.child,n.child);slice(o.last,n.last);slice(o.lmax,n.lmax);
  }
  return out;
}

Tree::Node Tree::concat(Node left,const Node& right,const Node& parent,unsigned right_index) const {
  auto append=[](auto& to,const auto& from) {to.insert(to.end(),from.begin(),from.end());};
  append(left.prefix,right.prefix);append(left.min,right.min);append(left.max,right.max);append(left.dirent,right.dirent);
  auto at=left.sep.size();
  append(left.sep,right.sep);append(left.lsep,right.lsep);append(left.child,right.child);
  append(left.last,right.last);append(left.lmax,right.lmax);
  // The right node's first separator is unused until it becomes an interior row.
  if(left.height) {
    left.sep[at]=parent.sep[right_index];
    if(c_.fenced()) left.lsep[at]=parent.lsep[right_index];
  }
  return left;
}

void Tree::moved(const Node& leaf,std::uint64_t name) {
  if(!c_.hash_index) return;
  Dirent d{};
  d.w[0]=name;
  for(auto p:leaf.prefix) pending_[p]=d;
}

std::vector<Tree::Step> Tree::descend(std::uint32_t p) const {
  std::vector<Step> path;
  auto name=root();
  auto node=decode(store_->peek(name));
  while(node.height) {
    auto j=std::upper_bound(node.sep.begin()+1,node.sep.end(),p)-node.sep.begin();
    auto child=node.child[j-1];
    auto size=node.size();
    path.push_back({name,std::move(node),unsigned(j-1),false,size});
    name=child;
    node=decode(store_->peek(name));
  }
  auto size=node.size();
  path.push_back({name,std::move(node),0,true,size});
  return path;
}

void Tree::settle(std::vector<Step>& path) {
  auto& leaf=path.back().node;
  if(leaf.size()&&path.size()>1) {
    if(c_.exact_bounds) {
      auto& parent=path[path.size()-2];
      auto before=std::make_tuple(parent.node.sep[parent.index],parent.node.last[parent.index]);
      auto lbefore=c_.fenced()?std::make_pair(parent.node.lsep[parent.index],parent.node.lmax[parent.index]):std::pair<Key,Key>{};
      set_row(parent.node,parent.index,leaf);
      auto lafter=c_.fenced()?std::make_pair(parent.node.lsep[parent.index],parent.node.lmax[parent.index]):std::pair<Key,Key>{};
      if(before!=std::make_tuple(parent.node.sep[parent.index],parent.node.last[parent.index])||
         cmp(lbefore.first,lafter.first)||cmp(lbefore.second,lafter.second)) parent.dirty=true;
    } else if(c_.fenced()) {
      // Only an edit beside a separator can violate it; deaths leave it loose.
      for(auto i=path.size()-1;i-->0;) if(path[i].index>0) {
        auto& s=path[i];
        if(cmp(s.node.lsep[s.index],leaf.min[0])>0) {s.node.lsep[s.index]=leaf.min[0];s.dirty=true;}
        break;
      }
      for(auto i=path.size()-1;i-->0;) if(path[i].index+1<path[i].node.size()) {
        auto& s=path[i];
        if(cmp(s.node.lsep[s.index+1],leaf.max.back())<0) {s.node.lsep[s.index+1]=leaf.max.back();s.dirty=true;}
        break;
      }
    }
  }
  // The rightmost node on each level takes appends; it splits off full
  // build-occupancy pieces instead of halves so appended runs stay dense.
  std::vector<bool> rightmost(path.size(),true);
  for(std::size_t i=1;i<path.size();++i)
    rightmost[i]=rightmost[i-1]&&path[i-1].index+1==path[i-1].node.size();
  for(auto i=path.size();i-->0;) {
    auto& s=path[i];
    auto* parent=i?&path[i-1]:nullptr;
    unsigned cap=s.node.height?c_.inner_capacity:c_.leaf_capacity;
    bool may_split=parent||c_.bounded_root||!s.node.height;
    if(s.node.size()>cap&&may_split) {
      std::vector<Node> pieces;
      if(c_.append_split&&rightmost[i]) {
        auto per=std::max<std::size_t>(1,std::size_t(cap*c_.fill));
        std::vector<std::size_t> bounds{0};
        while(s.node.size()-bounds.back()>cap) bounds.push_back(bounds.back()+per);
        bounds.push_back(s.node.size());
        pieces=split_at(s.node,bounds);
      } else {
        pieces=split(s.node,unsigned(s.node.size()/cap+1));
      }
      if(!parent) {
        Node r;
        r.height=s.node.height+1;
        std::vector<std::uint64_t> names;
        for(auto& p:pieces) {
          auto name=fresh();
          write(name,p);
          if(!p.height) moved(p,name);
          names.push_back(name);
        }
        insert_rows(r,0,pieces,names);
        while(c_.bounded_root&&r.size()>c_.inner_capacity) {
          auto up=split(r,unsigned(r.size()/c_.inner_capacity+1));
          names.clear();
          for(auto& p:up) {auto name=fresh();write(name,p);names.push_back(name);}
          Node top;
          top.height=r.height+1;
          insert_rows(top,0,up,names);
          r=std::move(top);
        }
        write(root(),r);
      } else {
        write(s.name,pieces[0]);
        std::vector<Node> rest(pieces.begin()+1,pieces.end());
        std::vector<std::uint64_t> names;
        for(auto& p:rest) {
          auto name=fresh();
          write(name,p);
          if(!p.height) moved(p,name);
          names.push_back(name);
        }
        if(c_.exact_bounds) set_row(parent->node,parent->index,pieces[0]);
        insert_rows(parent->node,parent->index+1,rest,names);
        parent->dirty=true;
      }
    } else if(!s.node.size()) {
      store_->erase(s.name);
      if(parent) {remove_row(parent->node,parent->index);parent->dirty=true;}
    } else if(parent&&s.node.size()<s.before&&s.node.size()<cap*c_.merge_below&&parent->node.size()>1) {
      auto& pn=parent->node;
      unsigned li,ri;
      std::uint64_t ln,rn;
      Node left,right;
      if(parent->index+1<pn.size()) {
        li=parent->index;ri=li+1;ln=s.name;rn=pn.child[ri];
        left=std::move(s.node);right=decode(store_->peek(rn));
      } else {
        ri=parent->index;li=ri-1;rn=s.name;ln=pn.child[li];
        right=std::move(s.node);left=decode(store_->peek(ln));
      }
      auto both=concat(std::move(left),right,pn,ri);
      if(both.size()<=cap) {
        write(ln,both);
        store_->erase(rn);
        if(!both.height) moved(right,ln);
        remove_row(pn,ri);
        if(c_.exact_bounds) set_row(pn,li,both);
      } else {
        auto halves=split(both,2);
        write(ln,halves[0]);
        write(rn,halves[1]);
        if(!both.height) {moved(halves[0],ln);moved(halves[1],rn);}
        set_row(pn,ri,halves[1]);
        if(c_.exact_bounds) set_row(pn,li,halves[0]);
      }
      parent->dirty=true;
    } else if(!parent&&s.node.height&&s.node.size()==1&&c_.bounded_root) {
      auto child=s.node.child[0];
      auto node=decode(store_->peek(child));
      write(root(),node);
      store_->erase(child);
      if(!node.height) moved(node,root());
    } else if(s.dirty) {
      write(s.name,s.node);
    }
  }
}

void Tree::edit_leaf(Step& s,const Edit& ed) {
  auto& e=ed.entry;
  auto& leaf=s.node;
  auto r=std::lower_bound(leaf.prefix.begin(),leaf.prefix.end(),e.prefix)-leaf.prefix.begin();
  bool present=r<std::ptrdiff_t(leaf.prefix.size())&&leaf.prefix[r]==e.prefix;
  switch(ed.kind) {
  case Edit::insert:
    require(!present,"insert: present");
    leaf.prefix.insert(leaf.prefix.begin()+r,e.prefix);
    if(c_.fenced()) {leaf.min.insert(leaf.min.begin()+r,e.min);leaf.max.insert(leaf.max.begin()+r,e.max);}
    if(!c_.inline_hash) leaf.dirent.insert(leaf.dirent.begin()+r,e.dirent);
    if(c_.hashed()) {
      Dirent d{};
      d.w[0]=s.name;
      pending_[e.prefix]=c_.inline_hash?e.dirent:d;
      ++entries_;
    }
    break;
  case Edit::erase:
    require(present,"erase: absent");
    leaf.prefix.erase(leaf.prefix.begin()+r);
    if(c_.fenced()) {leaf.min.erase(leaf.min.begin()+r);leaf.max.erase(leaf.max.begin()+r);}
    if(!c_.inline_hash) leaf.dirent.erase(leaf.dirent.begin()+r);
    if(c_.hashed()) {pending_[e.prefix]=std::nullopt;--entries_;}
    break;
  case Edit::update:
    require(present,"update: absent");
    if(c_.fenced()) {leaf.min[r]=e.min;leaf.max[r]=e.max;}
    if(c_.inline_hash) pending_[e.prefix]=e.dirent;
    else leaf.dirent[r]=e.dirent;
    break;
  }
}

void Tree::apply(std::span<const Edit> edits) {
  for(std::size_t i=0;i<edits.size();) {
    auto path=descend(edits[i].entry.prefix);
    // Edits below the leaf's right separator belong to it.
    std::uint64_t upper=UINT64_C(1)<<32;
    for(auto j=path.size()-1;j-->0;)
      if(path[j].index+1<path[j].node.size()) {upper=path[j].node.sep[path[j].index+1];break;}
    for(;i<edits.size()&&edits[i].entry.prefix<upper;++i) {
      require(i==0||edits[i-1].entry.prefix<edits[i].entry.prefix,"apply: unsorted");
      edit_leaf(path.back(),edits[i]);
    }
    settle(path);
  }
  flush_pages();
}

void Tree::insert(const Entry& e) {Edit ed{Edit::insert,e};apply({&ed,1});}
void Tree::erase(std::uint32_t p) {Edit ed{Edit::erase,{}};ed.entry.prefix=p;apply({&ed,1});}
void Tree::update(const Entry& e) {Edit ed{Edit::update,e};apply({&ed,1});}

void Tree::build(std::span<const Entry> es) {
  require(!store_->peek(root()),"build: not empty");
  auto per=std::max<std::size_t>(1,std::size_t(c_.leaf_capacity*c_.fill));
  std::vector<Node> level;
  for(std::size_t i=0;i<es.size();i+=per) {
    Node n;
    for(auto j=i;j<std::min(es.size(),i+per);++j) {
      n.prefix.push_back(es[j].prefix);
      if(c_.fenced()) {n.min.push_back(es[j].min);n.max.push_back(es[j].max);}
      if(c_.inline_hash) pending_[es[j].prefix]=es[j].dirent;
      else n.dirent.push_back(es[j].dirent);
    }
    level.push_back(std::move(n));
  }
  if(level.size()==1) {write(root(),level[0]);moved(level[0],root());}
  if(level.size()>1) {
    std::vector<std::uint64_t> names;
    for(auto& n:level) {
      auto name=fresh();
      write(name,n);
      moved(n,name);
      names.push_back(name);
    }
    std::size_t fan=c_.bounded_root?std::max<std::size_t>(2,std::size_t(c_.inner_capacity*c_.fill)):SIZE_MAX;
    for(unsigned h=1;;++h) {
      std::vector<Node> up;
      for(std::size_t i=0;i<level.size();i+=std::min(fan,level.size()-i)) {
        auto e=std::min(level.size(),i+std::min(fan,level.size()-i));
        Node p;
        p.height=h;
        insert_rows(p,0,std::vector<Node>(level.begin()+i,level.begin()+e),
                    std::vector<std::uint64_t>(names.begin()+i,names.begin()+e));
        up.push_back(std::move(p));
      }
      if(up.size()==1) {write(root(),up[0]);break;}
      names.clear();
      for(auto& p:up) {
        auto name=fresh();
        write(name,p);
        names.push_back(name);
      }
      level=std::move(up);
    }
  }
  if(c_.hashed()) {
    auto p=std::max<std::uint64_t>(1,(es.size()+c_.page_target-1)/c_.page_target);
    level_=std::bit_width(p)-1;
    split_=p-(std::uint64_t(1)<<level_);
    entries_=es.size();
    flush_pages();
  }
}

// Reads charge Object visits and lines through the active Trace.
namespace {
unsigned leaf_dirent_part(const Config& c) {return c.fenced()?4:2;}
unsigned child_part(const Config& c) {return c.fenced()?3:2;}
}

// First index in [begin, n) where less() fails. With summaries, the header
// line (or key summary Part) picks one 16-entry block first.
template<class Less,class SummaryLess>
unsigned Tree::first_not(unsigned begin,unsigned n,SummaryLess summary_less,Less less) const {
  unsigned lo=begin,hi=n;
  if(c_.summary&&n>summary_block) {
    unsigned blocks=(n+summary_block-1)/summary_block,blo=1,bhi=blocks;
    while(blo<bhi) {auto mid=(blo+bhi)/2;if(summary_less(mid)) blo=mid+1;else bhi=mid;}
    // Every block before blo starts below the target; block blo does not.
    lo=std::max(begin,(blo-1)*summary_block);
    hi=std::min(n,blo*summary_block);
  }
  while(lo<hi) {auto mid=(lo+hi)/2;if(less(mid)) lo=mid+1;else hi=mid;}
  return lo;
}

unsigned Tree::route(const Object& o,unsigned n,std::uint32_t p) const {
  auto& h=*o.parts[0];
  return first_not(1,n,[&](unsigned b) {return load<std::uint32_t>(h,2+b)<=p;},
                   [&](unsigned i) {return load<std::uint32_t>(*o.parts[1],i)<=p;})-1;
}
unsigned Tree::lower(const Object& o,unsigned n,std::uint32_t p) const {
  auto& h=*o.parts[0];
  return first_not(0,n,[&](unsigned b) {return load<std::uint32_t>(h,2+b)<p;},
                   [&](unsigned i) {return load<std::uint32_t>(*o.parts[1],i)<p;});
}
unsigned Tree::route(const Object& o,unsigned n,const Key& low) const {
  auto& sum=*o.parts.back();
  return first_not(1,n,[&](unsigned b) {return cmp(bytes(sum,b*c_.k,c_.k),low)<0;},
                   [&](unsigned i) {return cmp(bytes(*o.parts[2],i*c_.k,c_.k),low)<0;})-1;
}
unsigned Tree::lower(const Object& o,unsigned n,const Key& low) const {
  auto& sum=*o.parts.back();
  return first_not(0,n,[&](unsigned b) {return cmp(bytes(sum,b*c_.k,c_.k),low)<0;},
                   [&](unsigned i) {return cmp(bytes(*o.parts[3],i*c_.k,c_.k),low)<0;});
}

std::optional<Dirent> Tree::find(std::uint32_t p) const {
  ObjectPtr o;
  if(c_.inline_hash) {
    auto* slot=page_find(p);
    if(!slot) return std::nullopt;
    Dirent d;
    std::memcpy(&d,slot,64);
    return d;
  }
  if(c_.hash_index) {
    auto* slot=page_find(p);
    if(!slot) return std::nullopt;
    std::uint64_t leaf;
    std::memcpy(&leaf,slot,8);
    o=store_->get(leaf);
  } else {
    o=store_->get(root());
    if(!o) return std::nullopt;
    for(auto [n,h]=header(*o);h;std::tie(n,h)=header(*o)) {
      o=store_->get(load<std::uint64_t>(*o->parts[child_part(c_)],route(*o,n,p)));
    }
  }
  auto n=header(*o).first;
  auto lo=lower(*o,n,p);
  if(lo==n||load<std::uint32_t>(*o->parts[1],lo)!=p) return std::nullopt;
  auto t=c_.tile?lo/c_.tile:0,r=c_.tile?lo%c_.tile:lo;
  Dirent d;
  std::memcpy(&d,bytes(*o->parts[leaf_dirent_part(c_)+t],r*64,64),64);
  return d;
}

void Tree::leftmost(Cursor& c,ObjectPtr o) const {
  for(auto [n,h]=header(*o);h;std::tie(n,h)=header(*o)) {
    c.path.push_back({o,0});
    o=store_->get(load<std::uint64_t>(*o->parts[child_part(c_)],0));
  }
  c.leaf=o;
  c.rank=0;
  c.count=header(*o).first;
}

bool Tree::next(Cursor& c) const {
  if(!c.leaf) return false;
  if(++c.rank<c.count) return true;
  while(!c.path.empty()) {
    auto& [o,i]=c.path.back();
    if(i+1<header(*o).first) {
      ++i;
      auto child=store_->get(load<std::uint64_t>(*o->parts[child_part(c_)],i));
      leftmost(c,child);
      return true;
    }
    c.path.pop_back();
  }
  c.leaf=nullptr;
  return false;
}

Tree::Cursor Tree::seek(std::uint32_t p) const {
  Cursor c;
  auto o=store_->get(root());
  if(!o) return c;
  for(auto [n,h]=header(*o);h;std::tie(n,h)=header(*o)) {
    auto i=route(*o,n,p);
    c.path.push_back({o,i});
    o=store_->get(load<std::uint64_t>(*o->parts[child_part(c_)],i));
  }
  auto n=header(*o).first;
  auto lo=lower(*o,n,p);
  c.leaf=o;c.count=n;c.rank=lo;
  if(lo==n) {c.rank=n?n-1:0;if(n) next(c);else c.leaf=nullptr;}
  return c;
}

Tree::Cursor Tree::seek(const Key& low) const {
  require(c_.fenced(),"logical seek below J uses the prefix");
  Cursor c;
  auto o=store_->get(root());
  if(!o) return c;
  for(auto [n,h]=header(*o);h;std::tie(n,h)=header(*o)) {
    auto i=route(*o,n,low);
    c.path.push_back({o,i});
    o=store_->get(load<std::uint64_t>(*o->parts[child_part(c_)],i));
  }
  auto n=header(*o).first;
  auto lo=lower(*o,n,low);
  c.leaf=o;c.count=n;c.rank=lo;
  if(lo==n) {c.rank=n?n-1:0;if(n) next(c);else c.leaf=nullptr;}
  return c;
}

Entry Tree::read(const Cursor& c) const {
  Entry e;
  auto& o=*c.leaf;
  e.prefix=load<std::uint32_t>(*o.parts[1],c.rank);
  if(c_.fenced()) {
    std::memcpy(e.min.b.data(),bytes(*o.parts[2],c.rank*c_.k,c_.k),c_.k);
    std::memcpy(e.max.b.data(),bytes(*o.parts[3],c.rank*c_.k,c_.k),c_.k);
  }
  if(c_.inline_hash) {
    std::memcpy(&e.dirent,page_find(e.prefix),64);
    return e;
  }
  auto t=c_.tile?c.rank/c_.tile:0,r=c_.tile?c.rank%c_.tile:c.rank;
  std::memcpy(&e.dirent,bytes(*o.parts[leaf_dirent_part(c_)+t],r*64,64),64);
  return e;
}

std::vector<Entry> Tree::dump() const {
  std::vector<Entry> out;
  auto* saved=trace;
  trace=nullptr;
  for(auto c=seek(0u);c;next(c)) out.push_back(read(c));
  trace=saved;
  return out;
}

std::pair<std::size_t,std::size_t> Tree::occupancy() const {
  std::size_t leaves=0,entries=0;
  auto walk=[&](auto& self,std::uint64_t name)->void {
    auto n=decode(store_->peek(name));
    if(!n.height) {++leaves;entries+=n.size();return;}
    for(auto ch:n.child) self(self,ch);
  };
  if(store_->peek(root())) walk(walk,root());
  return {leaves,entries};
}

void Tree::validate() const {
  struct Bounds {std::uint32_t first,last;Key min,max;std::size_t height;};
  auto walk=[&](auto& self,std::uint64_t name,bool is_root)->Bounds {
    auto n=decode(store_->peek(name));
    require(n.size()>0,"validate: empty node");
    unsigned cap=n.height?c_.inner_capacity:c_.leaf_capacity;
    require(n.size()<=cap||(is_root&&n.height&&!c_.bounded_root),"validate: overfull");
    if(!n.height) {
      for(std::size_t i=1;i<n.size();++i) require(n.prefix[i-1]<n.prefix[i],"validate: leaf order");
      Bounds b{n.prefix[0],n.prefix.back(),{},{},0};
      if(c_.fenced()) {b.min=n.min[0];b.max=n.max.back();}
      return b;
    }
    std::vector<Bounds> kids;
    for(auto ch:n.child) kids.push_back(self(self,ch,false));
    for(std::size_t i=0;i<kids.size();++i) {
      require(kids[i].height+1==n.height,"validate: height");
      if(i) {
        require(kids[i-1].last<n.sep[i]&&n.sep[i]<=kids[i].first,"validate: physical separator");
        if(c_.fenced()) require(cmp(kids[i-1].max,n.lsep[i])<=0&&cmp(n.lsep[i],kids[i].min)<=0,"validate: logical separator");
      }
      if(c_.exact_bounds) require(n.sep[i]==kids[i].first&&n.last[i]==kids[i].last,"validate: exact bounds");
    }
    return {kids[0].first,kids.back().last,kids[0].min,kids.back().max,n.height};
  };
  if(store_->peek(root())) walk(walk,root(),true);
  if(c_.hashed()) {
    std::uint64_t total=0;
    for(std::uint64_t p=0;p<pages();++p) {
      auto es=page_entries(p);
      total+=es.size();
      for(auto& [prefix,d]:es) {
        require(bucket(prefix)==p,"validate: page");
        auto n=c_.inline_hash?descend(prefix).back().node:decode(store_->peek(d.w[0]));
        require(!n.height&&std::binary_search(n.prefix.begin(),n.prefix.end(),prefix),"validate: page leaf");
      }
    }
    require(total==entries_,"validate: page count");
  }
}

std::uint64_t Tree::bucket(std::uint32_t p) const {
  auto h=bucket_hash(p);
  auto b=h&((std::uint64_t(1)<<level_)-1);
  if(b<split_) b=h&((std::uint64_t(2)<<level_)-1);
  return b;
}

// Page slots: prefix, occupancy, then a leaf name (8 bytes) or an inline dirent.
std::size_t Tree::slot_bytes() const {return 8+(c_.inline_hash?64:8);}

std::vector<std::pair<std::uint32_t,Dirent>> Tree::page_entries(std::uint64_t page) const {
  std::vector<std::pair<std::uint32_t,Dirent>> out;
  auto o=store_->peek(page_name(page));
  if(!o) return out;
  auto& slots=o->parts[1]->bytes;
  for(std::size_t i=0;i<slots.size();i+=slot_bytes()) {
    std::uint32_t p,used;
    std::memcpy(&p,slots.data()+i,4);
    std::memcpy(&used,slots.data()+i+4,4);
    if(!used) continue;
    Dirent d;
    std::memcpy(&d,slots.data()+i+8,slot_bytes()-8);
    out.push_back({p,d});
  }
  std::sort(out.begin(),out.end(),[](auto& x,auto& y) {return x.first<y.first;});
  return out;
}

void Tree::write_page(std::uint64_t page,const std::vector<std::pair<std::uint32_t,Dirent>>& es) {
  if(es.empty()) {store_->erase(page_name(page));return;}
  auto cap=table_capacity(es.size(),0.75);
  auto stride=slot_bytes();
  std::vector<std::byte> slots(cap*stride);
  for(auto& [p,d]:es) {
    auto s=slot_hash(p)&(cap-1);
    for(;;s=(s+1)&(cap-1)) {
      std::uint32_t used;
      std::memcpy(&used,slots.data()+s*stride+4,4);
      if(!used) break;
    }
    std::uint32_t one=1;
    std::memcpy(slots.data()+s*stride,&p,4);
    std::memcpy(slots.data()+s*stride+4,&one,4);
    std::memcpy(slots.data()+s*stride+8,&d,stride-8);
  }
  auto basis=store_->peek(page_name(page));
  store_->put(page_name(page),{store_->part(basis,0,header(std::uint32_t(es.size()),std::uint32_t(cap))),
                               store_->part(basis,1,std::move(slots))});
}

void Tree::flush_pages() {
  if(!c_.hashed()||pending_.empty()) return;
  std::map<std::uint64_t,std::vector<std::pair<std::uint32_t,std::optional<Dirent>>>> by_page;
  for(auto& [p,d]:pending_) by_page[bucket(p)].push_back({p,d});
  pending_.clear();
  auto stride=slot_bytes()-8;
  for(auto& [page,changes]:by_page) {
    auto es=page_entries(page);
    std::map<std::uint32_t,Dirent> m(es.begin(),es.end());
    bool changed=false;
    for(auto& [p,d]:changes) {
      auto it=m.find(p);
      if(!d) {if(it!=m.end()) {m.erase(it);changed=true;}}
      else if(it==m.end()||std::memcmp(&it->second,&*d,stride)) {m[p]=*d;changed=true;}
    }
    if(changed) write_page(page,{m.begin(),m.end()});
  }
  bool grew=false;
  while(entries_>pages()*c_.page_target) {
    auto from=split_,to=split_+(std::uint64_t(1)<<level_);
    auto es=page_entries(from);
    ++split_;
    if(split_==std::uint64_t(1)<<level_) {++level_;split_=0;}
    std::vector<std::pair<std::uint32_t,Dirent>> a,b;
    for(auto& e:es) (bucket(e.first)==from?a:b).push_back(e);
    require(b.empty()||bucket(b[0].first)==to,"page split");
    write_page(from,a);
    write_page(to,b);
    grew=true;
  }
  if(grew) store_->put(ns_<<40|std::uint64_t(1)<<38,{store_->part(header(level_,std::uint32_t(split_)))});
}

const std::byte* Tree::page_find(std::uint32_t p) const {
  auto o=store_->get(page_name(bucket(p)));
  if(!o) return nullptr;
  auto cap=load<std::uint32_t>(*o->parts[0],1);
  auto stride=slot_bytes();
  for(auto s=slot_hash(p)&(cap-1);;s=(s+1)&(cap-1)) {
    auto* slot=bytes(*o->parts[1],s*stride,8);
    std::uint32_t key,used;
    std::memcpy(&key,slot,4);
    std::memcpy(&used,slot+4,4);
    if(!used) return nullptr;
    if(key==p) return bytes(*o->parts[1],s*stride+8,stride-8);
  }
}

Natural::Natural(Store& s,Config c,unsigned bits,std::uint64_t ns):store_(&s),c_(c),bits_(bits),ns_(ns) {}

namespace {
std::uint32_t group_of(std::uint32_t p,unsigned bits) {return std::uint32_t(std::uint64_t(p)>>bits);}
}

std::vector<std::pair<std::uint32_t,Dirent>> Natural::entries(std::uint32_t g) const {
  std::vector<std::pair<std::uint32_t,Dirent>> out;
  auto o=store_->peek(name(g));
  if(!o) return out;
  auto& t=o->parts[2]->bytes;
  for(std::size_t i=0;i<t.size();i+=natural_slot) {
    std::uint32_t used,p;
    std::memcpy(&used,t.data()+i,4);
    std::memcpy(&p,t.data()+i+4,4);
    if(!used) continue;
    Dirent d;
    std::memcpy(&d,t.data()+i+8,64);
    out.push_back({p,d});
  }
  std::sort(out.begin(),out.end(),[](auto& a,auto& b) {return a.first<b.first;});
  return out;
}

void Natural::write(std::uint32_t g,const std::vector<std::pair<std::uint32_t,Dirent>>& es) {
  auto basis=store_->peek(name(g));
  std::vector<std::uint32_t> groups;
  if(auto r=store_->peek(roster())) groups=decode<std::uint32_t>(*r->parts[1]);
  auto it=std::lower_bound(groups.begin(),groups.end(),g);
  bool listed=it!=groups.end()&&*it==g;
  if(es.empty()) {
    store_->erase(name(g));
    if(listed) groups.erase(it);
  } else {
    auto cap=table_capacity(es.size(),0.875);
    std::vector<std::byte> table(cap*natural_slot);
    std::vector<std::uint32_t> prefixes;
    for(auto& [p,d]:es) {
      prefixes.push_back(p);
      auto s=slot_hash(p)&(cap-1);
      for(;;s=(s+1)&(cap-1)) {
        std::uint32_t used;
        std::memcpy(&used,table.data()+s*natural_slot,4);
        if(!used) break;
      }
      std::uint32_t one=1;
      std::memcpy(table.data()+s*natural_slot,&one,4);
      std::memcpy(table.data()+s*natural_slot+4,&p,4);
      std::memcpy(table.data()+s*natural_slot+8,&d,64);
    }
    store_->put(name(g),{store_->part(basis,0,header(std::uint32_t(es.size()),std::uint32_t(cap))),
                         store_->part(basis,1,encode(prefixes)),store_->part(basis,2,std::move(table))});
    if(!listed) groups.insert(it,g);
  }
  if(listed!=!es.empty()) {
    auto r=store_->peek(roster());
    store_->put(roster(),{store_->part(r,0,header(std::uint32_t(groups.size()),0)),store_->part(r,1,encode(groups))});
  }
}

void Natural::build(std::span<const Entry> es) {
  std::map<std::uint32_t,std::vector<std::pair<std::uint32_t,Dirent>>> groups;
  for(auto& e:es) groups[group_of(e.prefix,bits_)].push_back({e.prefix,e.dirent});
  for(auto& [g,v]:groups) write(g,v);
}
void Natural::insert(const Entry& e) {
  auto g=group_of(e.prefix,bits_);
  auto es=entries(g);
  auto it=std::lower_bound(es.begin(),es.end(),e.prefix,[](auto& a,std::uint32_t p) {return a.first<p;});
  require(it==es.end()||it->first!=e.prefix,"insert: present");
  es.insert(it,{e.prefix,e.dirent});
  write(g,es);
}
void Natural::erase(std::uint32_t p) {
  auto g=group_of(p,bits_);
  auto es=entries(g);
  auto it=std::lower_bound(es.begin(),es.end(),p,[](auto& a,std::uint32_t q) {return a.first<q;});
  require(it!=es.end()&&it->first==p,"erase: absent");
  es.erase(it);
  write(g,es);
}
void Natural::update(const Entry& e) {
  auto g=group_of(e.prefix,bits_);
  auto es=entries(g);
  auto it=std::lower_bound(es.begin(),es.end(),e.prefix,[](auto& a,std::uint32_t q) {return a.first<q;});
  require(it!=es.end()&&it->first==e.prefix,"update: absent");
  it->second=e.dirent;
  write(g,es);
}

const std::byte* Natural::probe(const ObjectPtr& o,std::uint32_t p) const {
  auto cap=load<std::uint32_t>(*o->parts[0],1);
  for(auto s=slot_hash(p)&(cap-1);;s=(s+1)&(cap-1)) {
    auto* slot=bytes(*o->parts[2],s*natural_slot,8);
    std::uint32_t used,key;
    std::memcpy(&used,slot,4);
    std::memcpy(&key,slot+4,4);
    if(!used) return nullptr;
    if(key==p) return bytes(*o->parts[2],s*natural_slot+8,64);
  }
}

std::optional<Dirent> Natural::find(std::uint32_t p) const {
  auto o=store_->get(name(group_of(p,bits_)));
  if(!o) return std::nullopt;
  auto* d=probe(o,p);
  if(!d) return std::nullopt;
  Dirent out;
  std::memcpy(&out,d,64);
  return out;
}

bool Natural::enter(Cursor& c,unsigned gr) const {
  for(;gr<c.groups;++gr) {
    c.group_rank=gr;
    c.group=store_->get(name(load<std::uint32_t>(*c.roster->parts[1],gr)));
    c.count=header(*c.group).first;
    c.rank=0;
    if(c.count) return true;
  }
  c.group=nullptr;
  return false;
}

Natural::Cursor Natural::seek(std::uint32_t p) const {
  Cursor c;
  c.roster=store_->get(roster());
  if(!c.roster) return c;
  c.groups=header(*c.roster).first;
  auto g=group_of(p,bits_);
  std::uint32_t lo=0,hi=c.groups;
  while(lo<hi) {auto mid=(lo+hi)/2;if(load<std::uint32_t>(*c.roster->parts[1],mid)<g) lo=mid+1;else hi=mid;}
  if(!enter(c,lo)) return c;
  lo=0;hi=c.count;
  while(lo<hi) {auto mid=(lo+hi)/2;if(load<std::uint32_t>(*c.group->parts[1],mid)<p) lo=mid+1;else hi=mid;}
  c.rank=lo;
  if(lo==c.count) enter(c,c.group_rank+1);
  return c;
}

bool Natural::next(Cursor& c) const {
  if(!c.group) return false;
  if(++c.rank<c.count) return true;
  return enter(c,c.group_rank+1);
}

Entry Natural::read(const Cursor& c) const {
  Entry e;
  e.prefix=load<std::uint32_t>(*c.group->parts[1],c.rank);
  std::memcpy(&e.dirent,probe(c.group,e.prefix),64);
  return e;
}

std::vector<Entry> Natural::dump() const {
  std::vector<Entry> out;
  auto* saved=trace;
  trace=nullptr;
  for(auto c=seek(0u);c;next(c)) out.push_back(read(c));
  trace=saved;
  return out;
}
}
