#include <engine/directory.h>
#include <ikea/boundlist.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Sections, top to bottom: the directory's Object format and row edits; the
// publication workspace; tree mechanics (row planes, descent, settling, apply
// and build), which are generic B+ tree apart from widen_separators; reads;
// validation.
namespace engine::directory {
namespace {
void require(bool ok,const char* what) {
  if(ok) return;
  std::fprintf(stderr,"engine::directory: %s\n",what);
  std::abort();
}
std::size_t rounded(std::size_t n) {return (n+63)/64*64;}
const std::byte* address(Selection s,std::size_t i) {return s.part(i)->address();}
template<class T> const T* plane(Selection s,std::size_t i) {return reinterpret_cast<const T*>(address(s,i));}
std::uint32_t rows(Selection s) {return plane<std::uint32_t>(s,0)[0];}
std::uint32_t height(Selection s) {return plane<std::uint32_t>(s,0)[1];}
template<class T> std::vector<std::byte> raw(const std::vector<T>& v) {
  std::vector<std::byte> out(v.size()*sizeof(T));
  if(!v.empty()) std::memcpy(out.data(),v.data(),out.size());
  return out;
}
template<class T> std::vector<T> typed(const std::byte* p,std::size_t n) {
  std::vector<T> out(n);
  if(n) std::memcpy(out.data(),p,n*sizeof(T));
  return out;
}
// K-byte logical keys as a Boundlist endpoint domain.
struct KeyRef {
  const std::uint8_t* bytes;
  unsigned width;
  friend bool operator<(KeyRef a,KeyRef b) {return std::memcmp(a.bytes,b.bytes,a.width)<0;}
};
struct KeySource {
  const std::uint8_t* data;
  unsigned count,width;
  unsigned size() const {return count;}
  KeyRef at(unsigned r) const {return {data+std::size_t(r)*width,width};}
};
namespace bl=ikea::boundlist;
// Visits each row plane of two nodes pairwise with its width in elements per row.
template<class To,class From,class F> void planes(To& to,const From& from,std::size_t k,F f) {
  f(to.prefix,from.prefix,1);f(to.min,from.min,k);f(to.max,from.max,k);f(to.dirent,from.dirent,1);
  f(to.sep,from.sep,1);f(to.lsep,from.lsep,k);f(to.child,from.child,1);
}
}

// Directory format and rows.

Layer::Layer(loom::Worker& w,Store& s,std::pmr::memory_resource& m,std::uint64_t ns,Shape shape)
  :worker_(&w),store_(&s),memory_(&m),space_(ns),shape_(shape) {
  require(shape.layer<=4&&shape.key_bytes&&shape.key_bytes<=255&&shape.tile&&shape.tile<=255,"shape");
  require(shape.leaf_capacity>=4&&shape.inner_capacity>=4,"capacity");
}

unsigned Layer::leaf_capacity() const {return shape_.layer<=1?256:shape_.leaf_capacity;}

Control Layer::control(std::uint64_t name,const Node& n) const {
  Control c;
  c.memory=memory_;
  c.name=name;
  c.count=std::uint32_t(n.size());
  c.height=std::uint16_t(n.height);
  c.key_bytes=std::uint8_t(shape_.key_bytes);
  c.fenced=shape_.fenced();
  c.tile=std::uint8_t(shape_.tile);
  c.parts=std::uint16_t(c.full_part_count());
  return c;
}

std::vector<std::vector<std::byte>> Layer::encode(const Node& n) const {
  std::vector<std::vector<std::byte>> parts;
  const auto& routing=n.height?n.sep:n.prefix;
  parts.push_back(raw(std::vector<std::uint32_t>{std::uint32_t(n.size()),n.height}));
  parts.push_back(raw(routing));
  if(n.height) {
    if(shape_.fenced()) parts.push_back(raw(n.lsep));
    parts.push_back(raw(n.child));
  } else {
    if(shape_.fenced()) {parts.push_back(raw(n.min));parts.push_back(raw(n.max));}
    for(std::size_t t=0;t<n.dirent.size();t+=shape_.tile)
      parts.push_back(raw(std::vector<Dirent>(n.dirent.begin()+t,n.dirent.begin()+std::min(n.dirent.size(),t+shape_.tile))));
  }
  return parts;
}

Layer::Node Layer::decode(Selection s) const {
  Node n;
  if(!s) return n;
  auto c=s.control();
  const std::size_t rows=c.count,k=shape_.key_bytes;
  n.height=c.height;
  auto keys=[&](std::size_t i) {
    auto* p=reinterpret_cast<const std::uint8_t*>(address(s,i));
    return std::vector<std::uint8_t>(p,p+rows*k);
  };
  if(n.height) {
    n.sep=typed<std::uint32_t>(address(s,1),rows);
    if(shape_.fenced()) n.lsep=keys(2);
    n.child=typed<std::uint64_t>(address(s,shape_.fenced()?3:2),rows);
  } else {
    n.prefix=typed<std::uint32_t>(address(s,1),rows);
    if(shape_.fenced()) {n.min=keys(2);n.max=keys(3);}
    std::size_t first=shape_.fenced()?4:2;
    for(std::size_t t=0;t*shape_.tile<rows;++t) {
      auto tile=typed<Dirent>(address(s,first+t),std::min<std::size_t>(shape_.tile,rows-t*shape_.tile));
      n.dirent.insert(n.dirent.end(),tile.begin(),tile.end());
    }
  }
  return n;
}

void Layer::edit_leaf(Step& s,const Edit& ed) const {
  const auto& e=ed.entry;
  const auto k=shape_.key_bytes;
  auto& leaf=s.node;
  auto r=std::lower_bound(leaf.prefix.begin(),leaf.prefix.end(),e.prefix)-leaf.prefix.begin();
  bool present=r<std::ptrdiff_t(leaf.prefix.size())&&leaf.prefix[r]==e.prefix;
  if(shape_.fenced()&&ed.kind!=Edit::erase) require(e.min.size()==k&&e.max.size()==k,"fence width");
  switch(ed.kind) {
  case Edit::insert:
    require(!present,"insert: present");
    leaf.prefix.insert(leaf.prefix.begin()+r,e.prefix);
    if(shape_.fenced()) {
      leaf.min.insert(leaf.min.begin()+r*k,e.min.begin(),e.min.end());
      leaf.max.insert(leaf.max.begin()+r*k,e.max.begin(),e.max.end());
    }
    leaf.dirent.insert(leaf.dirent.begin()+r,e.dirent);
    break;
  case Edit::erase:
    require(present,"erase: absent");
    leaf.prefix.erase(leaf.prefix.begin()+r);
    if(shape_.fenced()) {
      leaf.min.erase(leaf.min.begin()+r*k,leaf.min.begin()+(r+1)*k);
      leaf.max.erase(leaf.max.begin()+r*k,leaf.max.begin()+(r+1)*k);
    }
    leaf.dirent.erase(leaf.dirent.begin()+r);
    break;
  case Edit::update:
    require(present,"update: absent");
    if(shape_.fenced()) {
      std::copy_n(e.min.begin(),k,leaf.min.begin()+r*k);
      std::copy_n(e.max.begin(),k,leaf.max.begin()+r*k);
    }
    leaf.dirent[r]=e.dirent;
    break;
  }
}

// Logical separators stay loose. Physical order is logical order, so only an
// edit beside a boundary can violate one, and deaths never do; the leaf's new
// first and last fences pull the two governing separators outward.
void Layer::widen_separators(std::vector<Step>& path) const {
  auto& leaf=path.back().node;
  if(!leaf.size()||path.size()<2||!shape_.fenced()) return;
  const auto k=shape_.key_bytes;
  auto key_less=[&](const std::uint8_t* a,const std::uint8_t* b) {return std::memcmp(a,b,k)<0;};
  for(auto i=path.size()-1;i-->0;) if(path[i].index>0) {
    auto& s=path[i];
    auto* sep=s.node.lsep.data()+std::size_t(s.index)*k;
    if(key_less(leaf.min.data(),sep)) {std::copy_n(leaf.min.begin(),k,sep);s.dirty=true;}
    break;
  }
  for(auto i=path.size()-1;i-->0;) if(path[i].index+1<path[i].node.size()) {
    auto& s=path[i];
    auto* sep=s.node.lsep.data()+std::size_t(s.index+1)*k;
    auto* last=leaf.max.data()+leaf.max.size()-k;
    if(key_less(sep,last)) {std::copy_n(last,k,sep);s.dirty=true;}
    break;
  }
}

// Publication workspace.

Layer::Node Layer::load(Work& w,std::uint64_t name) const {
  if(auto it=w.nodes.find(name);it!=w.nodes.end()) return it->second;
  if(w.erased.contains(name)) return {};
  return decode(bind(*w.basis,name));
}
void Layer::write(Work& w,std::uint64_t name,Node n) const {
  w.nodes[name]=std::move(n);
  w.dirty.insert(name);
  w.erased.erase(name);
}
void Layer::erase(Work& w,std::uint64_t name) const {
  w.nodes.erase(name);
  w.dirty.erase(name);
  w.erased.insert(name);
}

// Captures the final version of each touched node, reusing every Part whose
// bytes are unchanged, and installs them in one packet.
orbital::Result<void> Layer::publish(Work& w) {
  std::vector<Store::Change> changes;
  for(auto name:w.dirty) {
    auto& node=w.nodes.at(name);
    auto ctl=control(name,node);
    auto parts=encode(node);
    require(parts.size()==ctl.full_part_count(),"part count");
    auto basis=bind(*w.basis,name);
    std::vector<orbital::allocation::Allocation> fresh;
    fresh.reserve(parts.size());
    std::vector<orbital::PartSource> sources;
    bool unchanged=basis&&basis.part_count()==parts.size();
    for(std::size_t i=0;i<parts.size();++i) {
      require(parts[i].size()==ctl.full_extent(i),"part extent");
      if(basis&&i<basis.part_count()&&*basis.control().extent(i)==parts[i].size()&&
         !std::memcmp(address(basis,i),parts[i].data(),parts[i].size())) {
        auto kept=orbital::source(basis,i);
        if(!kept) return std::unexpected(kept.error());
        kept->id=ctl.id(i);
        sources.push_back(std::move(*kept));
        continue;
      }
      unchanged=false;
      auto a=loom::allocate(*worker_,{.bytes=std::max<std::size_t>(64,rounded(parts[i].size())),.alignment=64});
      if(!a) return std::unexpected(a.error());
      fresh.push_back(std::move(*a));
      auto* p=fresh.back().address;
      std::memcpy(p,parts[i].data(),parts[i].size());
      std::memset(p+parts[i].size(),0,rounded(parts[i].size())-parts[i].size());
      sources.push_back({ctl.id(i),{p,parts[i].size()},{},&fresh.back()});
    }
    if(unchanged) continue;
    auto owner=store_->capture(ctl,sources);
    if(!owner) return std::unexpected(owner.error());
    changes.push_back({{space_,name},std::move(*owner)});
  }
  for(auto name:w.erased) if(bind(*w.basis,name)) changes.push_back({{space_,name},Store::Owner{}});
  if(changes.empty()) return {};
  Store::Prepared packet(*store_);
  if(auto r=store_->prepare(*w.basis,changes,packet);!r) return r;
  return store_->install(packet);
}

// Tree mechanics.

void Layer::set_row(Node& parent,unsigned i,const Node& child) const {
  const auto k=shape_.key_bytes;
  const auto& first_sep=child.height?child.sep:child.prefix;
  parent.sep[i]=first_sep[0];
  if(shape_.fenced()) {
    const auto& keys=child.height?child.lsep:child.min;
    std::copy_n(keys.begin(),k,parent.lsep.begin()+std::size_t(i)*k);
  }
}

void Layer::insert_rows(Node& parent,unsigned at,const std::vector<Node>& children,
                        const std::vector<std::uint64_t>& names) const {
  auto n=children.size();
  parent.sep.insert(parent.sep.begin()+at,n,0);
  if(shape_.fenced()) parent.lsep.insert(parent.lsep.begin()+std::size_t(at)*shape_.key_bytes,n*shape_.key_bytes,0);
  parent.child.insert(parent.child.begin()+at,names.begin(),names.end());
  for(std::size_t i=0;i<n;++i) set_row(parent,unsigned(at+i),children[i]);
}

void Layer::remove_row(Node& parent,unsigned i) const {
  const auto k=shape_.key_bytes;
  parent.sep.erase(parent.sep.begin()+i);
  if(shape_.fenced()) parent.lsep.erase(parent.lsep.begin()+std::size_t(i)*k,parent.lsep.begin()+std::size_t(i+1)*k);
  parent.child.erase(parent.child.begin()+i);
}

std::vector<Layer::Node> Layer::split_at(const Node& n,const std::vector<std::size_t>& bounds) const {
  const auto k=shape_.key_bytes;
  std::vector<Node> out(bounds.size()-1);
  for(std::size_t p=0;p+1<bounds.size();++p) {
    auto b=bounds[p],e=bounds[p+1];
    auto& o=out[p];
    o.height=n.height;
    planes(o,n,k,[&](auto& to,const auto& from,std::size_t width) {
      if(!from.empty()) to.assign(from.begin()+b*width,from.begin()+e*width);
    });
  }
  return out;
}

std::vector<Layer::Node> Layer::split(const Node& n,unsigned pieces) const {
  std::vector<std::size_t> bounds;
  for(unsigned p=0;p<=pieces;++p) bounds.push_back(n.size()*p/pieces);
  return split_at(n,bounds);
}

Layer::Node Layer::concat(Node left,const Node& right,const Node& parent,unsigned right_index) const {
  const auto k=shape_.key_bytes;
  auto at=left.sep.size();
  planes(left,right,k,[](auto& to,const auto& from,std::size_t) {to.insert(to.end(),from.begin(),from.end());});
  // The right node's first separator is unused until it becomes an interior row.
  if(left.height) {
    left.sep[at]=parent.sep[right_index];
    if(shape_.fenced()) std::copy_n(parent.lsep.begin()+std::size_t(right_index)*k,k,left.lsep.begin()+at*k);
  }
  return left;
}

std::vector<Layer::Step> Layer::descend(Work& w,std::uint32_t p) const {
  std::vector<Step> path;
  auto name=root();
  auto node=load(w,name);
  while(node.height) {
    auto j=std::upper_bound(node.sep.begin()+1,node.sep.end(),p)-node.sep.begin();
    auto child=node.child[j-1];
    auto size=node.size();
    path.push_back({name,std::move(node),unsigned(j-1),false,size});
    name=child;
    node=load(w,name);
  }
  auto size=node.size();
  path.push_back({name,std::move(node),0,true,size});
  return path;
}

// Restores node bounds bottom-up along one edited path: splits overfull nodes
// (the root keeps its name and deepens), removes empty ones, merges or
// redistributes nodes that shrank below merge_below, and collapses a root
// with one child.
void Layer::settle(Work& w,std::vector<Step>& path) {
  // The rightmost node on each level takes appends; it splits off full
  // build-occupancy pieces instead of halves so appended runs stay dense.
  std::vector<bool> rightmost(path.size(),true);
  for(std::size_t i=1;i<path.size();++i)
    rightmost[i]=rightmost[i-1]&&path[i-1].index+1==path[i-1].node.size();
  for(auto i=path.size();i-->0;) {
    auto& s=path[i];
    auto* parent=i?&path[i-1]:nullptr;
    unsigned cap=s.node.height?shape_.inner_capacity:leaf_capacity();
    if(s.node.size()>cap) {
      std::vector<Node> pieces;
      if(rightmost[i]) {
        auto per=std::max<std::size_t>(1,std::size_t(cap*shape_.fill));
        std::vector<std::size_t> bounds{0};
        while(s.node.size()-bounds.back()>cap) bounds.push_back(bounds.back()+per);
        bounds.push_back(s.node.size());
        pieces=split_at(s.node,bounds);
      } else {
        pieces=split(s.node,unsigned(s.node.size()/cap+1));
      }
      if(!parent) {
        // The root keeps its name; its contents move down a level.
        std::vector<std::uint64_t> names;
        for(auto& p:pieces) {auto name=fresh();write(w,name,p);names.push_back(name);}
        Node r;
        r.height=s.node.height+1;
        insert_rows(r,0,pieces,names);
        while(r.size()>shape_.inner_capacity) {
          auto up=split(r,unsigned(r.size()/shape_.inner_capacity+1));
          names.clear();
          for(auto& p:up) {auto name=fresh();write(w,name,p);names.push_back(name);}
          Node top;
          top.height=r.height+1;
          insert_rows(top,0,up,names);
          r=std::move(top);
        }
        write(w,root(),std::move(r));
      } else {
        write(w,s.name,pieces[0]);
        std::vector<Node> rest(pieces.begin()+1,pieces.end());
        std::vector<std::uint64_t> names;
        for(auto& p:rest) {auto name=fresh();write(w,name,p);names.push_back(name);}
        insert_rows(parent->node,parent->index+1,rest,names);
        parent->dirty=true;
      }
    } else if(!s.node.size()) {
      erase(w,s.name);
      if(parent) {remove_row(parent->node,parent->index);parent->dirty=true;}
    } else if(parent&&s.node.size()<s.before&&s.node.size()<cap*shape_.merge_below&&parent->node.size()>1) {
      // Only a node that shrank merges, so edits that keep membership never restructure.
      auto& pn=parent->node;
      unsigned li,ri;
      std::uint64_t ln,rn;
      Node left,right;
      if(parent->index+1<pn.size()) {
        li=parent->index;ri=li+1;ln=s.name;rn=pn.child[ri];
        left=std::move(s.node);right=load(w,rn);
      } else {
        ri=parent->index;li=ri-1;rn=s.name;ln=pn.child[li];
        right=std::move(s.node);left=load(w,ln);
      }
      auto both=concat(std::move(left),right,pn,ri);
      if(both.size()<=cap) {
        write(w,ln,std::move(both));
        erase(w,rn);
        remove_row(pn,ri);
      } else {
        auto halves=split(both,2);
        write(w,ln,halves[0]);
        write(w,rn,halves[1]);
        set_row(pn,ri,halves[1]);
      }
      parent->dirty=true;
    } else if(!parent&&s.node.height&&s.node.size()==1) {
      auto child=s.node.child[0];
      write(w,root(),load(w,child));
      erase(w,child);
    } else if(s.dirty) {
      write(w,s.name,std::move(s.node));
    }
  }
}

orbital::Result<void> Layer::apply(std::span<const Edit> edits) {
  Store::Scope basis(*store_);
  Work w{&basis,{},{},{}};
  for(std::size_t i=0;i<edits.size();) {
    auto path=descend(w,edits[i].entry.prefix);
    // Edits below the leaf's right separator belong to it.
    std::uint64_t upper=std::uint64_t(1)<<32;
    for(auto j=path.size()-1;j-->0;)
      if(path[j].index+1<path[j].node.size()) {upper=path[j].node.sep[path[j].index+1];break;}
    for(;i<edits.size()&&edits[i].entry.prefix<upper;++i) {
      require(i==0||edits[i-1].entry.prefix<edits[i].entry.prefix,"apply: unsorted");
      edit_leaf(path.back(),edits[i]);
    }
    widen_separators(path);
    settle(w,path);
  }
  return publish(w);
}

orbital::Result<void> Layer::build(std::span<const Entry> es) {
  Store::Scope basis(*store_);
  require(!bind(basis,root()),"build: layer not empty");
  Work w{&basis,{},{},{}};
  const auto k=shape_.key_bytes;
  auto per=std::max<std::size_t>(1,std::size_t(leaf_capacity()*shape_.fill));
  if(shape_.layer<=1) per=leaf_capacity();
  std::vector<Node> level;
  for(std::size_t i=0;i<es.size();i+=per) {
    Node n;
    for(auto j=i;j<std::min(es.size(),i+per);++j) {
      require(!j||es[j-1].prefix<es[j].prefix,"build: unsorted");
      n.prefix.push_back(es[j].prefix);
      if(shape_.fenced()) {
        require(es[j].min.size()==k&&es[j].max.size()==k,"fence width");
        n.min.insert(n.min.end(),es[j].min.begin(),es[j].min.end());
        n.max.insert(n.max.end(),es[j].max.begin(),es[j].max.end());
      }
      n.dirent.push_back(es[j].dirent);
    }
    level.push_back(std::move(n));
  }
  if(level.size()==1) write(w,root(),std::move(level[0]));
  if(level.size()>1) {
    std::vector<std::uint64_t> names;
    for(auto& n:level) {auto name=fresh();write(w,name,n);names.push_back(name);}
    auto fan=std::max<std::size_t>(2,std::size_t(shape_.inner_capacity*shape_.fill));
    for(unsigned h=1;;++h) {
      std::vector<Node> up;
      for(std::size_t i=0;i<level.size();i+=fan) {
        auto e=std::min(level.size(),i+fan);
        Node p;
        p.height=h;
        insert_rows(p,0,std::vector<Node>(level.begin()+i,level.begin()+e),
                    std::vector<std::uint64_t>(names.begin()+i,names.begin()+e));
        up.push_back(std::move(p));
      }
      if(up.size()==1) {write(w,root(),std::move(up[0]));break;}
      names.clear();
      for(auto& p:up) {auto name=fresh();write(w,name,p);names.push_back(name);}
      level=std::move(up);
    }
  }
  return publish(w);
}

// Reads.

// Reads: every visit is one Scope::bind; ordered planes are Boundlist sources.
// Row 0 of an interior node covers everything below row 1's separator.
unsigned Layer::route(Selection o,std::uint32_t p) const {
  return bl::upper_bound(bl::direct_source<std::uint32_t>{plane<std::uint32_t>(o,1)+1,rows(o)-1},p);
}
unsigned Layer::lower(Selection o,std::uint32_t p) const {
  return bl::lower_bound(bl::direct_source<std::uint32_t>{plane<std::uint32_t>(o,1),rows(o)},p);
}
unsigned Layer::route(Selection o,std::span<const std::uint8_t> low) const {
  const auto k=shape_.key_bytes;
  return bl::lower_bound(KeySource{plane<std::uint8_t>(o,2)+k,rows(o)-1,k},KeyRef{low.data(),k});
}
unsigned Layer::lower(Selection o,std::span<const std::uint8_t> low) const {
  const auto k=shape_.key_bytes;
  return bl::lower_bound(KeySource{plane<std::uint8_t>(o,3),rows(o),k},KeyRef{low.data(),k});
}

const Dirent* Layer::find(const Store::Scope& scope,std::uint32_t p) const {
  auto o=bind(scope,root());
  if(!o) return nullptr;
  const auto child=shape_.fenced()?3:2;
  while(height(o)) o=bind(scope,plane<std::uint64_t>(o,child)[route(o,p)]);
  auto r=lower(o,p);
  if(r==rows(o)||plane<std::uint32_t>(o,1)[r]!=p) return nullptr;
  return plane<Dirent>(o,(shape_.fenced()?4:2)+r/shape_.tile)+r%shape_.tile;
}

Layer::Cursor& Layer::settle_cursor(const Store::Scope& scope,Cursor& c,Selection leaf,unsigned r) const {
  c.leaf=leaf;c.rank=r;c.count=rows(leaf);
  if(r==c.count) {c.rank=c.count-1;next(scope,c);}
  return c;
}

Layer::Cursor Layer::seek(const Store::Scope& scope,std::uint32_t p) const {
  Cursor c;
  auto o=bind(scope,root());
  if(!o) return c;
  const auto child=shape_.fenced()?3:2;
  while(height(o)) {
    auto i=route(o,p);
    c.stack[c.depth]=o;c.index[c.depth++]=i;
    o=bind(scope,plane<std::uint64_t>(o,child)[i]);
  }
  return settle_cursor(scope,c,o,lower(o,p));
}

Layer::Cursor Layer::seek(const Store::Scope& scope,std::span<const std::uint8_t> low) const {
  require(shape_.fenced()&&low.size()==shape_.key_bytes,"logical seek above J");
  Cursor c;
  auto o=bind(scope,root());
  if(!o) return c;
  while(height(o)) {
    auto i=route(o,low);
    c.stack[c.depth]=o;c.index[c.depth++]=i;
    o=bind(scope,plane<std::uint64_t>(o,3)[i]);
  }
  return settle_cursor(scope,c,o,lower(o,low));
}

bool Layer::next(const Store::Scope& scope,Cursor& c) const {
  if(!c.leaf) return false;
  if(++c.rank<c.count) return true;
  const auto child=shape_.fenced()?3:2;
  while(c.depth) {
    auto o=c.stack[c.depth-1];
    auto& i=c.index[c.depth-1];
    if(++i<rows(o)) {
      auto ch=bind(scope,plane<std::uint64_t>(o,child)[i]);
      while(height(ch)) {c.stack[c.depth]=ch;c.index[c.depth++]=0;ch=bind(scope,plane<std::uint64_t>(ch,child)[0]);}
      c.leaf=ch;c.rank=0;c.count=rows(ch);
      return true;
    }
    --c.depth;
  }
  c.leaf={};
  return false;
}

std::uint32_t Layer::prefix(const Cursor& c) const {return plane<std::uint32_t>(c.leaf,1)[c.rank];}
const Dirent& Layer::dirent(const Cursor& c) const {
  return plane<Dirent>(c.leaf,(shape_.fenced()?4:2)+c.rank/shape_.tile)[c.rank%shape_.tile];
}
std::span<const std::uint8_t> Layer::min(const Cursor& c) const {
  return {plane<std::uint8_t>(c.leaf,2)+std::size_t(c.rank)*shape_.key_bytes,shape_.key_bytes};
}
std::span<const std::uint8_t> Layer::max(const Cursor& c) const {
  return {plane<std::uint8_t>(c.leaf,3)+std::size_t(c.rank)*shape_.key_bytes,shape_.key_bytes};
}

// Validation.

void Layer::validate() const {
  Store::Scope scope(*store_);
  const auto k=shape_.key_bytes;
  struct Bounds {std::uint32_t first,last;std::vector<std::uint8_t> min,max;unsigned height;};
  std::unordered_set<std::uint64_t> reachable;
  auto walk=[&](auto& self,std::uint64_t name)->Bounds {
    reachable.insert(name);
    auto s=bind(scope,name);
    require(bool(s),"validate: missing child");
    auto n=decode(s);
    require(n.size()>0,"validate: empty node");
    require(n.size()<=(n.height?shape_.inner_capacity:leaf_capacity()),"validate: overfull");
    require(encode(n)==[&] {
      std::vector<std::vector<std::byte>> parts;
      for(std::size_t i=0;i<s.part_count();++i) parts.emplace_back(address(s,i),address(s,i)+*s.control().extent(i));
      return parts;
    }(),"validate: Part layout");
    if(!n.height) {
      for(std::size_t i=1;i<n.size();++i) require(n.prefix[i-1]<n.prefix[i],"validate: leaf order");
      Bounds b{n.prefix[0],n.prefix.back(),{},{},0};
      if(shape_.fenced()) {b.min.assign(n.min.begin(),n.min.begin()+k);b.max.assign(n.max.end()-k,n.max.end());}
      return b;
    }
    std::vector<Bounds> kids;
    for(auto ch:n.child) kids.push_back(self(self,ch));
    for(std::size_t i=0;i<kids.size();++i) {
      require(kids[i].height+1==n.height,"validate: height");
      if(!i) continue;
      require(kids[i-1].last<n.sep[i]&&n.sep[i]<=kids[i].first,"validate: physical separator");
      if(shape_.fenced()) {
        auto* sep=n.lsep.data()+i*k;
        require(std::memcmp(kids[i-1].max.data(),sep,k)<=0&&std::memcmp(sep,kids[i].min.data(),k)<=0,
                "validate: logical separator");
      }
    }
    return {kids[0].first,kids.back().last,kids[0].min,kids.back().max,n.height};
  };
  if(bind(scope,root())) walk(walk,root());
  // Every published name must be reachable: no orphaned Objects.
  for(std::uint64_t n=0;n<next_;++n)
    require(!bind(scope,root()|n)||reachable.contains(root()|n),"validate: orphaned Object");
}
}
