#include <engine/directory.h>
#include <loom/worker.h>
#include <lmdb.h>
#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
using Store=engine::material::Store;
using Dirent=engine::directory::Dirent;
using Row=std::array<std::uint64_t,8>;
using Clock=std::chrono::steady_clock;
void check(bool b,const char* why) { if (b) return; std::fprintf(stderr,"directory driver: %s\n",why); std::abort(); }
template<class T> T take(orbital::Result<T> r) { check(bool(r),"module refusal"); return std::move(*r); }
void lm(int s) { if (!s) return; std::fprintf(stderr,"LMDB: %s\n",mdb_strerror(s)); std::abort(); }
std::uint64_t mix(std::uint64_t x) { x^=x>>30; x*=UINT64_C(0xbf58476d1ce4e5b9); x^=x>>27; x*=UINT64_C(0x94d049bb133111eb); return x^(x>>31); }
Row row(std::uint32_t key) { Row r; for (unsigned i=0;i<8;++i) r[i]=mix(std::uint64_t(key)*8+i); return r; }
std::uint64_t consume(const void* p) {
  Row r; std::memcpy(r.data(),p,64); return r[0]^r[1]^r[2]^r[3]^r[4]^r[5]^r[6]^r[7];
}
struct Command { std::uint32_t key; std::uint64_t value; unsigned sequence{}; };
struct Plan { std::array<Command,64> commands; unsigned count{}; };
Plan plan(std::span<const Command> input) {
  check(input.size()<=64,"batch capacity"); Plan p;
  for (unsigned i=0;i<input.size();++i) { p.commands[i]=input[i]; p.commands[i].sequence=i; }
  std::sort(p.commands.begin(),p.commands.begin()+input.size(),[](auto& a,auto& b) {
    return a.key<b.key || (a.key==b.key && a.sequence<b.sequence);
  });
  for (unsigned i=0;i<input.size();++i)
    if (i+1==input.size() || p.commands[i].key!=p.commands[i+1].key) p.commands[p.count++]=p.commands[i];
  return p;
}
struct Engine {
  loom::Worker& worker;
  std::pmr::unsynchronized_pool_resource memory{{1024,1024}};
  Store store{memory};
  engine::material::Definition format;
  std::unique_ptr<engine::directory::Directory> directory;
  unsigned stride;
  static orbital::ObjectId data_id(std::uint32_t prefix) { return {2,std::uint64_t(prefix)+1}; }
  Store::Owner finish(std::uint32_t key,orbital::allocation::Allocation& allocation) {
    const auto c=format.control(data_id(key).low);
    const std::array sources{orbital::PartSource{c.id(0),loom::bytes(allocation),{},&allocation}};
    return take(store.capture(c,sources));
  }
  void publish(const Store::Scope& basis,std::span<const Store::Change> changes) {
    Store::Prepared packet(store); check(bool(store.prepare(basis,changes,packet)),"prepare");
    check(bool(store.install(packet)),"install");
  }
  Engine(loom::Worker& w,unsigned n,unsigned step):worker(w),stride(step) {
    const std::array geometry{engine::material::Geometry{64}};
    format=take(engine::material::Definition::native(memory,2,1,geometry));
    directory=take(engine::directory::Directory::open(store,worker,memory,1));
    std::vector<engine::directory::Entry> entries; entries.reserve(n);
    std::vector<Store::Change> changes; changes.reserve(512);
    for (unsigned i=0;i<n;++i) {
      const auto key=i*stride;
      auto allocation=take(loom::allocate(worker,{.bytes=64,.alignment=64}));
      const auto r=row(key); std::memcpy(allocation.address,r.data(),64);
      auto owner=finish(key,allocation);
      Dirent d; d.parts[0]=std::uintptr_t(owner.selection().part(0)->address()); d.representation=1;
      entries.push_back({key,d}); changes.push_back({data_id(key),std::move(owner)});
      if (changes.size()==512 || i+1==n) {
        Store::Scope basis(store); publish(basis,changes); changes.clear();
      }
    }
    auto root=take(directory->build(4,entries));
    { Store::Scope basis(store); const std::array c{Store::Change{directory->root_id(4),std::move(root)}}; publish(basis,c); }
    store.drain();
  }
  // Complete moving-Part producer, not opaque numeric locator replacement.
  // Caller data and directory share the original Scope, Store and ONE packet.
  void write(std::uint32_t key,std::uint64_t value,engine::directory::Work* work=nullptr,
      std::array<double,5>* phases=nullptr) {
    const auto a=phases?Clock::now():Clock::time_point{};
    {
      Store::Scope basis(store);
      auto read=take(directory->read(basis,4));
      const auto* before=read.find(key); check(bool(before),"write missing key");
      // Closed fixed-format producer: admission derived every locator from the
      // actual captured Part, and every write joins data+root in one packet.
      // Same-Scope bytes need no second Object lookup. Outside-clock snapshot
      // qualification checks actual binding/Part association and all64 bytes.
      const auto* old=reinterpret_cast<const std::byte*>(std::uintptr_t(before->parts[0]));
      const auto b=phases?Clock::now():Clock::time_point{};
      auto allocation=take(loom::allocate(worker,{.bytes=64,.alignment=64}));
      std::memcpy(allocation.address,old,64); std::memcpy(allocation.address+24,&value,8);
      auto finished=finish(key,allocation);
      auto d=*before; d.parts[0]=std::uintptr_t(finished.selection().part(0)->address());
      const auto c=phases?Clock::now():Clock::time_point{};
      auto root=take(directory->replace(basis,4,key,d,work));
      const auto e=phases?Clock::now():Clock::time_point{};
      {
        const std::array changes{Store::Change{data_id(key),std::move(finished)},
            Store::Change{directory->root_id(4),std::move(root)}};
        publish(basis,changes);
      }
      const auto f=phases?Clock::now():Clock::time_point{};
      if (phases) {
        (*phases)[0]+=std::chrono::duration<double,std::nano>(b-a).count();
        (*phases)[1]+=std::chrono::duration<double,std::nano>(c-b).count();
        (*phases)[2]+=std::chrono::duration<double,std::nano>(e-c).count();
        (*phases)[3]+=std::chrono::duration<double,std::nano>(f-e).count();
        // cleanup charged below, includes Scope release and finished handles.
        (*phases)[4]-=std::chrono::duration<double,std::nano>(f-a).count();
      }
    }
    store.drain(64); // paid owner-boundary maintenance, after releasing the Scope
    if (phases) (*phases)[4]+=std::chrono::duration<double,std::nano>(Clock::now()-a).count();
  }
  void write_batch(std::span<const Command> offered,engine::directory::Work* work=nullptr,
      std::array<double,5>* phases=nullptr) {
    const auto a=phases?Clock::now():Clock::time_point{};
    const auto final=plan(offered);
    {
      Store::Scope basis(store); auto read=take(directory->read(basis,4));
      std::array<Store::Change,65> changes;
      std::array<engine::directory::Entry,64> replacements;
      std::array<const std::byte*,64> old_bytes;
      for (unsigned i=0;i<final.count;++i) {
        const auto [key,value,sequence]=final.commands[i]; (void)sequence;
        const auto* before=read.find(key); check(before,"batch missing key");
        old_bytes[i]=reinterpret_cast<const std::byte*>(std::uintptr_t(before->parts[0]));
        replacements[i]={key,*before};
      }
      const auto b=phases?Clock::now():Clock::time_point{};
      for (unsigned i=0;i<final.count;++i) {
        const auto [key,value,sequence]=final.commands[i]; (void)sequence;
        auto allocation=take(loom::allocate(worker,{.bytes=64,.alignment=64}));
        std::memcpy(allocation.address,old_bytes[i],64); std::memcpy(allocation.address+24,&value,8);
        auto finished=finish(key,allocation);
        replacements[i].value.parts[0]=std::uintptr_t(finished.selection().part(0)->address());
        changes[i]={data_id(key),std::move(finished)};
      }
      const auto c=phases?Clock::now():Clock::time_point{};
      auto root=take(directory->replace_many(basis,4,std::span(replacements).first(final.count),work));
      const auto d=phases?Clock::now():Clock::time_point{};
      changes[final.count]={directory->root_id(4),std::move(root)};
      publish(basis,std::span(changes).first(final.count+1));
      const auto e=phases?Clock::now():Clock::time_point{};
      if (phases) {
        (*phases)[0]+=std::chrono::duration<double,std::nano>(b-a).count();
        (*phases)[1]+=std::chrono::duration<double,std::nano>(c-b).count();
        (*phases)[2]+=std::chrono::duration<double,std::nano>(d-c).count();
        (*phases)[3]+=std::chrono::duration<double,std::nano>(e-d).count();
        (*phases)[4]-=std::chrono::duration<double,std::nano>(e-a).count();
      }
    }
    store.drain(64*offered.size());
    if (phases) (*phases)[4]+=std::chrono::duration<double,std::nano>(Clock::now()-a).count();
  }
  std::uint64_t read(std::span<const std::uint32_t> queries) {
    std::uint64_t digest=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      Store::Scope scope(store); const auto read=take(directory->read(scope,4));
      for (auto key:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) {
        const auto* d=read.find(key); check(bool(d),"read missing key");
        digest+=consume(reinterpret_cast<const void*>(std::uintptr_t(d->parts[0])));
      }
    }
    return digest;
  }
};
struct Peer {
  MDB_env* env{}; MDB_dbi db{}; std::string path;
  static std::uint32_t key(std::uint32_t k) { return k; }
  Peer(unsigned n,unsigned stride) {
    char name[]="/tmp/sixdb-engine-directory-XXXXXX"; auto* p=mkdtemp(name); check(p,"temporary LMDB directory"); path=p;
    lm(mdb_env_create(&env)); lm(mdb_env_set_mapsize(env,256u<<20));
    lm(mdb_env_open(env,path.c_str(),MDB_NOSYNC|MDB_NOMETASYNC|MDB_NOTLS,0600));
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn)); lm(mdb_dbi_open(txn,nullptr,MDB_INTEGERKEY,&db));
    for (unsigned i=0;i<n;++i) {
      auto encoded=key(i*stride); MDB_val k{4,&encoded}; auto r=row(i*stride); MDB_val v{64,r.data()};
      lm(mdb_put(txn,db,&k,&v,MDB_APPEND));
    }
    lm(mdb_txn_commit(txn));
  }
  ~Peer() { mdb_env_close(env); unlink((path+"/data.mdb").c_str()); unlink((path+"/lock.mdb").c_str()); rmdir(path.c_str()); }
  void write(std::uint32_t prefix,std::uint64_t replacement) {
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn));
    MDB_cursor* cursor; lm(mdb_cursor_open(txn,db,&cursor));
    auto encoded=key(prefix); MDB_val k{4,&encoded},v;
    lm(mdb_cursor_get(cursor,&k,&v,MDB_SET)); check(v.mv_size==64,"LMDB width");
    Row copy; std::memcpy(copy.data(),v.mv_data,64); copy[3]=replacement;
    v={64,nullptr}; lm(mdb_cursor_put(cursor,&k,&v,MDB_CURRENT|MDB_RESERVE)); std::memcpy(v.mv_data,copy.data(),64);
    mdb_cursor_close(cursor);
    lm(mdb_txn_commit(txn));
  }
  void write_batch(std::span<const Command> offered) {
    const auto final=plan(offered);
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn));
    MDB_cursor* cursor; lm(mdb_cursor_open(txn,db,&cursor));
    for (unsigned i=0;i<final.count;++i) {
      auto encoded=key(final.commands[i].key); MDB_val k{4,&encoded},v;
      lm(mdb_cursor_get(cursor,&k,&v,MDB_SET)); Row copy; std::memcpy(copy.data(),v.mv_data,64);
      copy[3]=final.commands[i].value; v={64,nullptr};
      lm(mdb_cursor_put(cursor,&k,&v,MDB_CURRENT|MDB_RESERVE)); std::memcpy(v.mv_data,copy.data(),64);
    }
    mdb_cursor_close(cursor);
    lm(mdb_txn_commit(txn));
  }
  std::uint64_t read(std::span<const std::uint32_t> queries) {
    std::uint64_t digest=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,MDB_RDONLY,&txn));
      for (auto prefix:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) {
        auto encoded=key(prefix); MDB_val k{4,&encoded},v;
        lm(mdb_get(txn,db,&k,&v)); digest+=consume(v.mv_data);
      }
      mdb_txn_abort(txn);
    }
    return digest;
  }
};
void validate_snapshots(Engine& e,Peer& p,std::uint32_t key) {
  Store::Scope old(e.store);
  MDB_txn* old_peer; lm(mdb_txn_begin(p.env,nullptr,MDB_RDONLY,&old_peer));
  const auto original=row(key); const auto replacement=original[3]^12345;
  e.write(key,replacement); p.write(key,replacement); e.store.drain();
  // FIRST discovery is after both publications and maintenance.
  auto check_one=[&](const Store::Scope& scope,MDB_txn* txn,const Row& expected) {
    auto read=take(e.directory->read(scope,4)); const auto* d=read.find(key); check(d,"late key");
    check(!std::memcmp(reinterpret_cast<const void*>(std::uintptr_t(d->parts[0])),expected.data(),64),"snapshot all data bytes");
    const auto bound=take(e.store.bound(scope,Engine::data_id(key)));
    check(d->parts[0]==std::uintptr_t(bound.selection().part(0)->address()),"snapshot locator association");
    auto encoded=Peer::key(key); MDB_val k{4,&encoded},v; lm(mdb_get(txn,p.db,&k,&v));
    check(v.mv_size==64 && !std::memcmp(v.mv_data,expected.data(),64),"peer snapshot all bytes");
  };
  check_one(old,old_peer,original);
  Store::Scope fresh(e.store); MDB_txn* fresh_peer; lm(mdb_txn_begin(p.env,nullptr,MDB_RDONLY,&fresh_peer));
  auto expected=original; expected[3]=replacement; check_one(fresh,fresh_peer,expected);
  mdb_txn_abort(old_peer); mdb_txn_abort(fresh_peer);
  e.write(key,original[3]); p.write(key,original[3]);
}
void validate_state(Engine& e,Peer& p,std::span<const Row> expected) {
  Store::Scope scope(e.store); auto read=take(e.directory->read(scope,4));
  MDB_txn* txn; lm(mdb_txn_begin(p.env,nullptr,MDB_RDONLY,&txn));
  for (unsigned i=0;i<expected.size();++i) {
    const auto prefix=i*e.stride; auto* d=read.find(prefix); check(d,"oracle membership");
    auto* bytes=reinterpret_cast<const void*>(std::uintptr_t(d->parts[0]));
    check(!std::memcmp(bytes,expected[i].data(),64),"independent sequential expected data");
    auto bound=take(e.store.bound(scope,Engine::data_id(prefix)));
    check(d->parts[0]==std::uintptr_t(bound.selection().part(0)->address()),"all locator associations");
    auto encoded=Peer::key(prefix); MDB_val k{4,&encoded},v; lm(mdb_get(txn,p.db,&k,&v));
    check(v.mv_size==64 && !std::memcmp(v.mv_data,expected[i].data(),64),"peer independent full data");
  }
  mdb_txn_abort(txn);
}
void run(loom::Worker& worker,unsigned n,unsigned stride,unsigned writes,unsigned trials,bool phases_only,unsigned batch) {
  Engine engine(worker,n,stride); Peer peer(n,stride);
  validate_snapshots(engine,peer,(n/2)*stride);
  std::vector<std::uint32_t> queries(1u<<18);
  std::vector<Row> model; model.reserve(n);
  for (unsigned i=0;i<n;++i) model.push_back(row(i*stride));
  std::uint64_t expected=0;
  for (unsigned i=0;i<queries.size();++i) { queries[i]=(mix(i+1)%n)*stride; expected+=consume(row(queries[i]).data()); }
  check(engine.read(queries)==expected && peer.read(queries)==expected,"independent input read oracle");
  const auto key=queries[23];
  engine::directory::Work work;
  std::array<double,5> phases{};
  if (batch==1) {
    engine.write(key,mix(23),&work,phases_only?&phases:nullptr); peer.write(key,mix(23));
    model[key/stride][3]=mix(23);
  } else {
    std::array<Command,64> commands;
    for (unsigned i=0;i<batch;++i) commands[i]={queries[i],mix(i)};
    engine.write_batch(std::span(commands).first(batch),&work); peer.write_batch(std::span(commands).first(batch));
    for (unsigned i=0;i<batch;++i) model[queries[i]/stride][3]=mix(i);
  }
  validate_state(engine,peer,model);
  std::printf("ledger,%u,%u,dir_allocations=%llu,dir_body_bytes=%llu,dir_copied_bytes=%llu,dir_captures=%llu,node_proof_bytes=%llu,unchanged_child_retains=%llu\n",n,stride,
      (unsigned long long)work.allocations,(unsigned long long)work.body_bytes,(unsigned long long)work.copied_bytes,
      (unsigned long long)work.captures,(unsigned long long)work.proof_bytes,(unsigned long long)work.child_retains);
  if (phases_only) {
    auto pass=[&](std::array<double,5>* diagnostic) {
      for (unsigned i=0;i<writes;i+=batch) {
        if (batch==1) engine.write(queries[i%queries.size()],mix(i),nullptr,diagnostic);
        else {
          std::array<Command,64> commands; const unsigned count=std::min(batch,writes-i);
          for (unsigned j=0;j<count;++j) commands[j]={queries[(i+j)%queries.size()],mix(i+j)};
          engine.write_batch(std::span(commands).first(count),nullptr,diagnostic);
        }
      }
    };
    pass(nullptr);
    phases={};
    pass(&phases);
    std::printf("diagnostic_phases_ns,%u,%u,basis_validate=%.3f,data_allocate_copy_capture=%.3f,directory_copy_capture=%.3f,prepare_install=%.3f,local_cleanup=%.3f\n",n,stride,
        phases[0]/writes,phases[1]/writes,phases[2]/writes,phases[3]/writes,phases[4]/writes);
    return;
  }
  for (unsigned trial=0;trial<trials;++trial) {
    // Complete per-arm warm pass, then counterbalanced arm order. Read and write
    // requests use identical inputs, output and snapshot/publication lifetime.
    std::array<double,2> read_ns{},write_ns{}; std::array<std::uint64_t,2> digests{};
    for (unsigned order=0;order<2;++order) {
      const unsigned arm=order^(trial&1);
      auto read=[&] { return arm?peer.read(queries):engine.read(queries); };
      (void)read(); const auto a=Clock::now(); digests[arm]=read();
      read_ns[arm]=std::chrono::duration<double,std::nano>(Clock::now()-a).count()/queries.size();
    }
    check(digests[0]==digests[1],"matched useful read output");
    for (unsigned order=0;order<2;++order) {
      const unsigned arm=order^(trial&1);
      const auto a=Clock::now();
      for (unsigned i=0;i<writes;i+=batch) {
        if (batch==1) {
          const auto k=queries[i%queries.size()]; const auto v=mix((std::uint64_t(trial)<<32)+i+1000);
          if (arm) peer.write(k,v); else engine.write(k,v);
        } else {
          std::array<Command,64> commands;
          const unsigned count=std::min(batch,writes-i);
          for (unsigned j=0;j<count;++j) commands[j]={queries[(i+j)%queries.size()],mix((std::uint64_t(trial)<<32)+i+j+1000)};
          const auto input=std::span(commands).first(count);
          if (arm) peer.write_batch(input); else engine.write_batch(input);
        }
      }
      if (!arm) engine.store.drain(); // settle residual debt inside this timed arm
      write_ns[arm]=std::chrono::duration<double,std::nano>(Clock::now()-a).count()/writes;
    }
    check(engine.read(queries)==peer.read(queries),"full output after write trace");
    // Independent sequential application; never use the shared final-intent
    // coalescer to produce expected state for either treatment.
    for (unsigned i=0;i<writes;++i)
      model[queries[i%queries.size()]/stride][3]=mix((std::uint64_t(trial)<<32)+i+1000);
    validate_state(engine,peer,model);
    std::printf("sample,%u,%u,%u,engine_read_ns=%.3f,lmdb_read_ns=%.3f,engine_write_ns=%.3f,lmdb_write_ns=%.3f,digest=%llu\n",n,stride,trial,
        read_ns[0],read_ns[1],write_ns[0],write_ns[1],(unsigned long long)digests[0]);
  }
  engine.store.drain();
  const auto native=orbital::allocation::statistics(*worker.memory);
  std::printf("settled_native_footprint,%u,%u,mapped=%llu,reserved=%llu,metadata=%llu,idle=%llu,active_mapped=%llu\n",n,stride,
      (unsigned long long)native.mapped_bytes,(unsigned long long)native.reserved_bytes,(unsigned long long)native.metadata_bytes,
      (unsigned long long)native.idle_bytes,(unsigned long long)(native.mapped_bytes-native.idle_bytes));
}
}
int main(int argc,char** argv) {
  const unsigned n=argc>1?std::strtoul(argv[1],nullptr,10):8192;
  const unsigned stride=argc>2?std::strtoul(argv[2],nullptr,10):1;
  const unsigned writes=argc>3?std::strtoul(argv[3],nullptr,10):2048;
  const unsigned trials=argc>4?std::strtoul(argv[4],nullptr,10):4;
  const bool phases=argc>5 && std::string(argv[5])=="phases";
  const unsigned batch=argc>6?std::strtoul(argv[6],nullptr,10):1;
  check(n && n<=262144 && stride && std::uint64_t(n)*stride<=UINT32_MAX && writes && trials && batch && batch<=64,"arguments");
  std::printf("context,compiler=%s,lmdb=%s,peer=INTEGERKEY_u32_CURRENT_RESERVE,metadata_pool_max=1024,scope_or_transaction_queries=64,output=full64B_xor8_sum,write_batch=%u,write_denominator=offered_commands,durability=memory_only_lmdb_NOSYNC_NOMETASYNC_NOTLS,phases_instrumented=%u\n",__clang_version__,mdb_version(nullptr,nullptr,nullptr),batch,unsigned(phases));
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt");
  check(bool(loom::configure(worker,take(orbital::allocation::open_arena(512u<<20)),0)),"configure");
  run(worker,n,stride,writes,trials,phases,batch);
  for (;;) { auto p=loom::shutdown(worker); check(p.kind()!=orbital::Progress::Kind::failed,"shutdown");
    if (p.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker);
}
