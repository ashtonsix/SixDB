#include <engine/directory.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <random>
#include <unordered_set>

// Directory layers against an ordered oracle, through real Orbital Objects:
// random single and batched births, deaths, fence changes, value edits and
// identical replacements; draining and regrowth; small capacities so trees
// reach several levels; and readers holding an older Scope across publication.
namespace {
using namespace engine::directory;
using Rng=std::mt19937_64;
constexpr unsigned K=12;

void check(bool ok,const char* what) {
  if(ok) return;
  std::fprintf(stderr,"engine directory check: %s\n",what);
  std::abort();
}

struct Row {
  std::array<std::uint8_t,K> min{},max{};
  Dirent dirent{};
};
using Oracle=std::map<std::uint32_t,Row>;

std::array<std::uint8_t,K> key(std::uint64_t v) {
  std::array<std::uint8_t,K> k{};
  for(int i=0;i<8;++i) k[i]=std::uint8_t(v>>(56-8*i));
  return k;
}
std::uint64_t value(std::span<const std::uint8_t> k) {
  std::uint64_t v=0;
  for(int i=0;i<8;++i) v=v<<8|k[i];
  return v;
}
Dirent dirent(std::uint32_t p,std::uint64_t version) {
  Dirent d;
  for(unsigned i=0;i<8;++i) d.word[i]=(std::uint64_t(p)<<16|version)*(2*i+1);
  return d;
}
Entry entry(std::uint32_t p,const Row& r) {return {p,r.min,r.max,r.dirent};}

struct Domain {std::uint64_t lo,hi;};

// Fences between ordered neighbours, so physical order stays logical order;
// one gap in twenty is zero, so equal keys cross Segment boundaries.
Row born(const Oracle& o,std::uint32_t p,std::uint64_t version,Rng& rng) {
  auto next=o.lower_bound(p);
  std::uint64_t lo=next==o.begin()?0:value(std::prev(next)->second.max);
  std::uint64_t hi=next==o.end()?lo+(UINT64_C(1)<<31):value(next->second.min);
  if(rng()%20==0) return {key(lo),key(lo),dirent(p,version)};
  return {key(lo+(hi-lo)/3),key(lo+2*(hi-lo)/3),dirent(p,version)};
}
Row widened(const Oracle& o,std::uint32_t p,std::uint64_t version) {
  auto it=o.find(p);
  auto r=it->second;
  std::uint64_t lo=it==o.begin()?0:value(std::prev(it)->second.max);
  std::uint64_t hi=std::next(it)==o.end()?value(r.max)+(UINT64_C(1)<<31):value(std::next(it)->second.min);
  r.min=key(lo+(value(r.min)-lo)/2);
  r.max=key(value(r.max)+(hi-value(r.max))/2);
  r.dirent=dirent(p,version);
  return r;
}
std::uint32_t absent(const Oracle& o,Domain d,Rng& rng) {
  for(;;) {auto p=std::uint32_t(d.lo+rng()%(d.hi-d.lo));if(!o.contains(p)) return p;}
}

void compare(const Layer& l,const Store& store,const Oracle& o,Domain d,Rng& rng) {
  l.validate();
  Store::Scope scope(store);
  const bool fenced=l.shape().fenced();
  auto c=l.seek(scope,0u);
  for(auto& [p,r]:o) {
    check(bool(c)&&l.prefix(c)==p,"scan prefix");
    check(!std::memcmp(&l.dirent(c),&r.dirent,64),"scan dirent");
    if(fenced) check(std::ranges::equal(l.min(c),r.min)&&std::ranges::equal(l.max(c),r.max),"scan fences");
    l.next(scope,c);
  }
  check(!c,"scan end");
  for(int q=0;q<40;++q) {
    auto p=std::uint32_t(d.lo+rng()%(d.hi-d.lo));
    auto it=o.find(p);
    auto* found=l.find(scope,p);
    check(bool(found)==(it!=o.end()),"find presence");
    if(found) check(!std::memcmp(found,&it->second.dirent,64),"find dirent");
    auto s=l.seek(scope,p);
    auto lb=o.lower_bound(p);
    check(bool(s)==(lb!=o.end())&&(!s||l.prefix(s)==lb->first),"seek");
    if(fenced&&!o.empty()) {
      auto low=key(rng()%(value(o.rbegin()->second.max)+2));
      auto lc=l.seek(scope,std::span<const std::uint8_t>(low));
      auto jt=std::find_if(o.begin(),o.end(),[&](auto& kv) {return value(kv.second.max)>=value(low);});
      check(bool(lc)==(jt!=o.end())&&(!lc||l.prefix(lc)==jt->first),"logical seek");
    }
  }
}

// One batch of edits with unique prefixes; occasionally empties the layer.
std::vector<std::pair<Edit::Kind,std::pair<std::uint32_t,Row>>> batch(Oracle& o,Domain d,Rng& rng,
    std::size_t max_batch,std::size_t target,std::uint64_t& version,bool& noop) {
  std::map<std::uint32_t,std::pair<Edit::Kind,Row>> edits;
  noop=false;
  if(rng()%40==0) {
    for(auto& [p,r]:o) edits[p]={Edit::erase,r};
    o.clear();
  } else if(rng()%10==0&&!o.empty()) {
    // Identical replacements must publish nothing.
    auto it=o.lower_bound(std::uint32_t(d.lo+rng()%(d.hi-d.lo)));
    if(it==o.end()) it=o.begin();
    edits[it->first]={Edit::update,it->second};
    noop=true;
  } else {
    auto n=1+rng()%max_batch;
    for(std::size_t i=0;i<n;++i) {
      auto r=rng()%4;
      bool grow=o.size()<target/2||(o.size()<target*2&&rng()%2);
      if(o.size()>=d.hi-d.lo) grow=false;
      if(o.empty()||(r<2&&grow)) {
        auto p=absent(o,d,rng);
        if(edits.contains(p)) continue;
        auto row=born(o,p,version++,rng);
        o.emplace(p,row);edits[p]={Edit::insert,row};
      } else {
        auto it=o.lower_bound(std::uint32_t(d.lo+rng()%(d.hi-d.lo)));
        if(it==o.end()) it=o.begin();
        auto p=it->first;
        if(edits.contains(p)) continue;
        if(r<2) {edits[p]={Edit::erase,it->second};o.erase(it);}
        else {
          auto row=r==2?widened(o,p,version++):it->second;
          row.dirent=dirent(p,version++);
          it->second=row;edits[p]={Edit::update,row};
        }
      }
    }
  }
  std::vector<std::pair<Edit::Kind,std::pair<std::uint32_t,Row>>> out;
  for(auto& [p,e]:edits) out.push_back({e.first,{p,e.second}});
  return out;
}

void exercise(loom::Worker& worker,Shape shape,bool dense,std::size_t target,std::size_t singles,std::size_t batches) {
  std::pmr::unsynchronized_pool_resource memory;
  Store store(memory);
  Layer layer(worker,store,memory,0xd1,shape);
  Rng rng(shape.layer*101+shape.shared_prefix*7+dense+shape.leaf_capacity);
  auto domain_size=std::uint64_t(1)<<(8*shape.layer);
  target=std::min<std::size_t>(target,domain_size/4+1);
  Domain d{0,domain_size};
  if(dense&&shape.layer>=2) {d.lo=std::min<std::uint64_t>(domain_size-2*target,UINT64_C(0x12000000)>>(32-8*shape.layer));d.hi=d.lo+2*target;}
  if(shape.layer==0) target=1;
  Oracle o;
  std::uint64_t version=1;
  while(o.size()<target) {auto p=absent(o,d,rng);o.emplace(p,born(o,p,version++,rng));}
  std::vector<Entry> es;
  for(auto& [p,r]:o) es.push_back(entry(p,r));
  check(bool(layer.build(es)),"build");
  compare(layer,store,o,d,rng);
  // Singles, with a reader holding the Scope from before each edit.
  for(std::size_t i=0;i<singles;++i) {
    bool noop=false;
    auto edits=batch(o,d,rng,1,target,version,noop);
    std::optional<Store::Scope> old;
    old.emplace(store);
    std::optional<std::pair<std::uint32_t,Dirent>> before;
    if(!edits.empty())
      if(auto* found=layer.find(*old,edits[0].second.first)) before=std::pair{edits[0].second.first,*found};
    std::vector<Edit> es2;
    for(auto& [kind,pr]:edits) es2.push_back({kind,entry(pr.first,pr.second)});
    auto cut=Store::Scope(store).cut();
    check(bool(layer.apply(es2)),"apply");
    if(noop) check(Store::Scope(store).cut()==cut,"identical replacement published");
    if(before) {
      auto* again=layer.find(*old,before->first);
      check(again&&!std::memcmp(again,&before->second,64),"older Scope lost its version");
    }
    if(i%53==0) compare(layer,store,o,d,rng);
  }
  for(std::size_t b=0;b<batches;++b) {
    bool noop=false;
    auto max_batch=std::size_t(3)*shape.leaf_capacity*std::max(1u,shape.inner_capacity/8);
    auto edits=batch(o,d,rng,max_batch,target,version,noop);
    std::vector<Edit> es2;
    for(auto& [kind,pr]:edits) es2.push_back({kind,entry(pr.first,pr.second)});
    // Keep the rows alive while edits borrow their fences.
    check(bool(layer.apply(es2)),"apply batch");
    if(b%7==0) compare(layer,store,o,d,rng);
  }
  compare(layer,store,o,d,rng);
}

void run(loom::Worker& worker) {
  unsigned cases=0;
  for(unsigned l=0;l<=4;++l) for(unsigned j:{0u,4u}) for(bool dense:{false,true}) {
    Shape s{.layer=l,.key_bytes=K,.shared_prefix=j,.leaf_capacity=8,.inner_capacity=4,.fill=0.75,.merge_below=0.5,.tile=3};
    exercise(worker,s,dense,300,3000,150);
    auto wide=s;wide.leaf_capacity=40;wide.inner_capacity=36;
    exercise(worker,wide,dense,600,1500,80);
    cases+=2;
  }
  std::printf("PASS: %u directory layer cases against an ordered oracle, with late readers and no-op publication\n",cases);
}
}

int main() {
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"worker adoption");
  auto arena=orbital::allocation::open_arena(std::size_t(1)<<30);
  check(arena&&bool(loom::configure(worker,std::move(*arena),0)),"worker configuration");
  run(worker);
  for(;;) {
    auto p=loom::shutdown(worker);
    check(p.kind()!=orbital::Progress::Kind::failed,"shutdown");
    if(p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
}
