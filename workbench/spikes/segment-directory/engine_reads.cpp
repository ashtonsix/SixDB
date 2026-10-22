#include "workload.h"
#include <engine/directory.h>
#include <chrono>
#include <cstdio>
#include <string>

// Read timings for the graduated engine::directory on real Orbital Objects,
// using the same populations and loops as bench.cpp's @orbital rows.
namespace ed=engine::directory;
namespace {
using Clock=std::chrono::steady_clock;
volatile std::uint64_t sink;
void require(bool ok,const char* what) {
  if(!ok) {std::fprintf(stderr,"engine reads: %s\n",what);std::exit(1);}
}
template<class F> double timed(std::size_t ops,F&& f) {
  auto t0=Clock::now();
  f();
  return std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/double(ops);
}
}

int main(int argc,char** argv) {
  if(argc<7) {std::fprintf(stderr,"usage: %s LAYER J COUNT dense|spread QUERIES REPS\n",argv[0]);return 2;}
  unsigned layer=unsigned(std::stoul(argv[1])),j=unsigned(std::stoul(argv[2]));
  std::size_t count=std::stoull(argv[3]),queries=std::stoull(argv[5]);
  bool dense=std::string(argv[4])=="dense";
  int reps=std::stoi(argv[6]);
  loom::Worker worker;
  require(bool(loom::adopt(worker)),"adopt");
  auto mb=std::getenv("SEGDIR_ARENA_MB")?std::stoull(std::getenv("SEGDIR_ARENA_MB")):4096;
  auto arena=orbital::allocation::open_arena(mb<<20);
  require(arena&&bool(loom::configure(worker,std::move(*arena),0)),"arena");
  {
    std::pmr::unsynchronized_pool_resource memory;
    ed::Store store(memory);
    segdir::Rng rng(layer*1000003+count*7+dense);
    segdir::Workload w{layer,count,dense};
    auto o=segdir::populate(w,rng);
    std::vector<segdir::Entry> rows;
    for(auto& [p,e]:o) rows.push_back(e);
    const unsigned k=12;
    std::vector<ed::Entry> es;
    for(auto& r:rows) {
      ed::Dirent d;
      std::memcpy(&d,&r.dirent,64);
      es.push_back({r.prefix,{r.min.b.data(),k},{r.max.b.data(),k},d});
    }
    ed::Layer l(worker,store,memory,0xd1,{.layer=layer,.key_bytes=k,.shared_prefix=j});
    require(bool(l.build(es)),"build");
    std::vector<std::uint32_t> present(queries),probes(queries);
    std::vector<segdir::Key> lows;
    for(auto& p:present) p=rows[rng()%rows.size()].prefix;
    for(auto& p:probes) p=std::uint32_t(w.lo+rng()%(w.hi-w.lo));
    bool fenced=layer>j;
    if(fenced) for(std::size_t i=0;i<queries;++i) lows.push_back(segdir::key(rng()%segdir::value(rows.back().max)));
    ed::Store::Scope scope(store);
    for(std::size_t i=0;i<rows.size();i+=std::max<std::size_t>(1,rows.size()/4096)) {
      auto* d=l.find(scope,rows[i].prefix);
      require(d&&!std::memcmp(d,&rows[i].dirent,64),"find");
    }
    const char* metric[]={"find_dependent","find_independent","seek_read","logical_seek_read","scan_entry"};
    std::vector<std::array<double,5>> samples;
    for(int r=0;r<reps;++r) {
      std::array<double,5> ns{};
      auto q=present.size();
      ns[0]=timed(q,[&] {
        std::uint64_t carry=0;
        for(std::size_t i=0;i<q;++i) carry=l.find(scope,present[(i+(carry&1))%q])->word[1];
        sink=carry;
      });
      ns[1]=timed(q,[&] {
        std::uint64_t sum=0;
        for(std::size_t i=0;i<q;++i) sum+=l.find(scope,present[i])->word[1];
        sink=sum;
      });
      ns[2]=timed(probes.size(),[&] {
        std::uint64_t sum=0;
        for(auto p:probes) if(auto c=l.seek(scope,p)) sum+=l.dirent(c).word[1];
        sink=sum;
      });
      if(fenced) ns[3]=timed(lows.size(),[&] {
        std::uint64_t sum=0;
        for(auto& low:lows) if(auto c=l.seek(scope,std::span<const std::uint8_t>(low.b.data(),k))) sum+=l.dirent(c).word[1];
        sink=sum;
      });
      std::size_t entries=0;
      std::uint64_t sum=0;
      auto t0=Clock::now();
      for(std::size_t s=0;s<queries/1000+1;++s) {
        auto c=l.seek(scope,probes[s%probes.size()]);
        for(int i=0;i<1000&&c;++i,l.next(scope,c)) {sum+=l.dirent(c).word[1]+l.prefix(c);++entries;}
      }
      sink=sum;
      ns[4]=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/double(entries);
      samples.push_back(ns);
    }
    for(int m=0;m<5;++m) {
      if(m==3&&!fenced) continue;
      std::vector<double> xs;
      for(auto& s:samples) xs.push_back(s[m]);
      std::sort(xs.begin(),xs.end());
      std::printf("engine-directory,%u,%u,%zu,%s,%s,%.2f,%.2f,%d\n",layer,j,count,dense?"dense":"spread",
                  metric[m],xs[xs.size()/2],xs[0],reps);
    }
  }
  for(;;) {
    auto p=loom::shutdown(worker);
    require(p.kind()!=orbital::Progress::Kind::failed,"shutdown");
    if(p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
}
