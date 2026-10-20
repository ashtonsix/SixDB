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
  ScanResult scan(const GraphScanRequest& request,lmdb_range::Work* counters=nullptr) {
    native(mdb_txn_renew(reader)); native(mdb_cursor_renew(reader,cursor));
    ScanResult result{};
    lmdb_range::Work work;
    if (!request.mask || request.mask_bits)
      native(lmdb_range::scan(cursor,request.low,request.high,work,
          [&](std::uint32_t prefix,const MDB_val& value) {
            if (!selected(request,prefix)) return;
            result.sum+=digest(static_cast<const std::byte*>(value.mv_data)); ++result.selected;
          }));
    if (counters) *counters=work;
    mdb_txn_reset(reader); return result;
  }
};
ScanResult engine(GroupFixture& fixture,GraphScanRequest request,ScanCounters* counters=nullptr,
    unsigned kernel=0) {
  Store::Scope scope(fixture.store());
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
  std::printf("trial,position,arm,population,grain,range,mask_kept_per64,scans_per_sample,visited_per_scan,selected_per_scan,material_bytes_per_scan,wall_ns,owner_ns,process_ns,checksum\n");
  const bool dense=!std::strcmp(mode,"dense"),densities=!std::strcmp(mode,"densities"),
      lookahead=!std::strcmp(mode,"lookahead");
  const auto ranges=dense||lookahead?std::vector<const char*>{"full"}:
      std::vector<const char*>{"quarter","full"};
  for (const char* range:ranges) {
    const auto low=std::strcmp(range,"full")?count/4:0;
    const auto high=std::strcmp(range,"full")?count/2-1:count-1;
    const auto choices=dense||lookahead?std::vector<int>{-1}:densities?std::vector<int>{-1,0,1,8,32,64}:
        std::vector<int>{-1,8};
    for (const auto kept:choices) {
      const auto bits=mask(count,kept<0?64:unsigned(kept));
      const GraphScanRequest request{nullptr,kept<0?nullptr:bits.data(),0,count,
          std::uint32_t(low),std::uint32_t(high),nullptr,0};
      const auto want=expected(rows,request);
      ScanCounters visited; lmdb_range::Work native_visited;
      const auto e=engine(fixture,request,&visited),n=peer.scan(request,&native_visited);
      check(e.sum==want.sum && e.selected==want.selected && n.sum==want.sum &&
          n.selected==want.selected,"untimed complete scan output differs");
      if (dense) {
        ScanCounters dense_visited;
        const auto d=engine(fixture,request,&dense_visited,true);
        check(d.sum==want.sum && d.selected==want.selected &&
            !std::memcmp(&visited,&dense_visited,sizeof(visited)),"untimed dense-rank output/counters differ");
      }
      if (lookahead) for (unsigned kernel:{1u,2u,3u}) {
        ScanCounters actual;
        const auto d=engine(fixture,request,&actual,kernel);
        check(d.sum==want.sum && d.selected==want.selected &&
            !std::memcmp(&visited,&actual,sizeof(visited)),"untimed pointer-bank output/counters differ");
      }
      check(visited.visited_dirents==native_visited.visited,"range visit boundary differs");
      const auto scans=std::max<std::uint64_t>(1,((1u<<20)+visited.visited_dirents-1)/visited.visited_dirents);
      const unsigned arms=lookahead?4:dense?3:2;
      static constexpr unsigned permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},
          {1,2,0},{2,0,1},{2,1,0}};
      // Four rows balance every position and all twelve directed adjacent-arm
      // pairs. Per-arm self-warm remains necessary for the timed scan itself.
      static constexpr unsigned lookahead_order[4]={0,1,3,2};
      for (unsigned trial=0;trial<repeats;++trial) for (unsigned position=0;position<arms;++position) {
        const auto arm=dense?permutations[trial%6][position]:lookahead?
            (lookahead_order[position]+trial)%4:(trial+position)%arms;
        const bool is_engine=arm!=1;
        const unsigned kernel=lookahead?(arm==0?1:arm):dense&&arm==2?1:0;
        const char* label=is_engine?(kernel==1?"engine_dense_rank":kernel==2?
            "engine_pointerbank_none":kernel==3?"engine_pointerbank_hint":"engine_native_scan"):
            "lmdb_native_cursor";
        // Dense comparison: one complete, checked self-warm scan per arm avoids
        // confounding the first timed scan with a different predecessor's data.
        if (dense||lookahead) {
          const auto warm=is_engine?engine(fixture,request,nullptr,kernel):peer.scan(request);
          check(warm.sum==want.sum && warm.selected==want.selected,"dense arm warmup differs");
        }
        const Clock start;
        ScanResult result{};
        for (std::uint64_t scan=0;scan<scans;++scan) {
          const auto one=is_engine?engine(fixture,request,nullptr,kernel):peer.scan(request);
          result.sum+=one.sum; result.selected+=one.selected;
        }
        const auto wall=now(CLOCK_MONOTONIC_RAW)-start.wall;
        const auto owner=now(CLOCK_THREAD_CPUTIME_ID)-start.owner;
        const auto process=now(CLOCK_PROCESS_CPUTIME_ID)-start.process;
        check(result.sum==want.sum*scans && result.selected==want.selected*scans,"timed useful output differs");
        std::printf("%u,%u,%s,%zu,%u,%s,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            trial,position,label,
            count,grain,range,kept,
            (unsigned long long)scans,(unsigned long long)visited.visited_dirents,
            (unsigned long long)want.selected,(unsigned long long)(want.selected*64),(unsigned long long)wall,
            (unsigned long long)owner,(unsigned long long)process,(unsigned long long)result.sum);
      }
    }
  }
}
}
int main(int argc,char** argv) {
  check(argc==7,"usage: scan_driver NEW_DB_DIR POPULATION REPEATS GRAIN HELPER_CPU basic|densities|dense|lookahead");
  const auto count=std::strtoull(argv[2],nullptr,10);
  const unsigned repeats=std::strtoul(argv[3],nullptr,10),grain=std::strtoul(argv[4],nullptr,10);
  check(count>=4 && count<=1u<<24 && repeats>=2 && !(count%4),"bounded count/repeats");
  check(grain==16 || grain==64 || grain==256,"physical grain16/64/256");
  check(!std::strcmp(argv[6],"basic") || !std::strcmp(argv[6],"densities") ||
      !std::strcmp(argv[6],"dense") || !std::strcmp(argv[6],"lookahead"),"basic|densities|dense|lookahead");
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
