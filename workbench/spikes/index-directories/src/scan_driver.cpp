#include "graph_scan.h"
#include "group_fixture.h"
#include "../peers/lmdb_range.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <time.h>
#include <vector>

namespace {
using namespace index_spike;
void check(bool good,const char* why) {
  if (good) return;
  std::fprintf(stderr,"range driver: %s\n",why); std::abort();
}
void native(int status) {
  if (!status) return;
  std::fprintf(stderr,"LMDB: %s\n",mdb_strerror(status)); std::abort();
}
std::uint64_t mix(std::uint64_t x) {
  x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
  x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb); return x^(x>>31);
}
std::uint64_t digest(const std::byte* bytes) {
  std::uint64_t result=0;
  for (unsigned j=0;j<8;++j) {
    std::uint64_t word; std::memcpy(&word,bytes+j*8,8); result^=word;
  }
  return result;
}
std::vector<Record> records(std::size_t count) {
  std::vector<Record> rows(count);
  for (unsigned i=0;i<count;++i) {
    auto& row=rows[i]; row.prefix=i;
    for (unsigned j=0;j<8;++j) {
      const auto value=mix(std::uint64_t(i)*17+j+71);
      std::memcpy(row.material.data()+j*8,&value,8);
      std::memcpy(row.descriptor.data()+j*8,&value,8);
    }
  }
  return rows;
}
std::vector<std::uint64_t> mask(std::size_t count,unsigned kept) {
  std::vector<std::uint64_t> words((count+63)/64);
  for (unsigned word=0;word<words.size();++word) {
    std::array<unsigned,64> order;
    for (unsigned i=0;i<64;++i) order[i]=i;
    auto state=mix(713+word);
    for (unsigned i=63;i;--i) {
      state=mix(state); std::swap(order[i],order[state%(i+1)]);
    }
    for (unsigned i=0;i<kept;++i) words[word]|=UINT64_C(1)<<order[i];
  }
  if (count%64) words.back()&=(UINT64_C(1)<<(count%64))-1;
  return words;
}
bool selected(const GraphScanRequest& request,std::uint32_t prefix) {
  return !request.mask || (prefix>=request.mask_base &&
      std::uint64_t(prefix)-request.mask_base<request.mask_bits &&
      ((request.mask[(prefix-request.mask_base)/64]>>((prefix-request.mask_base)%64))&1));
}
ScanResult expected(const std::vector<Record>& rows,const GraphScanRequest& request) {
  ScanResult result{};
  for (const auto& row:rows) if (row.prefix>=request.low && row.prefix<=request.high &&
      selected(request,row.prefix)) { ++result.selected; result.sum+=digest(row.material.data()); }
  return result;
}
struct Native {
  MDB_env* env{}; MDB_dbi db{}; MDB_txn* reader{}; MDB_cursor* cursor{};
  ~Native() {
    if (cursor) mdb_cursor_close(cursor);
    if (reader) mdb_txn_abort(reader);
    if (env) mdb_env_close(env);
  }
  void load(const char* path,const std::vector<Record>& rows) {
    check(std::filesystem::create_directory(path),"new, unused DB directory required");
    native(mdb_env_create(&env)); native(mdb_env_set_maxdbs(env,1));
    native(mdb_env_set_mapsize(env,1u<<30));
    native(mdb_env_open(env,path,MDB_NOTLS|MDB_NOSYNC|MDB_NOMETASYNC,0600));
    MDB_txn* writer{}; native(mdb_txn_begin(env,nullptr,0,&writer));
    native(mdb_dbi_open(writer,"range-u32",MDB_INTEGERKEY|MDB_CREATE,&db));
    for (const auto& row:rows) {
      auto prefix=row.prefix;
      MDB_val key{sizeof(prefix),&prefix},value{64,const_cast<std::byte*>(row.material.data())};
      native(mdb_put(writer,db,&key,&value,MDB_NOOVERWRITE));
    }
    native(mdb_txn_commit(writer)); native(mdb_txn_begin(env,nullptr,MDB_RDONLY,&reader));
    native(mdb_cursor_open(reader,db,&cursor));
    unsigned flags{}; native(mdb_dbi_flags(reader,db,&flags));
    check(flags==MDB_INTEGERKEY,"native database key policy differs");
    mdb_txn_reset(reader);
  }
  // Policy0 preserves the original full-walk control;1 guards that walk;
  //2 guards then seeks actual maximal selected mask runs in this SAME txn.
  template<bool Instrumented=false>
  ScanResult scan(const GraphScanRequest& request,lmdb_range::Work* counters=nullptr,
      unsigned policy=0) {
    native(mdb_txn_renew(reader));
    ScanResult result{};
    lmdb_range::Work work;
    if ((!policy && (!request.mask || request.mask_bits)) ||
        (policy && mask_has_candidate(request))) {
      native(mdb_cursor_renew(reader,cursor));
      const auto consume=[&](std::uint32_t,const MDB_val& value) {
        result.sum+=digest(static_cast<const std::byte*>(value.mv_data)); ++result.selected;
      };
      native(policy==2?lmdb_range::masked_runs<Instrumented>(cursor,request,work,consume):
          lmdb_range::scan<Instrumented>(cursor,request.low,request.high,work,
              [&](std::uint32_t prefix,const MDB_val& value) {
                if (selected(request,prefix)) consume(prefix,value);
              }));
    }
    if constexpr (Instrumented) { if (counters) *counters=work; }
    mdb_txn_reset(reader); return result;
  }
};
ScanResult engine(GroupFixture& fixture,GraphScanRequest request,ScanCounters* counters=nullptr,
    unsigned kernel=0,bool guard=false) {
  Store::Scope scope(fixture.store());
  if (guard && !mask_has_candidate(request)) {
    if (counters) *counters={};
    return {};
  }
  const auto root=scope.bind({GroupFixture::graph_space,GroupFixture::root_base});
  check(bool(root),"selected root absent");
  const auto part=root.part(0); check(bool(part),"selected root Part absent");
  request.root=part->address();
#if defined(__aarch64__)
  if (kernel==1) return idx_asm_graph_scan64_dense(&request,counters);
  if (kernel==2) return idx_asm_graph_scan64_lookahead_none(&request,counters);
  if (kernel==3) return idx_asm_graph_scan64_lookahead(&request,counters);
#else
  check(!kernel,"native scan candidates are ARM-only");
#endif
  return idx_asm_graph_scan64(&request,counters);
}
std::uint64_t now(clockid_t id) {
  timespec time{}; check(!clock_gettime(id,&time),"clock");
  return std::uint64_t(time.tv_sec)*1000000000+time.tv_nsec;
}
struct Clock {
  std::uint64_t wall=now(CLOCK_MONOTONIC_RAW),owner=now(CLOCK_THREAD_CPUTIME_ID),
      process=now(CLOCK_PROCESS_CPUTIME_ID);
};
void run(loom::Worker& worker,const char* path,std::size_t count,unsigned repeats,
    unsigned grain,const char* mode) {
  const auto rows=records(count);
  GroupFixture fixture(worker,rows,nullptr,true,grain);
  Native peer; peer.load(path,rows);
  std::printf("trial,position,arm,population,grain,range,mask_kept_per64,scans_per_sample,visited_per_scan,selected_per_scan,material_bytes_per_scan,wall_ns,owner_ns,process_ns,checksum,offered_per_scan,native_cursor_gets_per_scan,native_seeks_per_scan,native_nexts_per_scan,positioned_per_scan,boundary_per_scan,visited_groups_per_scan,visited_nodes_per_scan,mask_guard\n");
  const bool dense=!std::strcmp(mode,"dense"),densities=!std::strcmp(mode,"densities"),
      lookahead=!std::strcmp(mode,"lookahead"),sparse_peer=!std::strcmp(mode,"sparse-peer");
  struct Arm {
    const char* label;
    bool is_engine;
    unsigned kernel{},native_policy{};
    bool guard{};
  };
  std::array<Arm,4> configurations{{{"engine_native_scan",true},
      {"lmdb_native_cursor",false}}};
  const unsigned arms=lookahead?4:dense||sparse_peer?3:2;
  if (dense) configurations[2]={"engine_dense_rank",true,1};
  if (lookahead) {
    configurations[0]={"engine_dense_rank",true,1};
    configurations[2]={"engine_pointerbank_none",true,2};
    configurations[3]={"engine_pointerbank_hint",true,3};
  }
  if (sparse_peer) {
#if defined(__aarch64__)
    constexpr unsigned strongest=1;
#else
    constexpr unsigned strongest=0;
#endif
    configurations[0]={"engine_guarded_scan",true,strongest,0,true};
    configurations[1]={"lmdb_guarded_cursor",false,0,1,true};
    configurations[2]={"lmdb_guarded_mask_runs",false,0,2,true};
  }
  const auto ranges=dense||lookahead?std::vector<const char*>{"full"}:
      std::vector<const char*>{"quarter","full"};
  for (const char* range:ranges) {
    const auto low=std::strcmp(range,"full")?count/4:0;
    const auto high=std::strcmp(range,"full")?count/2-1:count-1;
    const auto choices=sparse_peer?std::vector<int>{0,1}:dense||lookahead?
        std::vector<int>{-1}:densities?std::vector<int>{-1,0,1,8,32,64}:std::vector<int>{-1,8};
    for (const auto kept:choices) {
      const auto bits=mask(count,kept<0?64:unsigned(kept));
      const GraphScanRequest request{nullptr,kept<0?nullptr:bits.data(),0,count,
          std::uint32_t(low),std::uint32_t(high),nullptr,0};
      const auto want=expected(rows,request);
      // This fixture's admitted directory has exactly one entry per original
      // prefix0..count-1. Offered work is shared even when an arm visits none.
      const auto offered=std::uint64_t(high-low+1);
      std::array<ScanCounters,4> engine_counts{};
      std::array<lmdb_range::Work,4> native_counts{};
      std::array<std::uint64_t,4> visits{};
      for (unsigned arm=0;arm<arms;++arm) {
        const auto& c=configurations[arm];
        const auto actual=c.is_engine?engine(fixture,request,&engine_counts[arm],c.kernel,c.guard):
            peer.scan<true>(request,&native_counts[arm],c.native_policy);
        check(actual.sum==want.sum && actual.selected==want.selected,"untimed complete scan output differs");
        visits[arm]=c.is_engine?engine_counts[arm].visited_dirents:native_counts[arm].visited;
        const auto wanted_visits=c.guard && !want.selected?0:
            c.native_policy==2?want.selected:offered;
        check(visits[arm]==wanted_visits,"actual traversal count differs from admitted dense range/policy");
        if (c.is_engine) check(engine_counts[arm].material_bytes==want.selected*64,
            "actual selected material count differs");
        else check(native_counts[arm].calls==native_counts[arm].seeks+native_counts[arm].nexts,
            "native cursor call accounting differs");
      }
      if (dense || lookahead) for (unsigned arm=2;arm<arms;++arm)
        check(!std::memcmp(&engine_counts[0],&engine_counts[arm],sizeof(ScanCounters)),
            "candidate traversal counters differ");
      auto scans=std::max<std::uint64_t>(1,((1u<<20)+offered-1)/offered);
      if (sparse_peer) scans=std::max<std::uint64_t>(scans,kept?16:8192);
      static constexpr unsigned permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},
          {1,2,0},{2,0,1},{2,1,0}};
      // Four rows balance every position and all twelve directed adjacent-arm
      // pairs. Per-arm self-warm remains necessary for the timed scan itself.
      static constexpr unsigned lookahead_order[4]={0,1,3,2};
      for (unsigned trial=0;trial<repeats;++trial) for (unsigned position=0;position<arms;++position) {
        const auto arm=dense||sparse_peer?permutations[trial%6][position]:lookahead?
            (lookahead_order[position]+trial)%4:(trial+position)%arms;
        const auto& c=configurations[arm];
        const auto invoke=[&] {
          return c.is_engine?engine(fixture,request,nullptr,c.kernel,c.guard):
              peer.scan(request,nullptr,c.native_policy);
        };
        // Dense comparison: one complete, checked self-warm scan per arm avoids
        // confounding the first timed scan with a different predecessor's data.
        if (dense||lookahead||sparse_peer) {
          const auto warm=invoke();
          check(warm.sum==want.sum && warm.selected==want.selected,"dense arm warmup differs");
        }
        const Clock start;
        ScanResult result{};
        for (std::uint64_t scan=0;scan<scans;++scan) {
          const auto one=invoke();
          result.sum+=one.sum; result.selected+=one.selected;
        }
        const auto wall=now(CLOCK_MONOTONIC_RAW)-start.wall;
        const auto owner=now(CLOCK_THREAD_CPUTIME_ID)-start.owner;
        const auto process=now(CLOCK_PROCESS_CPUTIME_ID)-start.process;
        check(result.sum==want.sum*scans && result.selected==want.selected*scans,"timed useful output differs");
        const auto& nw=native_counts[arm]; const auto& ec=engine_counts[arm];
        std::printf("%u,%u,%s,%zu,%u,%s,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
            trial,position,c.label,
            count,grain,range,kept,
            (unsigned long long)scans,(unsigned long long)visits[arm],
            (unsigned long long)want.selected,(unsigned long long)(want.selected*64),(unsigned long long)wall,
            (unsigned long long)owner,(unsigned long long)process,(unsigned long long)result.sum,
            (unsigned long long)offered,(unsigned long long)nw.calls,
            (unsigned long long)nw.seeks,(unsigned long long)nw.nexts,
            (unsigned long long)nw.positioned,(unsigned long long)nw.boundary,
            (unsigned long long)ec.visited_groups,(unsigned long long)ec.visited_nodes,unsigned(c.guard));
      }
    }
  }
}
}
int main(int argc,char** argv) {
  check(argc==7,"usage: scan_driver NEW_DB_DIR POPULATION REPEATS GRAIN HELPER_CPU basic|densities|dense|lookahead|sparse-peer");
  const auto count=std::strtoull(argv[2],nullptr,10);
  const unsigned repeats=std::strtoul(argv[3],nullptr,10),grain=std::strtoul(argv[4],nullptr,10);
  check(count>=4 && count<=1u<<24 && repeats>=2 && !(count%4),"bounded count/repeats");
  check(grain==16 || grain==64 || grain==256,"physical grain16/64/256");
  check(!std::strcmp(argv[6],"basic") || !std::strcmp(argv[6],"densities") ||
      !std::strcmp(argv[6],"dense") || !std::strcmp(argv[6],"lookahead") ||
      !std::strcmp(argv[6],"sparse-peer"),"basic|densities|dense|lookahead|sparse-peer");
  loom::Worker worker; check(bool(loom::adopt(worker)),"Worker adoption");
  auto arena=orbital::allocation::open_arena(512u<<20,std::atoi(argv[5]));
  check(bool(arena) && bool(loom::configure(worker,std::move(*arena),0)),"Worker configuration");
  run(worker,argv[1],count,repeats,grain,argv[6]);
  while (true) {
    const auto result=loom::shutdown(worker);
    check(result.kind()!=orbital::Progress::Kind::failed,"physical shutdown");
    if (result.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  loom::relinquish(worker);
}
