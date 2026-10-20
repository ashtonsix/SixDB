#include "graph_scan.h"
#include "scan_mask.h"
#include "../peers/lmdb_range.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {
using namespace index_spike;
struct Row { std::uint32_t key; std::array<std::uint64_t,8> values; };
void check(bool good,const char* why) {
  if (good) return;
  std::fprintf(stderr,"masked cursor: %s\n",why); std::abort();
}
void native(int status) { check(!status,mdb_strerror(status)); }
Row row(std::uint32_t key) {
  Row result{key,{}};
  for (unsigned f=0;f<8;++f) result.values[f]=UINT64_C(0x9e3779b97f4a7c15)*(key+71ULL+f*17);
  return result;
}
bool bit(const GraphScanRequest& request,std::uint32_t key) {
  if (key<request.low || key>request.high || request.low>request.high) return false;
  if (!request.mask) return true;
  if (key<request.mask_base || std::uint64_t(key)-request.mask_base>=request.mask_bits) return false;
  const auto offset=key-request.mask_base;
  return (request.mask[offset/64]>>(offset%64))&1;
}
void helper_oracle(const GraphScanRequest& request) {
  MaskRuns runs(request);
  std::uint32_t low{},high{};
  if (!request.mask) {
    const auto have=runs.next(low,high);
    check(have==(request.low<=request.high),"unmasked run emptiness");
    if (have) check(low==request.low && high==request.high,"unmasked run bounds");
    check(!runs.next(low,high),"unmasked run duplicated"); return;
  }
  std::vector<std::uint32_t> wanted,actual;
  for (std::uint64_t offset=0;offset<request.mask_bits;++offset) {
    const auto key=std::uint32_t(request.mask_base+offset);
    if (bit(request,key)) wanted.push_back(key);
  }
  std::uint64_t previous=0; bool first=true;
  while (runs.next(low,high)) {
    check(low<=high && (first || low>previous+1),"mask runs overlap or remain unjoined");
    for (std::uint64_t key=low;key<=high;++key) actual.push_back(key);
    previous=high; first=false;
  }
  check(actual==wanted,"mask run expansion differs from direct per-bit oracle");
  check(mask_has_candidate(request)==!wanted.empty(),"range/mask intersection guard differs");
}
void scan(MDB_txn* txn,MDB_dbi db,const std::vector<Row>& rows,const GraphScanRequest& request) {
  helper_oracle(request);
  MDB_cursor* cursor{}; native(mdb_cursor_open(txn,db,&cursor));
  std::vector<Row> actual;
  lmdb_range::Work work;
  if (mask_has_candidate(request)) native(lmdb_range::masked_runs(cursor,request,work,
      [&](std::uint32_t key,const MDB_val& bytes) {
        check(bytes.mv_size==64,"native exact material width");
        Row result{key,{}}; std::memcpy(result.values.data(),bytes.mv_data,64);
        actual.push_back(result);
      }));
  std::vector<Row> wanted;
  for (const auto& value:rows) if (bit(request,value.key)) wanted.push_back(value);
  check(actual.size()==wanted.size() && work.visited==wanted.size(),"native selected/run visit count");
  check(work.calls==work.seeks+work.nexts,"native cursor call accounting");
  if (wanted.empty() && !mask_has_candidate(request))
    check(!work.calls && !work.visited,"zero guard performed native traversal");
  for (unsigned i=0;i<actual.size();++i) check(actual[i].key==wanted[i].key &&
      actual[i].values==wanted[i].values,"native ordered complete row/key association");
  mdb_cursor_close(cursor);
}
void cases(MDB_txn* txn,MDB_dbi db,const std::vector<Row>& rows) {
  const std::array<std::array<std::uint32_t,2>,9> ranges{{{0,UINT32_MAX},{0,0},{1,16},
      {2,14},{13,65},{63,130},{UINT32_MAX-100,UINT32_MAX},{UINT32_MAX,UINT32_MAX},{7,6}}};
  for (const auto& range:ranges) {
    GraphScanRequest request{nullptr,nullptr,0,0,range[0],range[1],nullptr,0};
    scan(txn,db,rows,request);
    for (unsigned pattern=0;pattern<6;++pattern) {
      std::array<std::uint64_t,3> words{};
      for (unsigned i=0;i<words.size();++i) words[i]=pattern==0?0:pattern==1?UINT64_MAX:
          pattern==2?UINT64_C(0x8000000000000001):pattern==3?UINT64_C(0xaaaaaaaaaaaaaaaa):
          pattern==4?UINT64_C(0x0000000000000008):UINT64_C(0xf00000000000000f);
      request.mask=words.data(); request.mask_base=0; request.mask_bits=133;
      scan(txn,db,rows,request);
      request.mask_base=UINT64_C(0xffffff80); request.mask_bits=128;
      scan(txn,db,rows,request);
      request.mask_bits=0; scan(txn,db,rows,request);
    }
  }
}
void put(MDB_txn* txn,MDB_dbi db,const Row& row) {
  auto prefix=row.key; MDB_val key{4,&prefix},value{64,const_cast<std::uint64_t*>(row.values.data())};
  native(mdb_put(txn,db,&key,&value,0));
}
}
int main() {
  char path[]="/tmp/sixdb-masked-cursor-XXXXXX";
  check(mkdtemp(path),"temporary native environment");
  MDB_env* env{}; native(mdb_env_create(&env)); native(mdb_env_set_mapsize(env,16u<<20));
  native(mdb_env_set_maxdbs(env,1));
  native(mdb_env_open(env,path,MDB_NOTLS|MDB_NOSYNC|MDB_NOMETASYNC,0600));
  MDB_txn* writer{}; MDB_dbi db{}; native(mdb_txn_begin(env,nullptr,0,&writer));
  native(mdb_dbi_open(writer,"mask-runs",MDB_CREATE|MDB_INTEGERKEY,&db));
  std::vector<Row> before;
  for (std::uint32_t key:{0u,1u,3u,15u,16u,63u,64u,65u,129u,UINT32_MAX-64,UINT32_MAX}) {
    before.push_back(row(key)); put(writer,db,before.back());
  }
  native(mdb_txn_commit(writer));
  MDB_txn* old{}; native(mdb_txn_begin(env,nullptr,MDB_RDONLY,&old)); // no first seek yet
  native(mdb_txn_begin(env,nullptr,0,&writer));
  auto after=before; after[2].values[3]=UINT64_MAX; put(writer,db,after[2]);
  std::uint32_t deleted=15; MDB_val key{4,&deleted}; native(mdb_del(writer,db,&key,nullptr));
  after.erase(after.begin()+3); after.insert(after.begin()+3,row(9)); put(writer,db,row(9));
  native(mdb_txn_commit(writer));
  cases(old,db,before);
  MDB_txn* fresh{}; native(mdb_txn_begin(env,nullptr,MDB_RDONLY,&fresh)); cases(fresh,db,after);
  mdb_txn_abort(fresh); mdb_txn_abort(old); mdb_env_close(env);
  std::filesystem::remove(std::filesystem::path(path)/"data.mdb");
  std::filesystem::remove(std::filesystem::path(path)/"lock.mdb");
  std::filesystem::remove(path);
  std::puts("PASS: selected-mask native run cursors, direct per-bit/ordered full64 oracle, cross-word/partial/zero/max/gaps and late-old/fresh real txns; no timing");
}
