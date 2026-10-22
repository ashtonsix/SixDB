#include "image.h"
#include "workload.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <variant>

// Read timings over flat Images of each variant's published layout. Every
// variant pays the same visit mechanism (one name-table probe per Object), so
// differences come from Object count and lines per operation. Repetitions
// rotate which variant runs first. Edits are not timed here: the model's
// writer decodes and re-encodes vectors, which is not a production writer.
using namespace segdir;

namespace {
using Clock=std::chrono::steady_clock;
volatile std::uint64_t sink;

struct Variant {
  std::string name;
  std::unique_ptr<Store> store;
  std::unique_ptr<Tree> tree;
  std::unique_ptr<Natural> natural;
  std::unique_ptr<Image> image;
  std::variant<std::monostate,TreeView,NaturalView> view;
};

void require(bool ok,const char* what) {
  if(!ok) {std::fprintf(stderr,"bench check failed: %s\n",what);std::exit(1);}
}

// The views must return the model's answers before anything is timed.
template<class V,class L> void verify(const V& v,const L& l,const std::vector<Entry>& es,
                                      const std::vector<std::uint32_t>& probes,bool fenced) {
  for(std::size_t i=0;i<es.size();i+=std::max<std::size_t>(1,es.size()/4096)) {
    auto* d=v.find(es[i].prefix);
    require(d&&std::memcmp(d,&es[i].dirent,64)==0,"find");
  }
  for(std::size_t q=0;q<std::min<std::size_t>(probes.size(),2000);++q) {
    auto p=probes[q];
    auto it=std::lower_bound(es.begin(),es.end(),p,[](const Entry& e,std::uint32_t x) {return e.prefix<x;});
    require(bool(v.find(p))==(it!=es.end()&&it->prefix==p),"find presence");
    auto c=v.seek(p);
    for(int s=0;s<3;++s,++it) {
      require(bool(c)==(it!=es.end()),"seek end");
      if(!c) break;
      require(v.prefix(c)==it->prefix&&std::memcmp(v.read(c),&it->dirent,64)==0,"seek entry");
      v.next(c);
    }
    if constexpr(requires {v.seek(Key{});}) if(fenced) {
      auto low=key(value(es[q%es.size()].min)+1);
      auto c=v.seek(low);
      auto jt=std::find_if(es.begin(),es.end(),[&](const Entry& e) {return value(e.max)>=value(low);});
      require(bool(c)==(jt!=es.end())&&(!c||v.prefix(c)==jt->prefix),"logical seek");
    }
  }
  std::size_t n=0;
  for(auto c=v.seek(0u);c;v.next(c),++n) require(v.prefix(c)==es[n].prefix,"scan order");
  require(n==es.size(),"scan count");
  (void)l;
}

template<class F> double timed(std::size_t ops,F&& f) {
  auto t0=Clock::now();
  f();
  return std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/double(ops);
}

template<class V> std::array<double,5> pass(const V& v,const std::vector<std::uint32_t>& present,
                                           const std::vector<std::uint32_t>& probes,
                                           const std::vector<Key>& lows,std::size_t scans) {
  std::array<double,5> ns{};
  auto q=present.size();
  // Dependent: the next key's index depends on the previous dirent.
  ns[0]=timed(q,[&] {
    std::uint64_t carry=0;
    for(std::size_t i=0;i<q;++i) carry=v.find(present[(i+(carry&1))%q])->w[1];
    sink=carry;
  });
  ns[1]=timed(q,[&] {
    std::uint64_t sum=0;
    for(std::size_t i=0;i<q;++i) sum+=v.find(present[i])->w[1];
    sink=sum;
  });
  ns[2]=timed(probes.size(),[&] {
    std::uint64_t sum=0;
    for(auto p:probes) if(auto c=v.seek(p)) sum+=v.read(c)->w[1];
    sink=sum;
  });
  if constexpr(requires {v.seek(Key{});}) if(!lows.empty()) ns[3]=timed(lows.size(),[&] {
    std::uint64_t sum=0;
    for(auto& k:lows) if(auto c=v.seek(k)) sum+=v.read(c)->w[1];
    sink=sum;
  });
  std::size_t entries=0;
  std::uint64_t sum=0;
  auto t0=Clock::now();
  for(std::size_t s=0;s<scans;++s) {
    auto c=v.seek(probes[s%probes.size()]);
    for(int i=0;i<1000&&c;++i,v.next(c)) {sum+=v.read(c)->w[1]+v.prefix(c);++entries;}
  }
  sink=sum;
  ns[4]=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/double(entries);
  return ns;
}
}

