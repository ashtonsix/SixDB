#include "group_fixture.h"
#include "../peers/lmdb.h"
#include <time.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace index_spike;
namespace peer=index_spike::lmdb_peer;
void check(bool yes,const char* why) {
  if (yes) return;
  std::fprintf(stderr,"driver: %s\n",why); std::abort();
}
void native(int status) {
  if (!status) return;
  std::fprintf(stderr,"LMDB: %s\n",mdb_strerror(status)); std::abort();
}
std::uint64_t ns(clockid_t clock) {
  timespec t{}; check(!clock_gettime(clock,&t),"clock_gettime");
  return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
struct Clock {
  std::uint64_t wall=ns(CLOCK_MONOTONIC_RAW),owner=ns(CLOCK_THREAD_CPUTIME_ID),
      process=ns(CLOCK_PROCESS_CPUTIME_ID);
};
struct Result { std::uint64_t wall{},owner{},process{},sum{}; };
Result stop(const Clock& start,std::uint64_t sum) {
  const auto wall=ns(CLOCK_MONOTONIC_RAW);
  const auto owner=ns(CLOCK_THREAD_CPUTIME_ID);
  const auto process=ns(CLOCK_PROCESS_CPUTIME_ID);
  return {wall-start.wall,owner-start.owner,process-start.process,sum};
}
void report(const char* operation,const char* arm,const char* stream,unsigned cohort,
    std::size_t population,std::size_t requests,const Result& r) {
  std::printf("%s,%s,%s,%u,%zu,%zu,%.3f,%.3f,%.3f,%llu\n",operation,arm,stream,
      cohort,population,requests,double(r.wall)/requests,double(r.owner)/requests,
      double(r.process)/requests,static_cast<unsigned long long>(r.sum));
}
std::uint64_t word(const std::byte* bytes,unsigned ordinal) {
  std::uint64_t value; std::memcpy(&value,bytes+ordinal*8,8); return value;
}
void set_word(std::byte* bytes,unsigned ordinal,std::uint64_t value) {
  std::memcpy(bytes+ordinal*8,&value,8);
}
void stock(loom::Worker& worker,const char* phase) {
  const auto s=orbital::allocation::statistics(*worker.memory);
  std::fprintf(stderr,"stock phase=%s mapped=%llu reserved=%llu metadata=%llu idle=%llu "
      "preparing=%llu uncertain=%llu allocations=%llu reuses=%llu foreign_returns=%llu\n",phase,
      static_cast<unsigned long long>(s.mapped_bytes),static_cast<unsigned long long>(s.reserved_bytes),
      static_cast<unsigned long long>(s.metadata_bytes),static_cast<unsigned long long>(s.idle_bytes),
      static_cast<unsigned long long>(s.preparing_bytes),static_cast<unsigned long long>(s.uncertain_bytes),
      static_cast<unsigned long long>(s.allocations),static_cast<unsigned long long>(s.reuses),
      static_cast<unsigned long long>(s.foreign_returns));
}
std::uint64_t mix(std::uint64_t x) {
  x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
  x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);
  return x^(x>>31);
}
std::vector<Record> records(std::size_t count,bool sparse) {
  std::vector<Record> out(count);
  for (std::size_t i=0;i<count;++i) {
    // Sparse occupancy1/16 is a pressure control, not a new key allocator.
    auto& r=out[i]; r.prefix=std::uint32_t(sparse?i*16:i);
    for (unsigned j=0;j<8;++j) {
      set_word(r.descriptor.data(),j,mix(i*13+j+1));
      set_word(r.material.data(),j,mix(i*17+j+71));
    }
    set_word(r.descriptor.data(),0,r.prefix); // peer's semantic locator
  }
  return out;
}
peer::Packed128 packed(const Record& r,bool material_first) {
  return material_first?peer::Packed128{r.material,r.descriptor}:peer::Packed128{r.descriptor,r.material};
}
const std::byte* material(const MDB_val& value,bool material_first) {
  return static_cast<const std::byte*>(value.mv_data)+(material_first?0:64);
}
void load(peer::Environment& env,const std::vector<Record>& rows,bool material_first) {
  MDB_txn* txn{}; native(mdb_txn_begin(env.handle,nullptr,0,&txn));
  for (const auto& row:rows) native(peer::put(txn,env.db,row.prefix,packed(row,material_first)));
  native(mdb_txn_commit(txn));
}
void oracle(GroupFixture& fixture,peer::Environment& env,const std::vector<Record>& rows,bool material_first) {
  Store::Scope scope(fixture.store());
  ReadContext context{GroupFixture::directory_space,&scope,{}};
  const auto view=fixture.view(context);
  MDB_txn* txn{}; native(mdb_txn_begin(env.handle,nullptr,MDB_RDONLY,&txn));
  for (const auto& row:rows) {
    const auto* descriptor=idx_asm_group16(&view,row.prefix,nullptr);
    check(descriptor,"missing directory entry in full oracle");
    const std::byte* material; std::memcpy(&material,descriptor,8);
    const auto selected=scope.bind(GroupFixture::material_id(row.prefix));
    check(bool(selected),"directory locator has no actual material Object");
    const auto part=selected.part(0);
    check(part && part->address()==material,"directory locator differs from selected actual material");
    check(!std::memcmp(material,row.material.data(),64),"Engine full material oracle");
    check(!std::memcmp(descriptor+8,row.descriptor.data()+8,56),"Engine metadata oracle");
    MDB_val value{}; native(peer::get(txn,env.db,row.prefix,value));
    const auto expected=packed(row,material_first);
    check(value.mv_size==128 && !std::memcmp(value.mv_data,&expected,128),"LMDB full oracle");
  }
  mdb_txn_abort(txn);
}
std::uint64_t consume(const std::byte* data,bool full) {
  return full?idx_asm_digest64(data):word(data,0);
}
// Exactly the same logical stream/output oracle, independent of native layout.
std::uint64_t expected(const std::vector<Record>& rows,const std::vector<unsigned>& indices,
    bool dependent,bool full) {
  std::uint64_t sum=0,state=0;
  for (std::size_t i=0;i<indices.size();++i) {
    const auto index=dependent?state&(rows.size()-1):indices[i];
    std::uint64_t value=0;
    if (full) for (unsigned j=0;j<8;++j) value^=word(rows[index].material.data(),j);
    else value=word(rows[index].material.data(),0);
    sum+=value; state=mix(value+i);
  }
  return sum;
}
Result engine_read(GroupFixture& fixture,std::span<const std::uint32_t> keys,
    const std::vector<unsigned>& indices,unsigned cohort,bool dependent,bool full) {
  std::uint64_t sum=0,state=0;
  const Clock start;
  for (std::size_t first=0;first<indices.size();first+=cohort) {
    Store::Scope scope(fixture.store());
    ReadContext context{GroupFixture::directory_space,&scope,{}};
    const auto view=fixture.view(context);
    for (unsigned j=0;j<cohort;++j) {
      const auto i=first+j;
      const auto index=dependent?state&(keys.size()-1):indices[i];
      const auto* descriptor=idx_asm_group16(&view,keys[index],nullptr);
      check(descriptor,"Engine timed read missing");
      const std::byte* data; std::memcpy(&data,descriptor,8);
      const auto value=consume(data,full);
      sum+=value; state=mix(value+i);
    }
  }
  return stop(start,sum);
}
Result lmdb_read(peer::Environment& env,std::span<const std::uint32_t> keys,
    const std::vector<unsigned>& indices,unsigned cohort,bool dependent,bool full,bool material_first) {
  MDB_txn* txn{}; native(mdb_txn_begin(env.handle,nullptr,MDB_RDONLY,&txn));
  mdb_txn_reset(txn); // reusable transaction handle setup; each snapshot is timed
  std::uint64_t sum=0,state=0;
  const Clock start;
  for (std::size_t first=0;first<indices.size();first+=cohort) {
    native(mdb_txn_renew(txn));
    for (unsigned j=0;j<cohort;++j) {
      const auto i=first+j;
      const auto index=dependent?state&(keys.size()-1):indices[i];
      MDB_val found{}; native(peer::get(txn,env.db,keys[index],found));
      const auto value=consume(material(found,material_first),full);
      sum+=value; state=mix(value+i);
    }
    mdb_txn_reset(txn);
  }
  const auto result=stop(start,sum);
  mdb_txn_abort(txn); // release reusable handle, not a snapshot/request operation
  return result;
}
Result engine_write(GroupFixture& fixture,loom::Worker& worker,
    std::span<const std::uint32_t> keys,const std::vector<unsigned>& indices) {
  std::uint64_t sum=0;
  const Clock start;
  for (std::size_t i=0;i<indices.size();++i) {
    const auto prefix=keys[indices[i]];
    const auto value=mix(i+UINT64_C(0x123456789));
    check(bool(fixture.replace(prefix,3,value)),"Engine complete producer replacement");
    sum+=value;
    fixture.store().drain(64); // paid maintenance after request-local Scope closes
    if (!(i&63)) loom::poll(worker,{64,0});
  }
  while (fixture.store().drain(1024)) {}
  loom::poll(worker,{64,0});
  return stop(start,sum);
}
Result lmdb_write(peer::Environment& env,std::span<const std::uint32_t> keys,
    const std::vector<unsigned>& indices,bool material_first) {
  std::uint64_t sum=0;
  const Clock start;
  for (std::size_t i=0;i<indices.size();++i) {
    MDB_txn* txn{}; native(mdb_txn_begin(env.handle,nullptr,0,&txn));
    MDB_val previous{};
    const auto prefix=keys[indices[i]];
    native(peer::get(txn,env.db,prefix,previous));
    peer::Packed128 next;
    std::memcpy(&next,previous.mv_data,sizeof(next));
    const auto value=mix(i+UINT64_C(0x123456789));
    set_word(reinterpret_cast<std::byte*>(&next)+(material_first?0:64),3,value);
    native(peer::put(txn,env.db,prefix,next));
    native(mdb_txn_commit(txn));
    sum+=value;
  }
  return stop(start,sum);
}
void run(loom::Worker& worker,const char* path,std::size_t population,
    std::size_t requests,std::size_t updates,bool sparse,bool material_first) {
  auto rows=records(population,sparse);
  std::vector<std::uint32_t> keys;
  keys.reserve(rows.size());
  for (const auto& row:rows) keys.push_back(row.prefix);
  GroupFixture fixture(worker,rows);
  peer::Environment env;
  // Embedded ObjectStore publication is not durable: use explicit CPU-only peer.
  native(peer::open(env,path,1u<<30,MDB_NOTLS|MDB_NOSYNC|MDB_NOMETASYNC));
  peer::Info info{}; native(peer::inspect(env,info));
  std::fprintf(stderr,"peer=%s flags=%u map=%zu population=%zu group_occupancy=%s; "
      "peer_layout=%s; local synchronous screen; no durability/p99/cold/e2e claim\n",
      info.version,info.flags,info.environment.me_mapsize,population,sparse?"1/16":"16/16",
      material_first?"material-first":"metadata-first");
  load(env,rows,material_first); oracle(fixture,env,rows,material_first);
  stock(worker,"after_setup");
  std::vector<unsigned> indices(requests);
  for (std::size_t i=0;i<requests;++i) indices[i]=unsigned(mix(i+17)&(population-1));
  std::puts("operation,arm,stream,cohort,population,requests,wall_ns_op,owner_ns_op,process_ns_op,checksum");
  for (bool full:{false,true}) for (bool dependent:{false,true}) for (unsigned cohort:{1u,64u}) {
    const auto oracle_sum=expected(rows,indices,dependent,full);
    // Alternate arm order across cases; retain trial-level output externally.
    Result a,b;
    if (cohort==1) {
      a=engine_read(fixture,keys,indices,cohort,dependent,full);
      b=lmdb_read(env,keys,indices,cohort,dependent,full,material_first);
    } else {
      b=lmdb_read(env,keys,indices,cohort,dependent,full,material_first);
      a=engine_read(fixture,keys,indices,cohort,dependent,full);
    }
    check(a.sum==oracle_sum && b.sum==oracle_sum,"timed complete read output oracle");
    report(full?"material64":"project_u64","engine",dependent?"dependent":"independent",
        cohort,population,requests,a);
    report(full?"material64":"project_u64","lmdb",dependent?"dependent":"independent",
        cohort,population,requests,b);
  }
  indices.resize(updates);
  for (std::size_t i=0;i<updates;++i) indices[i]=unsigned(mix(i+37)&(population-1));
  stock(worker,"before_writes");
  const auto a=engine_write(fixture,worker,keys,indices);
  stock(worker,"after_writes_foreground_and_declared_maintenance");
  const auto b=lmdb_write(env,keys,indices,material_first);
  check(a.sum==b.sum,"write output oracle");
  for (std::size_t i=0;i<updates;++i)
    set_word(rows[indices[i]].material.data(),3,mix(i+UINT64_C(0x123456789)));
  oracle(fixture,env,rows,material_first);
  report("move_material64","engine","independent",1,population,updates,a);
  report("move_material64","lmdb","independent",1,population,updates,b);
}
}
int main(int argc,char** argv) {
  check(argc==7 || argc==8,"usage: driver EXISTING_EMPTY_LMDB_DIRECTORY POPULATION READS UPDATES dense|sparse metadata-first|material-first [HELPER_CPU]");
  const auto population=std::strtoull(argv[2],nullptr,10);
  const auto reads=std::strtoull(argv[3],nullptr,10);
  const auto updates=std::strtoull(argv[4],nullptr,10);
  check(population && std::has_single_bit(population) && population<=(1u<<24),"population power-of-two limit");
  check(reads && !(reads%64) && updates,"reads positive multiple64; updates positive");
  check(!std::strcmp(argv[5],"dense") || !std::strcmp(argv[5],"sparse"),"dense|sparse");
  check(!std::strcmp(argv[6],"metadata-first") || !std::strcmp(argv[6],"material-first"),"metadata-first|material-first");
  loom::Worker worker;
  check(bool(loom::adopt(worker)),"Worker adoption");
  const int helper_cpu=argc==8?std::atoi(argv[7]):-1;
  std::fprintf(stderr,"helper_cpu=%d (-1 inherits caller affinity)\n",helper_cpu);
  auto arena=orbital::allocation::open_arena(512u<<20,helper_cpu);
  check(bool(arena),"Arena open");
  check(bool(loom::configure(worker,std::move(*arena),0)),"Worker configure");
  run(worker,argv[1],population,reads,updates,!std::strcmp(argv[5],"sparse"),
      !std::strcmp(argv[6],"material-first"));
  const Clock settlement;
  while (true) {
    const auto p=loom::shutdown(worker);
    check(p.kind()!=orbital::Progress::Kind::failed,"shutdown failed");
    if (p.kind()==orbital::Progress::Kind::done) break;
    loom::poll(worker,{64,0});
  }
  const auto end=stop(settlement,0);
  std::fprintf(stderr,"final_worker_settlement wall_ns=%llu owner_ns=%llu process_ns=%llu\n",
      static_cast<unsigned long long>(end.wall),static_cast<unsigned long long>(end.owner),
      static_cast<unsigned long long>(end.process));
  loom::relinquish(worker);
}
