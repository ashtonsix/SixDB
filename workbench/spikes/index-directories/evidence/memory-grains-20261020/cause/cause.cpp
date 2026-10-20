// Isolate directory placement over the SAME actual independent Segment bytes.
// Engine spike diagnostic; no Loom runtime API or complete-operation claim.
#define main retained_screen_main
#include "/home/ashtonsix/sixdb/build/architecture-index-memory-fix-20261020/grains/screen.cpp"
#undef main
#include <algorithm>
#include <memory>
#include <set>

namespace index_spike { extern const Store::Scope* loom_causal_material_source; }
namespace {
struct Addresses { std::vector<std::uintptr_t> material,dirent; };
void inventory(const char* arm,const char* kind,const std::vector<std::uintptr_t>& v) {
  auto sorted=v; std::sort(sorted.begin(),sorted.end());
  check(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end(),"address alias");
  std::set<std::uintptr_t> pages,lines;
  std::uint64_t normalized=UINT64_C(1469598103934665603);
  for(auto a:v) {pages.insert(a>>12);lines.insert(a>>6);normalized=(normalized^(a-sorted.front()))*UINT64_C(1099511628211);}
  std::printf("{\"kind\":\"inventory\",\"arm\":\"%s\",\"bytes\":\"%s\",\"items\":%zu,\"va_lines64\":%zu,\"va_pages4096\":%zu,\"min\":%llu,\"max\":%llu,\"normalized_fnv\":%llu}\n",
      arm,kind,v.size(),lines.size(),pages.size(),(unsigned long long)sorted.front(),
      (unsigned long long)sorted.back(),(unsigned long long)normalized);
}
Addresses census(const char* name,GroupFixture& fixture,const std::vector<Record>& rows,
    const Addresses* same) {
  Addresses a; a.material.reserve(rows.size());a.dirent.reserve(rows.size());
  Store::Scope scope(fixture.store());
  auto selected=scope.bind({GroupFixture::graph_space,GroupFixture::root_base});
  check(bool(selected),"census root");auto root=selected.part(0);check(bool(root),"census root Part");
  for(std::size_t i=0;i<rows.size();++i) {
    auto d=idx_asm_graph32_bound(root->address(),rows[i].prefix);check(d,"census dirent");
    const std::byte* m{};std::memcpy(&m,d,8);
    auto material=scope.bind(GroupFixture::material_id(rows[i].prefix));check(bool(material),"census material");
    auto p=material.part(0);check(p && p->address()==m,"actual material association");
    check(!std::memcmp(m,rows[i].material.data(),64),"full material oracle");
    check(!std::memcmp(d+8,rows[i].descriptor.data()+8,56),"metadata oracle");
    if(same) check(same->material[i]==reinterpret_cast<std::uintptr_t>(m),"shared material address differs");
    a.material.push_back(reinterpret_cast<std::uintptr_t>(m));a.dirent.push_back(reinterpret_cast<std::uintptr_t>(d));
  }
  inventory(name,"material64",a.material);inventory(name,"dirent64",a.dirent);
  std::printf("{\"kind\":\"association\",\"arm\":\"%s\",\"checked\":%zu,\"same_actual_material_addresses\":%s,\"graph_nodes\":%zu,\"directory_allocations\":%zu}\n",
      name,rows.size(),same?"true":"null",fixture.initial_graph_nodes(),fixture.initial_directory_allocations());
  return a;
}
}
int main(int argc,char** argv) {
  using namespace index_spike;
  check(argc==6,"cause POPULATION SEED_GRAIN CHILD_BUILD_ORDER READS qualify|time");
  const auto n=std::strtoull(argv[1],nullptr,10),requests=std::strtoull(argv[4],nullptr,10);
  const unsigned seed=std::strtoul(argv[2],nullptr,10),order=std::strtoul(argv[3],nullptr,10);
  const bool timing=!std::strcmp(argv[5],"time");
  check((timing || !std::strcmp(argv[5],"qualify")) && n && std::has_single_bit(n) && requests && !(requests%64) &&
      (seed==16 || seed==256) && order<2,"arguments");
  loom::Worker worker;check(bool(loom::adopt(worker)),"adopt");
  auto arena=orbital::allocation::open_arena(512u<<20,0);check(bool(arena),"arena");
  check(bool(loom::configure(worker,std::move(*arena),0)),"configure");
  {
    auto rows=records(n,false);
    GroupFixture master(worker,rows,nullptr,true,seed);
    auto source_scope=std::make_unique<Store::Scope>(master.store());
    loom_causal_material_source=source_scope.get();
    std::unique_ptr<GroupFixture> d16,d256;
    if(!order) { d16=std::make_unique<GroupFixture>(worker,rows,nullptr,true,16);d256=std::make_unique<GroupFixture>(worker,rows,nullptr,true,256); }
    else { d256=std::make_unique<GroupFixture>(worker,rows,nullptr,true,256);d16=std::make_unique<GroupFixture>(worker,rows,nullptr,true,16); }
    loom_causal_material_source=nullptr;source_scope.reset();
    {
      auto original=census("original",master,rows,nullptr);
      (void)census("shared_D16",*d16,rows,&original);
      (void)census("shared_D256",*d256,rows,&original);
    }
    std::vector<std::uint32_t> keys(n),trace(requests);std::vector<unsigned> indices(requests);
    for(std::size_t i=0;i<n;++i)keys[i]=rows[i].prefix;
    for(std::size_t i=0;i<requests;++i) {indices[i]=unsigned(mix(i+17)&(n-1));trace[i]=keys[indices[i]];}
    GroupFixture* fixtures[]={&master,d16.get(),d256.get()};
    const char* names[]={"original","shared_D16","shared_D256"};
    for(auto* fixture:fixtures)batch_oracle(*fixture,rows,keys,indices);
    const auto sum=expected(rows,indices,false,true);
    std::printf("{\"kind\":\"qualification\",\"seed_grain\":%u,\"child_build_order\":%u,\"population\":%llu,\"requests\":%llu,\"sum\":%llu,\"timing_enabled\":%s}\n",
        seed,order,n,requests,(unsigned long long)sum,timing?"true":"false");
    if(timing) {
      const unsigned orders[6][3]={{0,1,2},{2,1,0},{1,0,2},{2,0,1},{0,2,1},{1,2,0}};
      std::puts("trial,position,operation,arm,stream,cohort,population,requests,wall_ns_op,owner_ns_op,process_ns_op,checksum");
      for(unsigned trial=0;trial<6;++trial)for(unsigned position=0;position<3;++position) {
        const unsigned arm=orders[trial][position];
        const auto result=batch_read(*fixtures[arm],trace,indices);check(result.sum==sum,"timed useful output");
        std::printf("%u,%u,",trial,position);report("material64",names[arm],"independent",64,n,requests,result);std::fflush(stdout);
      }
    }
  }
  for(;;) {auto p=loom::shutdown(worker);check(p.kind()!=orbital::Progress::Kind::failed,"shutdown");if(p.kind()==orbital::Progress::Kind::done)break;loom::poll(worker,{64,0});}
  loom::relinquish(worker);std::fprintf(stderr,"PASS: actual shared Segment addresses, full bytes/associations, per-input batch checks and physical shutdown\n");
}