int main(int argc,char** argv) {
  if(argc<8) {
    std::fprintf(stderr,"usage: %s LAYER J COUNT dense|spread QUERIES REPS VARIANT...\n"
                        "variants: flat tree tree-nosum tree-hash tree-inline<page entries> natural<bits>\n",argv[0]);
    return 2;
  }
  unsigned layer=unsigned(std::stoul(argv[1])),j=unsigned(std::stoul(argv[2]));
  std::size_t count=std::stoull(argv[3]),queries=std::stoull(argv[5]);
  bool dense=std::string_view(argv[4])=="dense";
  int reps=std::stoi(argv[6]);
  Rng rng(layer*1000003+count*7+dense);
  Workload w{layer,count,dense};
  auto o=populate(w,rng);
  std::vector<Entry> es;
  for(auto& [p,e]:o) es.push_back(e);
  bool fenced=layer>j;
  std::vector<std::uint32_t> present(queries),probes(queries);
  std::vector<Key> lows;
  for(auto& p:present) p=es[rng()%es.size()].prefix;
  for(auto& p:probes) p=std::uint32_t(w.lo+rng()%(w.hi-w.lo));
  if(fenced) for(std::size_t i=0;i<queries;++i) lows.push_back(key(rng()%value(es.back().max)));
  std::vector<Variant> vs;
  for(int a=7;a<argc;++a) {
    std::string_view v=argv[a];
    Config c;c.layer=layer;c.j=j;
    Variant x{std::string(v),std::make_unique<Store>(),{},{},{},{}};
    if(v.starts_with("natural")) {
      x.natural=std::make_unique<Natural>(*x.store,c,unsigned(std::stoul(std::string(v.substr(7)))),1);
      x.natural->build(es);
      x.image=std::make_unique<Image>(*x.store);
      x.view.emplace<NaturalView>(*x.image,*x.natural);
      verify(std::get<NaturalView>(x.view),*x.natural,es,probes,false);
    } else {
      if(v=="tree") {}
      else if(v=="tree-nosum") c.summary=false;
      else if(v=="tree-hash") c.hash_index=true;
      else if(v.starts_with("tree-inline")) {c.inline_hash=true;c.page_target=unsigned(std::stoul(std::string(v.substr(11))));}
      else if(v=="flat") {c.fill=1;c.tile=0;c.bounded_root=false;c.exact_bounds=true;c.summary=false;c.append_split=false;}
      else {std::fprintf(stderr,"unknown variant %s\n",argv[a]);return 2;}
      x.tree=std::make_unique<Tree>(*x.store,c,1);
      x.tree->build(es);
      x.image=std::make_unique<Image>(*x.store);
      x.view.emplace<TreeView>(*x.image,*x.tree);
      verify(std::get<TreeView>(x.view),*x.tree,es,probes,fenced);
    }
    vs.push_back(std::move(x));
  }
  const char* metric[]={"find_dependent","find_independent","seek_read","logical_seek_read","scan_entry"};
  std::vector<std::vector<std::array<double,5>>> samples(vs.size());
  for(int r=0;r<reps;++r)
    for(std::size_t i=0;i<vs.size();++i) {
      auto v=(i+std::size_t(r))%vs.size();
      std::visit([&](auto& view) {
        if constexpr(!std::is_same_v<std::decay_t<decltype(view)>,std::monostate>)
          samples[v].push_back(pass(view,present,probes,lows,queries/1000+1));
      },vs[v].view);
    }
  std::printf("variant,layer,j,count,placement,metric,median_ns,min_ns,reps\n");
  for(std::size_t v=0;v<vs.size();++v)
    for(int m=0;m<5;++m) {
      if(m==3&&!fenced) continue;
      if(m==3&&vs[v].natural) continue;
      std::vector<double> xs;
      for(auto& s:samples[v]) xs.push_back(s[m]);
      std::sort(xs.begin(),xs.end());
      std::printf("%s,%u,%u,%zu,%s,%s,%.2f,%.2f,%d\n",vs[v].name.c_str(),layer,j,count,dense?"dense":"spread",
                  metric[m],xs[xs.size()/2],xs[0],reps);
    }
}
