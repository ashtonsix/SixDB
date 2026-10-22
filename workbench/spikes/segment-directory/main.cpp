#include "workload.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>

using namespace segdir;

namespace {
void require(bool ok,const char* what) {
  if(!ok) {std::fprintf(stderr,"check failed: %s\n",what);std::exit(1);}
}
bool same(const Entry& a,const Entry& b,bool fenced) {
  return a.prefix==b.prefix&&std::memcmp(&a.dirent,&b.dirent,64)==0&&
         (!fenced||(a.min.b==b.min.b&&a.max.b==b.max.b));
}

template<class Layer> void compare(const Layer& l,const std::map<std::uint32_t,Entry>& o) {
  l.validate();
  auto got=l.dump();
  require(got.size()==o.size(),"dump size");
  std::size_t i=0;
  for(auto& [p,e]:o) require(same(got[i++],e,l.config().fenced()),"dump entry");
}

template<class Layer> void probe(const Layer& l,const std::map<std::uint32_t,Entry>& o,const Workload& w,Rng& rng) {
  auto fenced=l.config().fenced();
  for(int q=0;q<50;++q) {
    auto p=std::uint32_t(w.lo+rng()%(w.hi-w.lo));
    auto it=o.find(p);
    auto d=l.find(p);
    require(d.has_value()==(it!=o.end()),"find presence");
    if(d) require(std::memcmp(&*d,&it->second.dirent,64)==0,"find dirent");
    auto c=l.seek(p);
    auto lb=o.lower_bound(p);
    for(int s=0;s<5;++s,++lb) {
      require(bool(c)==(lb!=o.end()),"seek end");
      if(!c) break;
      require(same(l.read(c),lb->second,fenced),"seek entry");
      l.next(c);
    }
    if constexpr(requires {l.seek(Key{});}) if(fenced&&!o.empty()) {
      auto low=key(rng()%(value(o.rbegin()->second.max)+2));
      auto c=l.seek(low);
      auto it=std::find_if(o.begin(),o.end(),[&](auto& kv) {return value(kv.second.max)>=value(low);});
      require(bool(c)==(it!=o.end()),"logical seek end");
      if(c) require(l.read(c).prefix==it->first,"logical seek entry");
    }
  }
}

template<class Layer> void churn(Layer& l,Store& store,std::map<std::uint32_t,Entry>& o,const Workload& w,Rng& rng,
                                 std::size_t ops,unsigned check_every,std::size_t target) {
  std::vector<std::uint32_t> keys;
  for(auto& [p,e]:o) keys.push_back(p);
  std::uint64_t version=1;
  for(std::size_t i=0;i<ops;++i) {
    auto r=rng()%8;
    // Births and deaths hold the population near target; the layer may empty.
    bool grow=o.size()<target/2||(o.size()<target*2&&rng()%2);
    if(o.empty()||(r<4&&grow)) {
      auto p=absent(o,w,rng);
      auto e=born(o,p,version++);
      l.insert(e);o.emplace(p,e);keys.push_back(p);
    } else if(r<4||r==4) {
      auto p=present(o,keys,rng);
      l.erase(p);o.erase(p);
    } else if(r==5) {
      auto p=present(o,keys,rng);
      auto e=widened(o,p,version++);
      l.update(e);o[p]=e;
    } else {
      auto p=present(o,keys,rng);
      auto e=o[p];
      if(r==6) e.dirent=dirent(p,version++);
      auto before=store.written;
      l.update(e);o[p]=e;
      // An identical replacement publishes nothing.
      if(r==7) require((store.written-before).bytes==0&&(store.written-before).objects==0,"no-op edit wrote");
    }
    if(keys.size()>4*o.size()+64) {keys.clear();for(auto& [p,e]:o) keys.push_back(p);}
    if(i%check_every==0) {compare(l,o);probe(l,o,w,rng);}
  }
  compare(l,o);
  probe(l,o,w,rng);
}

// Batched edits with unique prefixes, published as one batch each. Some
// batches empty the whole layer, and large ones split leaves into many pieces.
void churn_batches(Tree& t,Store& store,std::map<std::uint32_t,Entry>& o,const Workload& w,Rng& rng,
                   std::size_t batches,std::size_t max_batch,std::size_t target) {
  std::uint64_t version=UINT64_C(1)<<32;
  for(std::size_t b=0;b<batches;++b) {
    std::map<std::uint32_t,Edit> edits;
    if(rng()%40==0) {
      for(auto& [p,e]:o) edits[p]={Edit::erase,e};
      o.clear();
    } else {
      auto n=1+rng()%max_batch;
      std::vector<std::uint32_t> keys;
      for(auto& [p,e]:o) keys.push_back(p);
      for(std::size_t i=0;i<n;++i) {
        auto r=rng()%4;
        bool grow=o.size()<target/2||(o.size()<target*2&&rng()%2);
        if(o.empty()||(r<2&&grow)) {
          auto p=absent(o,w,rng);
          if(edits.contains(p)) continue;
          auto e=born(o,p,version++);
          o.emplace(p,e);edits[p]={Edit::insert,e};
        } else {
          if(keys.empty()) continue;
          auto p=keys[rng()%keys.size()];
          if(edits.contains(p)||!o.contains(p)) continue;
          if(r<2) {o.erase(p);edits[p]={Edit::erase,{}};edits[p].entry.prefix=p;}
          else {auto e=r==2?widened(o,p,version++):o[p];e.dirent=dirent(p,version++);o[p]=e;edits[p]={Edit::update,e};}
        }
      }
    }
    std::vector<Edit> sorted;
    for(auto& [p,e]:edits) sorted.push_back(e);
    store.begin();
    t.apply(sorted);
    store.commit();
    if(b%7==0) {compare(t,o);probe(t,o,w,rng);}
  }
  compare(t,o);
  probe(t,o,w,rng);
}

int check() {
  struct Case {const char* name;Config c;};
  std::vector<Case> cases;
  for(unsigned layer:{2u,3u,4u}) for(unsigned j:{0u,4u}) {
    Config c;c.layer=layer;c.j=j;
    c.leaf_capacity=8;c.inner_capacity=4;c.tile=3;c.merge_below=0.5;
    cases.push_back({"tree",c});
    auto u=c;u.tile=0;u.fill=1;cases.push_back({"tree untiled full",u});
    auto ns=c;ns.summary=false;cases.push_back({"tree no summary",ns});
    auto wide=c;wide.leaf_capacity=40;wide.inner_capacity=36;cases.push_back({"tree wide nodes",wide});
    auto half=c;half.append_split=false;cases.push_back({"tree halves",half});
    auto f=c;f.append_split=false;f.bounded_root=false;f.exact_bounds=true;f.merge_below=1.0/3;cases.push_back({"flat",f});
    if(!c.fenced()) {auto h=c;h.hash_index=true;h.page_target=6;cases.push_back({"hash",h});}
    if(!c.fenced()) {auto h=c;h.inline_hash=true;h.page_target=5;cases.push_back({"inline hash",h});}
  }
  for(auto& [name,c]:cases) for(bool dense:{false,true}) {
    Rng rng(c.layer*101+c.j*7+dense);
    Workload w{c.layer,std::min<std::size_t>(300,(std::size_t(1)<<(8*c.layer))/4),dense};
    auto o=populate(w,rng);
    Store s;
    Tree t(s,c,1);
    t.build(std::vector<Entry>([&] {std::vector<Entry> v;for(auto& [p,e]:o) v.push_back(e);return v;}()));
    churn(t,s,o,w,rng,6000,97,w.count);
    // Drain to empty and refill, crossing root collapse and regrowth.
    churn(t,s,o,w,rng,3000,53,8);
    churn(t,s,o,w,rng,3000,53,w.count);
    churn_batches(t,s,o,w,rng,150,3*c.leaf_capacity*c.inner_capacity,w.count);
    std::printf("ok %-18s L%u J%u %s\n",name,c.layer,c.j,dense?"dense":"spread");
  }
  for(unsigned layer:{2u,3u}) for(unsigned bits:{8u,16u}) for(bool dense:{false,true}) {
    Config c;c.layer=layer;c.j=4;
    Rng rng(layer*13+bits+dense);
    Workload w{layer,300,dense};
    auto o=populate(w,rng);
    Store s;
    Natural n(s,c,bits,1);
    n.build(std::vector<Entry>([&] {std::vector<Entry> v;for(auto& [p,e]:o) v.push_back(e);return v;}()));
    churn(n,s,o,w,rng,4000,97,w.count);
    std::printf("ok natural%-2u         L%u J4 %s\n",bits,layer,dense?"dense":"spread");
  }
  return 0;
}

struct Stat {
  double sum{};std::uint64_t max{},n{};
  void add(std::uint64_t v) {sum+=double(v);max=std::max(max,v);++n;}
  double mean() const {return n?sum/double(n):0;}
};

template<class Layer> void measure(const char* name,Layer& l,Store& s,std::map<std::uint32_t,Entry> o,
                                   Workload w,Rng& rng) {
  std::vector<Entry> es;
  for(auto& [p,e]:o) es.push_back(e);
  auto before=s.written;
  l.build(es);
  auto built=s.written-before;
  std::vector<std::uint32_t> keys;
  for(auto& [p,e]:o) keys.push_back(p);
  Trace tr;
  trace=&tr;
  Stat find_lines,find_visits,seek_lines,seek_visits,lseek_lines,lseek_visits;
  double scan_lines=0,scan_visits=0,scanned=0;
  for(int q=0;q<2000;++q) {
    tr.clear();
    require(l.find(present(o,keys,rng)).has_value(),"measure find");
    find_lines.add(tr.lines.size());find_visits.add(tr.visits);
    tr.clear();
    auto c=l.seek(std::uint32_t(w.lo+rng()%(w.hi-w.lo)));
    if(c) l.read(c);
    seek_lines.add(tr.lines.size());seek_visits.add(tr.visits);
    if constexpr(requires {l.seek(Key{});}) if(l.config().fenced()) {
      tr.clear();
      auto c=l.seek(key(rng()%value(o.rbegin()->second.max)));
      if(c) l.read(c);
      lseek_lines.add(tr.lines.size());lseek_visits.add(tr.visits);
    }
  }
  for(int q=0;q<100;++q) {
    auto c=l.seek(std::uint32_t(w.lo+rng()%(w.hi-w.lo)));
    tr.clear();
    for(int i=0;i<1000&&c;++i,l.next(c)) {l.read(c);++scanned;}
    scan_lines+=double(tr.lines.size());scan_visits+=double(tr.visits);
  }
  trace=nullptr;
  auto resident=l.resident_bytes();
  // Single edits applied in sequence from the built state, so the first births
  // see bulk-build slack. Each kind starts from the state its predecessor left.
  Stat value_b,value_o,birth_b,birth_o,death_b,death_o,widen_b,widen_o;
  std::uint64_t version=1;
  // Large-partition controls copy megabytes per edit; SEGDIR_OPS bounds the sample.
  int ops=std::getenv("SEGDIR_OPS")?std::atoi(std::getenv("SEGDIR_OPS")):2000;
  auto edit=[&](Stat& b,Stat& ob,auto&& f) {auto w0=s.written;f();auto d=s.written-w0;b.add(d.bytes);ob.add(d.objects+d.erased);};
  for(int q=0;q<ops;++q) {
    auto p=present(o,keys,rng);
    auto e=o[p];e.dirent=dirent(p,version++);
    edit(value_b,value_o,[&] {l.update(e);});o[p]=e;
  }
  for(int q=0;q<ops;++q) {
    auto p=absent(o,w,rng);
    auto e=born(o,p,version++);
    edit(birth_b,birth_o,[&] {l.insert(e);});o.emplace(p,e);keys.push_back(p);
  }
  for(int q=0;q<ops;++q) {
    auto p=present(o,keys,rng);
    edit(death_b,death_o,[&] {l.erase(p);});o.erase(p);
  }
  if(l.config().fenced()) for(int q=0;q<ops;++q) {
    auto p=present(o,keys,rng);
    auto e=widened(o,p,version++);
    edit(widen_b,widen_o,[&] {l.update(e);});o[p]=e;
  }
  auto n=double(w.count);
  std::printf("%-12s L%u J%u %-6s %8zu | build %6.1f B/seg resident %6.1f B/seg %7.4f obj/seg"
              " | find %4.1f ln %3.1f obj  seek %4.1f ln %3.1f obj  lseek %4.1f ln %3.1f obj  scan %5.2f ln %5.3f obj /entry"
              " | value %8.0f B (max %8lu) %4.2f obj  birth %8.0f B (max %8lu) %4.2f obj  death %8.0f B (max %8lu) %4.2f obj  widen %8.0f B (max %8lu)\n",
              name,w.layer,l.config().j,w.dense?"dense":"spread",w.count,
              double(built.bytes)/n,double(resident)/n,double(l.resident_objects())/n,
              find_lines.mean(),find_visits.mean(),seek_lines.mean(),seek_visits.mean(),lseek_lines.mean(),lseek_visits.mean(),
              scan_lines/scanned,scan_visits/scanned,
              value_b.mean(),(unsigned long)value_b.max,value_o.mean(),birth_b.mean(),(unsigned long)birth_b.max,birth_o.mean(),
              death_b.mean(),(unsigned long)death_b.max,death_o.mean(),widen_b.mean(),(unsigned long)widen_b.max);
  std::fflush(stdout);
}

// Edit grain beyond single uniform edits: batch size, steady churn and
// append-heavy hot spots. Each batch is one publication.
void scenario(const Config& c,const char* name,Workload w,std::map<std::uint32_t,Entry> base) {
  auto fill=[&](const Tree& t) {auto [l,e]=t.occupancy();return double(e)/double(l*c.leaf_capacity);};
  auto entries=[&](const std::map<std::uint32_t,Entry>& o) {std::vector<Entry> v;for(auto& [p,e]:o) v.push_back(e);return v;};
  for(std::size_t b:{1u,16u,256u,4096u}) {
    Rng rng(b*31+w.count);
    auto o=base;
    Store s;
    Tree t(s,c,1);
    t.build(entries(o));
    auto before=s.written;
    std::size_t edits=0,batches=std::max<std::size_t>(4,16384/b);
    std::uint64_t version=1;
    for(std::size_t r=0;r<batches;++r) {
      std::map<std::uint32_t,Edit> batch;
      for(std::size_t i=0;i<b;++i) {
        auto k=rng()%3;
        if(k==0) {
          auto p=absent(o,w,rng);
          if(batch.contains(p)) continue;
          auto e=born(o,p,version++);
          o.emplace(p,e);batch[p]={Edit::insert,e};
        } else {
          auto it=o.lower_bound(std::uint32_t(w.lo+rng()%(w.hi-w.lo)));
          if(it==o.end()) it=o.begin();
          auto p=it->first;
          if(batch.contains(p)) continue;
          if(k==1) {o.erase(it);Edit e{Edit::erase,{}};e.entry.prefix=p;batch[p]=e;}
          else {it->second.dirent=dirent(p,version++);batch[p]={Edit::update,it->second};}
        }
      }
      std::vector<Edit> sorted;
      for(auto& [p,e]:batch) sorted.push_back(e);
      edits+=sorted.size();
      s.begin();t.apply(sorted);s.commit();
    }
    auto d=s.written-before;
    std::printf("%-6s L%u J%u %-6s %8zu batch %5zu: %8.0f B/edit %6.3f obj/edit  fill %.2f\n",name,c.layer,c.j,
                w.dense?"dense":"spread",w.count,b,double(d.bytes)/double(edits),double(d.objects+d.erased)/double(edits),fill(t));
  }
  {
    // Steady churn: alternate random births and deaths for N/2 operations.
    Rng rng(w.count+5);
    auto o=base;
    Store s;
    Tree t(s,c,1);
    t.build(entries(o));
    std::vector<std::uint32_t> keys;
    for(auto& [p,e]:o) keys.push_back(p);
    std::uint64_t version=1;
    Written half{};
    std::size_t ops=w.count/2;
    for(std::size_t i=0;i<ops;++i) {
      if(i==ops/2) half=s.written;
      if(i%2==0) {auto p=absent(o,w,rng);auto e=born(o,p,version++);t.insert(e);o.emplace(p,e);keys.push_back(p);}
      else {auto p=present(o,keys,rng);t.erase(p);o.erase(p);}
      if(keys.size()>4*o.size()+64) {keys.clear();for(auto& [p,e]:o) keys.push_back(p);}
    }
    auto d=s.written-half;
    std::printf("%-6s L%u J%u %-6s %8zu churn      : %8.0f B/edit %6.3f obj/edit  fill %.2f  %.1f B/seg\n",name,c.layer,c.j,
                w.dense?"dense":"spread",w.count,double(d.bytes)/double(ops-ops/2),double(d.objects+d.erased)/double(ops-ops/2),
                fill(t),double(t.resident_bytes())/double(o.size()));
  }
  // Appends past the current maximum, as with a growing time key. Spread
  // populations already reach the top of the prefix domain, so only dense ones append.
  if(w.dense) for(std::size_t b:{1u,256u}) {
    Rng rng(w.count+b);
    auto o=base;
    Store s;
    Tree t(s,c,1);
    t.build(entries(o));
    auto before=s.written;
    std::uint64_t version=1;
    std::size_t total=w.count/4;
    auto limit=(std::uint64_t(1)<<(8*c.layer))-1;
    for(std::size_t done=0;done<total;) {
      std::vector<Edit> batch;
      for(std::size_t i=0;i<b&&done<total;++i,++done) {
        auto p=std::uint32_t(std::min<std::uint64_t>(limit,std::uint64_t(o.rbegin()->first)+1+rng()%3));
        if(o.contains(p)) break;
        auto e=born(o,p,version++);
        o.emplace(p,e);batch.push_back({Edit::insert,e});
      }
      if(batch.empty()) break;
      s.begin();t.apply(batch);s.commit();
    }
    auto d=s.written-before;
    std::printf("%-6s L%u J%u %-6s %8zu append %4zu: %8.0f B/edit %6.3f obj/edit  fill %.2f  %.1f B/seg\n",name,c.layer,c.j,
                w.dense?"dense":"spread",w.count,b,double(d.bytes)/double(total),double(d.objects+d.erased)/double(total),
                fill(t),double(t.resident_bytes())/double(o.size()));
  }
  std::fflush(stdout);
}

int run(int argc,char** argv) {
  // run LAYER J COUNT dense|spread VARIANT...
  unsigned layer=unsigned(std::stoul(argv[2])),j=unsigned(std::stoul(argv[3]));
  std::size_t count=std::stoull(argv[4]);
  bool dense=std::string_view(argv[5])=="dense";
  for(int a=6;a<argc;++a) {
    std::string_view v=argv[a];
    Rng rng(layer*1000003+count*7+dense);
    Workload w{layer,count,dense};
    auto o=populate(w,rng);
    Store s;
    Config c;c.layer=layer;c.j=j;
    if(v=="flat") {
      // Old count-balanced control: full leaves, one unbounded root of 64-byte rows.
      c.fill=1;c.tile=0;c.bounded_root=false;c.exact_bounds=true;c.merge_below=1.0/3;c.summary=false;c.append_split=false;
      Tree t(s,c,1);measure("flat",t,s,o,w,rng);
    } else if(v.starts_with("natural")) {
      unsigned bits=unsigned(std::stoul(std::string(v.substr(7))));
      Natural n(s,c,bits,1);measure(std::string(v).c_str(),n,s,o,w,rng);
    } else if(v=="scenario-tree") {
      scenario(c,"tree",w,o);
    } else if(v=="scenario-flat") {
      c.fill=1;c.tile=0;c.bounded_root=false;c.exact_bounds=true;c.merge_below=1.0/3;c.summary=false;c.append_split=false;
      scenario(c,"flat",w,o);
    } else if(v=="scenario-halves") {
      c.append_split=false;
      scenario(c,"halves",w,o);
    } else if(v=="tree") {
      Tree t(s,c,1);measure("tree",t,s,o,w,rng);
    } else if(v=="tree-nosum") {
      c.summary=false;Tree t(s,c,1);measure("tree-nosum",t,s,o,w,rng);
    } else if(v=="tree-untiled") {
      c.tile=0;Tree t(s,c,1);measure("tree-untiled",t,s,o,w,rng);
    } else if(v=="tree-hash") {
      c.hash_index=true;Tree t(s,c,1);measure("tree-hash",t,s,o,w,rng);
    } else if(v.starts_with("tree-inline")) {
      // Brief-faithful: dirents inline in hash pages bounded by hash bits.
      c.inline_hash=true;c.page_target=unsigned(std::stoul(std::string(v.substr(11))));
      Tree t(s,c,1);measure(std::string(v).c_str(),t,s,o,w,rng);
    } else if(v.starts_with("tree-leaf")) {
      c.leaf_capacity=unsigned(std::stoul(std::string(v.substr(9))));
      Tree t(s,c,1);measure(std::string(v).c_str(),t,s,o,w,rng);
    } else {
      std::fprintf(stderr,"unknown variant %s\n",argv[a]);return 2;
    }
  }
  return 0;
}
}

int main(int argc,char** argv) {
  if(argc>=2&&std::string_view(argv[1])=="--check") return check();
  if(argc>=7&&std::string_view(argv[1])=="run") return run(argc,argv);
  std::fprintf(stderr,"usage: %s --check | run LAYER J COUNT dense|spread VARIANT...\n"
                      "variants: flat natural<bits> tree tree-untiled tree-hash tree-inline<page entries> tree-leaf<capacity>\n",argv[0]);
  return 2;
}
