#include "buffered_producer.h"
#include "../peers/lmdb.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <time.h>

namespace {
using namespace index_spike;
using namespace index_spike::driver;
namespace peer=index_spike::lmdb_peer;
void check(bool b,const char* why) {
  if (b) return;
  std::fprintf(stderr,"buffered driver: %s\n",why); std::abort();
}
void native(int status) { if (status) { std::fprintf(stderr,"LMDB: %s\n",mdb_strerror(status)); std::abort(); } }
std::uint64_t now(clockid_t c) {
  timespec t; check(!clock_gettime(c,&t),"clock"); return std::uint64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
struct Clock { std::uint64_t wall=now(CLOCK_MONOTONIC_RAW),owner=now(CLOCK_THREAD_CPUTIME_ID),process=now(CLOCK_PROCESS_CPUTIME_ID); };
struct Result { std::uint64_t wall,owner,process,checksum; };
Result stop(const Clock& c,std::uint64_t sum) {
  return {now(CLOCK_MONOTONIC_RAW)-c.wall,now(CLOCK_THREAD_CPUTIME_ID)-c.owner,
      now(CLOCK_PROCESS_CPUTIME_ID)-c.process,sum};
}
void report(const char* phase,const char* arm,std::size_t commands,std::size_t requests,
    unsigned batch,unsigned hot,unsigned points,const Result& r) {
  std::printf("%s,%s,%zu,%zu,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%llu\n",phase,arm,commands,requests,batch,hot,points,
      double(r.wall)/requests,double(r.wall)/commands,double(r.owner)/commands,
      double(r.process)/commands,static_cast<unsigned long long>(r.checksum));
}
std::uint64_t mix(std::uint64_t x) {
  x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9); x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb); return x^(x>>31);
}
std::array<Row,64> initial() {
  std::array<Row,64> rows{};
  for (unsigned r=0;r<64;++r) for (unsigned f=0;f<8;++f) rows[r][f]=mix(r*17+f+71);
  return rows;
}
std::uint64_t fold(Sum x) { return std::uint64_t(x)^std::uint64_t(x>>64); }
struct Expected { std::array<Row,64> rows; std::uint64_t digest{}; };
Expected expected(std::span<const Command> commands,unsigned batch,unsigned points,unsigned sum_period) {
  Expected x{initial(),0};
  for (std::size_t i=0;i<commands.size();i+=batch) {
    for (const auto& c:commands.subspan(i,batch)) x.rows[c.row][3]=c.after;
    for (unsigned q=0;q<points;++q) {
      const unsigned row=unsigned(mix(i+q+101)&63);
      // Actual useful consumer folds all8 fields, without forced row serialization.
      std::uint64_t digest=0; for (auto v:x.rows[row]) digest^=v;
      x.digest+=digest;
    }
    if (sum_period && (i/batch+1)%sum_period==0) {
      Sum sum=0; for (const auto& row:x.rows) sum+=row[3]; x.digest+=fold(sum);
    }
  }
  return x;
}
void stock(loom::Worker& worker,const char* phase,std::size_t live,std::size_t peak) {
  const auto s=orbital::allocation::statistics(*worker.memory);
  std::fprintf(stderr,"stock phase=%s mapped=%zu reserved=%zu pool_metadata=%zu idle=%zu preparing=%zu uncertain=%zu "
      "allocations=%zu reuses=%zu control_live=%zu control_peak=%zu\n",phase,s.mapped_bytes,s.reserved_bytes,
      s.metadata_bytes,s.idle_bytes,s.preparing_bytes,s.uncertain_bytes,s.allocations,s.reuses,live,peak);
}
Result engine(loom::Worker& worker,BufferPolicy policy,std::span<const Command> commands,unsigned batch,
    unsigned points,unsigned sum_period,unsigned fold_period,unsigned refusal_period,const Expected& oracle) {
  BufferedProducer p(worker,policy,initial());
  stock(worker,"after_setup",p.metadata_bytes(),p.metadata_peak());
  const Clock clock; std::uint64_t checksum=0,failures=0;
  for (std::size_t i=0;i<commands.size();i+=batch) {
    const auto request=commands.subspan(i,batch);
    if (refusal_period && (i/batch+1)%refusal_period==0) {
      check(!p.update(request,Fault::prepare),"injected actual prepare refusal not reached"); ++failures;
    }
    check(bool(p.update(request)),"valid actual producer update refused");
    if (points || (sum_period && (i/batch+1)%sum_period==0)) {
      Store::Scope scope(p.store());
      for (unsigned q=0;q<points;++q) {
        const auto row=p.row(scope,unsigned(mix(i+q+101)&63));
        std::uint64_t digest=0; for (auto v:row) digest^=v; checksum+=digest;
      }
      if (sum_period && (i/batch+1)%sum_period==0) checksum+=fold(p.sum(scope));
    }
    if (fold_period && (i/batch+1)%fold_period==0) {
      check(bool(p.compact_row(unsigned((i/batch)&63))),"partial port refused");
      check(bool(p.compact_all()),"full fold refused");
    }
    // Declared same-owner service. Whole-history interval also includes final
    // catalog/fixture destruction, Pool returns and actual shutdown below.
    p.store().drain(32);
    if ((i/batch&63)==63) loom::poll(worker,{64,0});
  }
  p.store().drain();
  const auto result=stop(clock,checksum);
  check(checksum==oracle.digest,"timed Engine output differs from independent original-row replay");
  { Store::Scope final(p.store());
    for (unsigned r=0;r<64;++r) check(p.row(final,r)==oracle.rows[r],"Engine final all-field oracle"); }
  const auto c=p.counters();
  std::fprintf(stderr,"producer publications=%llu captures=%llu directory_changes=%llu events=%llu log_copy_bytes=%llu "
      "rotations=%llu folds=%llu refusals=%llu offered_abort_retries=%llu\n",
      (unsigned long long)c.publications,(unsigned long long)c.captures,(unsigned long long)c.directory_changes,
      (unsigned long long)c.events,(unsigned long long)c.log_copy_bytes,(unsigned long long)c.rotations,
      (unsigned long long)c.folds,(unsigned long long)c.refusals,(unsigned long long)failures);
  stock(worker,"after_history_before_fixture_destruction",p.metadata_bytes(),p.metadata_peak());
  return result; // p and catalog destruction occurs before whole-history stop
}
Row native_row(MDB_txn* txn,MDB_dbi db,unsigned row) {
  MDB_val value; native(peer::get(txn,db,row,value)); check(value.mv_size==64,"native64 shape");
  Row out; std::memcpy(out.data(),value.mv_data,64); return out;
}
void put_row(MDB_txn* txn,MDB_dbi db,MDB_cursor* cursor,unsigned key,const Row& row) {
  auto encoded=peer::key(key); auto k=peer::key_value(encoded);
  MDB_val value{64,const_cast<std::uint64_t*>(row.data())};
  native(cursor?mdb_cursor_put(cursor,&k,&value,MDB_CURRENT):mdb_put(txn,db,&k,&value,0));
}
void write_native(peer::Environment& env,std::span<const Command> commands,bool cursor_policy,bool abort) {
  // Same private coalescing permission as Engine. Command order is retained;
  // only the final afterimage for each distinct changed row is published.
  std::array<std::uint64_t,64> after{}; std::uint64_t touched=0;
  for (const auto& c:commands) { after[c.row]=c.after; touched|=std::uint64_t(1)<<c.row; }
  MDB_txn* txn; native(mdb_txn_begin(env.handle,nullptr,0,&txn));
  MDB_cursor* cursor=nullptr; if (cursor_policy) native(mdb_cursor_open(txn,env.db,&cursor));
  while (touched) {
    const unsigned r=std::countr_zero(touched); touched&=touched-1;
    Row row;
    if (cursor) {
      auto encoded=peer::key(r); auto key=peer::key_value(encoded); MDB_val found;
      native(mdb_cursor_get(cursor,&key,&found,MDB_SET_KEY)); check(found.mv_size==64,"cursor shape");
      std::memcpy(row.data(),found.mv_data,64);
    } else row=native_row(txn,env.db,r);
    row[3]=after[r]; put_row(txn,env.db,cursor,r,row);
  }
  // A write cursor is freed by commit/abort, never reused or closed afterward.
  if (abort) mdb_txn_abort(txn); else native(mdb_txn_commit(txn));
}
Result lmdb(const char* path,bool cursor,std::span<const Command> commands,unsigned batch,
    unsigned points,unsigned sum_period,unsigned refusal_period,const Expected& oracle) {
  peer::Environment env;
  native(peer::open(env,path,64u<<20,MDB_NOTLS|MDB_NOSYNC|MDB_NOMETASYNC));
  MDB_txn* setup; native(mdb_txn_begin(env.handle,nullptr,0,&setup));
  const auto rows=initial();
  for (unsigned r=0;r<64;++r) put_row(setup,env.db,nullptr,r,rows[r]);
  native(mdb_txn_commit(setup));
  MDB_txn* reader; native(mdb_txn_begin(env.handle,nullptr,MDB_RDONLY,&reader)); mdb_txn_reset(reader);
  MDB_cursor* sum_cursor=nullptr;
  if (sum_period) {
    native(mdb_txn_renew(reader)); native(mdb_cursor_open(reader,env.db,&sum_cursor)); mdb_txn_reset(reader);
  }
  const Clock clock; std::uint64_t checksum=0;
  for (std::size_t i=0;i<commands.size();i+=batch) {
    const auto request=commands.subspan(i,batch);
    if (refusal_period && (i/batch+1)%refusal_period==0) write_native(env,request,cursor,true);
    write_native(env,request,cursor,false);
    if (points || (sum_period && (i/batch+1)%sum_period==0)) {
      native(mdb_txn_renew(reader));
      for (unsigned q=0;q<points;++q) {
        const auto row=native_row(reader,env.db,unsigned(mix(i+q+101)&63));
        std::uint64_t digest=0; for (auto v:row) digest^=v; checksum+=digest;
      }
      if (sum_period && (i/batch+1)%sum_period==0) {
        native(mdb_cursor_renew(reader,sum_cursor));
        Sum sum=0; MDB_val key{},value{};
        int status=mdb_cursor_get(sum_cursor,&key,&value,MDB_FIRST); unsigned n=0;
        while (!status) {
          check(value.mv_size==64,"native sum shape");
          std::uint64_t word; std::memcpy(&word,static_cast<const std::byte*>(value.mv_data)+24,8);
          sum+=word; ++n; status=mdb_cursor_get(sum_cursor,&key,&value,MDB_NEXT);
        }
        check(status==MDB_NOTFOUND && n==64,"native sum complete numeric domain"); checksum+=fold(sum);
      }
      mdb_txn_reset(reader);
    }
  }
  const auto result=stop(clock,checksum);
  check(checksum==oracle.digest,"native output differs from independent original-row replay");
  native(mdb_txn_renew(reader));
  for (unsigned r=0;r<64;++r) check(native_row(reader,env.db,r)==oracle.rows[r],"native final all-field oracle");
  if (sum_cursor) mdb_cursor_close(sum_cursor);
  mdb_txn_abort(reader);
  peer::Info info; native(peer::inspect(env,info));
  MDB_txn* inspect; native(mdb_txn_begin(env.handle,nullptr,MDB_RDONLY,&inspect));
  unsigned flags{}; native(mdb_dbi_flags(inspect,env.db,&flags)); mdb_txn_abort(inspect);
  std::fprintf(stderr,"native=%s env_flags=%u db_flags=%u key=BE4 value=64 durability=CPU-only\n",info.version,info.flags,flags);
  return result;
}
}
int main(int argc,char** argv) {
  check(argc==11,"usage: buffered_driver plain|indexed|eager|lmdb-get|lmdb-cursor EMPTY_LMDB_DIR COMMANDS BATCH HOT POINTS SUM_PERIOD FOLD_PERIOD REFUSAL_PERIOD HELPER_CPU");
  const char* arm=argv[1]; const auto commands=std::strtoull(argv[3],nullptr,10);
  const unsigned batch=std::strtoul(argv[4],nullptr,10),hot=std::strtoul(argv[5],nullptr,10),points=std::strtoul(argv[6],nullptr,10),
      sum_period=std::strtoul(argv[7],nullptr,10),fold_period=std::strtoul(argv[8],nullptr,10),refusal_period=std::strtoul(argv[9],nullptr,10);
  check(commands && (batch==1 || batch==64) && !(commands%batch) && (hot==2 || hot==16 || hot==64),"finite command/batch/locality domain");
  const bool peer=!std::strcmp(arm,"lmdb-get") || !std::strcmp(arm,"lmdb-cursor");
  check(peer || !std::strcmp(arm,"plain") || !std::strcmp(arm,"indexed") || !std::strcmp(arm,"eager"),"unknown arm");
  std::vector<Command> input(commands);
  for (std::size_t i=0;i<commands;++i) input[i]={unsigned(mix(i+37)&(hot-1)),mix(i+UINT64_C(0x123456789))};
  const auto oracle=expected(input,batch,points,sum_period); // outside ALL clocks
  const Clock whole; Result foreground;
  if (peer) foreground=lmdb(argv[2],!std::strcmp(arm,"lmdb-cursor"),input,batch,points,sum_period,refusal_period,oracle);
  else {
    loom::Worker worker; check(bool(loom::adopt(worker)),"Worker adopt");
    auto arena=orbital::allocation::open_arena(256u<<20,std::atoi(argv[10])); check(bool(arena),"Arena open");
    check(bool(loom::configure(worker,std::move(*arena),0)),"Worker configure");
    const auto policy=!std::strcmp(arm,"plain")?BufferPolicy::plain:
        !std::strcmp(arm,"indexed")?BufferPolicy::indexed:BufferPolicy::eager;
    foreground=engine(worker,policy,input,batch,points,sum_period,fold_period,refusal_period,oracle);
    stock(worker,"after_fixture_destruction",0,0);
    while (true) {
      const auto p=loom::shutdown(worker); check(p.kind()!=orbital::Progress::Kind::failed,"shutdown failed");
      if (p.kind()==orbital::Progress::Kind::done) break;
      loom::poll(worker,{64,0});
    }
    loom::relinquish(worker);
  }
  const auto total=stop(whole,foreground.checksum);
  std::puts("phase,arm,commands,requests,batch,hot,points,wall_ns_request,wall_ns_command,owner_ns_command,process_ns_command,checksum");
  report("foreground_and_declared_maintenance",arm,commands,commands/batch,batch,hot,points,foreground);
  report("whole_history_setup_oracle_teardown_settlement",arm,commands,commands/batch,batch,hot,points,total);
}
