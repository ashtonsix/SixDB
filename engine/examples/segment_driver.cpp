#include <engine/directory.h>
#include <loom/worker.h>
#include <lmdb.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
using namespace engine;
using Store=material::Store;
using Row=std::array<std::uint64_t,8>;
using Clock=std::chrono::steady_clock;
using Wide=unsigned __int128;
void check(bool b,const char* why) { if (b) return; std::fprintf(stderr,"Segment driver: %s\n",why); std::abort(); }
template<class T> T take(orbital::Result<T> r) { check(bool(r),"module refusal"); return std::move(*r); }
void lm(int s) { if (!s) return; std::fprintf(stderr,"LMDB: %s\n",mdb_strerror(s)); std::abort(); }
std::uint64_t mix(std::uint64_t x) { x^=x>>30; x*=UINT64_C(0xbf58476d1ce4e5b9); x^=x>>27; x*=UINT64_C(0x94d049bb133111eb); return x^(x>>31); }
std::size_t aligned(std::size_t x) { return (x+63)&~std::size_t(63); }
template<class T> T load(const std::byte* p) { T x; std::memcpy(&x,p,sizeof(x)); return x; }
std::uint64_t digest(const Row& r) { return r[0]^r[1]^r[2]^r[3]^r[4]^r[5]^r[6]^r[7]; }
struct Field {
  std::uint64_t minimum{},maximum{},sum_low{},sum_high{},not_null{};
  void add(std::uint64_t x) {
    if (!not_null || x<minimum) minimum=x;
    if (!not_null || x>maximum) maximum=x;
    const auto old=sum_low; sum_low+=x; sum_high+=sum_low<old; ++not_null;
  }
  friend bool operator==(const Field&,const Field&)=default;
};
struct Cell {
  std::uint16_t position{}; std::byte reserved[6]{};
  std::uint64_t records{},first{},end{};
  std::array<Field,8> fields{};
};
static_assert(sizeof(Field)==40 && sizeof(Cell)==352);
struct Command { unsigned row; std::uint64_t value; unsigned sequence{}; };
struct Geometry {
  unsigned segments,rows;
  std::uint64_t physical(unsigned index) const { return (std::uint64_t((index/rows)*17)<<16)|(index%rows); }
  unsigned index(std::uint64_t physical) const { return unsigned(physical>>16)/17*rows+unsigned(physical&65535); }
  std::uint32_t prefix(unsigned index,unsigned layer) const { return layer?physical(index)>>(48-8*layer):0; }
  std::uint16_t position(unsigned index,unsigned layer) const { return physical(index)>>(32-8*layer); }
  unsigned size() const { return segments*rows; }
};
Row initial(unsigned index) {
  Row r; r[0]=index/3; for (unsigned f=1;f<8;++f) r[f]=mix(std::uint64_t(index)*8+f); return r;
}
// Snapshot-correct afterimages are ordinary Orbital Objects, not a second MVCC
// mechanism. One fixed576B correction page covers64 physical slots, with a dirty
// mask and field3 aftervalues. Unchanged base/directory facts require no edit.
struct Producer {
  Geometry geometry;
  loom::Worker& worker;
  std::pmr::unsynchronized_pool_resource memory{{128,2048}};
  Store store{memory};
  std::unique_ptr<directory::Directory> directory;
  material::Definition corrections;
  std::array<std::vector<directory::Entry>,5> entries;
  std::array<std::uint64_t,5> root_versions{};
  std::uint64_t publications{},captures{},directory_changes{};
  static orbital::ObjectId segment_id(unsigned layer,std::uint32_t prefix) { return {2,(std::uint64_t(layer)<<32)|prefix}; }
  static orbital::ObjectId correction_id(std::uint64_t key) { return {3,(key>>6)+1}; }
  Producer(loom::Worker& w,Geometry g):geometry(g),worker(w) {
    directory=take(directory::Directory::open(store,worker,memory,1));
    const std::array cg{material::Geometry{576}};
    corrections=take(material::Definition::native(memory,3,300,cg));
    std::vector<Store::Change> changes;
    auto finish=[&](unsigned layer,std::uint32_t prefix,const std::byte* header,
        std::span<const std::byte> body) {
      const auto id=segment_id(layer,prefix);
      const std::array sizes{material::Geometry{64},material::Geometry{aligned(body.size())}};
      auto definition=take(material::Definition::native(memory,2,100+layer,sizes));
      auto h=take(loom::allocate(worker,{.bytes=64,.alignment=64})); std::memcpy(h.address,header,64);
      auto b=take(loom::allocate(worker,{.bytes=aligned(body.size()),.alignment=64}));
      std::memset(b.address,0,b.size); std::memcpy(b.address,body.data(),body.size());
      const auto c=definition.control(id.low);
      const std::array src{orbital::PartSource{c.id(0),loom::bytes(h),{},&h},orbital::PartSource{c.id(1),loom::bytes(b),{},&b}};
      auto owner=take(store.capture(c,src));
      directory::Dirent d; d.representation=100+layer;
      d.parts[0]=std::uintptr_t(owner.selection().part(0)->address()); d.parts[1]=std::uintptr_t(owner.selection().part(1)->address());
      entries[layer].push_back({prefix,d}); changes.push_back({id,std::move(owner)});
    };
    for (unsigned s=0;s<g.segments;++s) {
      std::vector<Row> data; data.reserve(g.rows);
      for (unsigned i=0;i<g.rows;++i) data.push_back(initial(s*g.rows+i));
      std::array<std::byte,64> h{}; std::memcpy(h.data(),&g.rows,4);
      finish(4,s*17,h.data(),std::as_bytes(std::span(data)));
    }
    for (unsigned layer=0;layer<4;++layer) {
      std::uint32_t prefix=0; std::vector<Cell> segment;
      auto flush=[&] {
        if (segment.empty()) return;
        std::array<std::byte,64> h{}; const auto n=std::uint32_t(segment.size()); std::memcpy(h.data(),&n,4);
        finish(layer,prefix,h.data(),std::as_bytes(std::span(segment))); segment.clear();
      };
      for (unsigned i=0;i<g.size();++i) {
        const auto p=g.prefix(i,layer); const auto position=g.position(i,layer);
        if (!segment.empty() && p!=prefix) flush(); prefix=p;
        if (segment.empty() || segment.back().position!=position) {
          Cell c; c.position=position; c.first=i; segment.push_back(c);
        }
        auto& cell=segment.back(); cell.end=i+1; ++cell.records;
        const auto r=initial(i); for (unsigned f=0;f<8;++f) cell.fields[f].add(r[f]);
      }
      flush();
    }
    for (unsigned layer=0;layer<5;++layer) {
      auto root=take(directory->build(layer,entries[layer])); changes.push_back({directory->root_id(layer),std::move(root)});
    }
    // Fresh constructor has not escaped. Ordinary later updates are never split.
    for (std::size_t first=0;first<changes.size();first+=512) {
      Store::Scope basis(store); publish(basis,std::span(changes).subspan(first,std::min<std::size_t>(512,changes.size()-first)));
    }
    changes.clear(); store.drain(); publications=0;
    Store::Scope basis(store);
    for (unsigned layer=0;layer<5;++layer)
      root_versions[layer]=take(store.bound(basis,directory->root_id(layer))).binding().version;
  }
  void publish(const Store::Scope& basis,std::span<const Store::Change> changes) {
    Store::Prepared packet(store); check(bool(store.prepare(basis,changes,packet)),"prepare");
    check(bool(store.install(packet)),"install"); ++publications;
  }
  struct Reader {
    Producer& owner; const Store::Scope& scope; directory::Read base;
    struct Cached { std::uint64_t id{UINT64_MAX}; const std::byte* page{}; };
    std::array<Cached,8> pages{};
    unsigned replacement{};
    Reader(Producer& p,const Store::Scope& s):owner(p),scope(s),base(take(p.directory->read(s,4))) {}
    const std::byte* page(std::uint64_t physical) {
      const auto id=correction_id(physical);
      for (auto c:pages) if (c.id==id.low) return c.page;
      auto selected=scope.bind(id); const auto* bytes=selected?selected.part(0)->address():nullptr;
      pages[replacement++&7]={id.low,bytes}; return bytes;
    }
    Row row(unsigned index) {
      const auto physical=owner.geometry.physical(index); const auto slot=unsigned(physical&65535);
      const auto* d=base.find(physical>>16); check(d,"base membership");
      const auto* data=reinterpret_cast<const std::byte*>(std::uintptr_t(d->parts[1]));
      Row r; std::memcpy(r.data(),data+slot*64,64);
      if (const auto* p=page(physical);p && (load<std::uint64_t>(p)&(UINT64_C(1)<<(slot&63))))
        r[3]=load<std::uint64_t>(p+64+(slot&63)*8);
      return r;
    }
    const Cell* cell(unsigned layer,std::uint32_t prefix,std::uint16_t position) {
      check(layer<4,"summary layer");
      auto read=take(owner.directory->read(scope,layer)); const auto* d=read.find(prefix); check(d,"summary segment");
      auto* header=reinterpret_cast<const std::byte*>(std::uintptr_t(d->parts[0]));
      auto* data=reinterpret_cast<const Cell*>(std::uintptr_t(d->parts[1]));
      const auto count=load<std::uint32_t>(header);
      for (unsigned i=0;i<count;++i) if (data[i].position==position) return data+i;
      check(false,"summary cell"); return nullptr;
    }
    Field summary(unsigned layer,std::uint32_t prefix,std::uint16_t position) {
      const auto* selected=cell(layer,prefix,position);
        // Exact min/max under afterimages requires examining the affected values;
        // this first caller deliberately recomputes field3, leaving seven
        // immutable fields in the selected native summary. No stale aggregate.
        Field corrected;
        for (auto r=selected->first;r<selected->end;++r) corrected.add(row(r)[3]);
        return corrected;
    }
  };
  void update(std::span<const Command> offered) {
    check(!offered.empty() && offered.size()<=64,"command batch");
    std::array<Command,64> ordered;
    for (unsigned i=0;i<offered.size();++i) {
      check(offered[i].row<geometry.size(),"command row"); ordered[i]=offered[i]; ordered[i].sequence=i;
    }
    std::sort(ordered.begin(),ordered.begin()+offered.size(),[&](auto& a,auto& b) {
      const auto x=geometry.physical(a.row)>>6,y=geometry.physical(b.row)>>6;
      return x<y || (x==y && a.sequence<b.sequence);
    });
    {
      Store::Scope basis(store); std::array<Store::Change,64> changes; unsigned count=0;
      for (std::size_t first=0;first<offered.size();) {
        const auto id=correction_id(geometry.physical(ordered[first].row));
        auto last=first+1;
        while (last<offered.size() && correction_id(geometry.physical(ordered[last].row)).low==id.low) ++last;
        auto allocation=take(loom::allocate(worker,{.bytes=576,.alignment=64}));
        const auto old=basis.bind(id);
        if (old) std::memcpy(allocation.address,old.part(0)->address(),576); else std::memset(allocation.address,0,576);
        auto mask=load<std::uint64_t>(allocation.address);
        for (auto i=first;i<last;++i) {
          const auto slot=geometry.physical(ordered[i].row)&63;
          mask|=UINT64_C(1)<<slot; std::memcpy(allocation.address+64+slot*8,&ordered[i].value,8);
        }
        std::memcpy(allocation.address,&mask,8);
        const auto c=corrections.control(id.low);
        const std::array source{orbital::PartSource{c.id(0),loom::bytes(allocation),{},&allocation}};
        changes[count++]={id,take(store.capture(c,source))}; ++captures; first=last;
      }
      publish(basis,std::span(changes).first(count));
    }
    store.drain(offered.size()*64);
  }
  std::uint64_t read(std::span<const unsigned> queries) {
    std::uint64_t sum=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      Store::Scope scope(store); Reader reader(*this,scope);
      for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=digest(reader.row(i));
    }
    return sum;
  }
};
struct Peer {
  Geometry geometry; MDB_env* env{}; MDB_dbi db{}; std::string path;
  Peer(Geometry g):geometry(g) {
    char name[]="/tmp/sixdb-segment-driver-XXXXXX"; auto* p=mkdtemp(name); check(p,"temp directory"); path=p;
    lm(mdb_env_create(&env)); lm(mdb_env_set_mapsize(env,512u<<20));
    lm(mdb_env_open(env,path.c_str(),MDB_NOSYNC|MDB_NOMETASYNC|MDB_NOTLS,0600));
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn)); lm(mdb_dbi_open(txn,nullptr,MDB_INTEGERKEY,&db));
    for (unsigned i=0;i<g.size();++i) { auto key=g.physical(i); auto r=initial(i); MDB_val k{8,&key},v{64,r.data()}; lm(mdb_put(txn,db,&k,&v,MDB_APPEND)); }
    lm(mdb_txn_commit(txn));
  }
  ~Peer() { mdb_env_close(env); unlink((path+"/data.mdb").c_str()); unlink((path+"/lock.mdb").c_str()); rmdir(path.c_str()); }
  Row row(MDB_txn* txn,unsigned index) {
    auto key=geometry.physical(index); MDB_val k{8,&key},v; lm(mdb_get(txn,db,&k,&v)); check(v.mv_size==64,"peer row bytes");
    Row r; std::memcpy(r.data(),v.mv_data,64); return r;
  }
  void update(std::span<const Command> commands) {
    std::array<Command,64> ordered;
    check(commands.size()<=64,"peer command count");
    for (unsigned i=0;i<commands.size();++i) { check(commands[i].row<geometry.size(),"peer row"); ordered[i]=commands[i]; ordered[i].sequence=i; }
    std::sort(ordered.begin(),ordered.begin()+commands.size(),[](auto& a,auto& b) {
      return a.row<b.row || (a.row==b.row && a.sequence<b.sequence);
    });
    // Native covering values, final duplicate intent only, one cursor/transaction.
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn)); MDB_cursor* cursor; lm(mdb_cursor_open(txn,db,&cursor));
    for (unsigned i=0;i<commands.size();++i) {
      if (i+1<commands.size() && ordered[i].row==ordered[i+1].row) continue;
      auto command=ordered[i];
      auto key=geometry.physical(command.row); MDB_val k{8,&key},v; lm(mdb_cursor_get(cursor,&k,&v,MDB_SET));
      Row r; std::memcpy(r.data(),v.mv_data,64); r[3]=command.value;
      v={64,nullptr}; lm(mdb_cursor_put(cursor,&k,&v,MDB_CURRENT|MDB_RESERVE)); std::memcpy(v.mv_data,r.data(),64);
    }
    mdb_cursor_close(cursor); lm(mdb_txn_commit(txn));
  }
  std::uint64_t read(std::span<const unsigned> queries) {
    std::uint64_t sum=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,MDB_RDONLY,&txn));
      for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=digest(row(txn,i));
      mdb_txn_abort(txn);
    }
    return sum;
  }
};
void verify(Producer& p,Peer& peer,const Store::Scope& scope,MDB_txn* txn,std::span<const Row> model) {
  Producer::Reader reader(p,scope);
  for (unsigned layer=0;layer<5;++layer)
    check(take(p.store.bound(scope,p.directory->root_id(layer))).binding().version==p.root_versions[layer],"unchanged directory version");
  for (unsigned i=0;i<model.size();++i) check(reader.row(i)==model[i] && peer.row(txn,i)==model[i],"independent full row oracle");
  for (unsigned layer=0;layer<4;++layer) {
    for (unsigned first=0;first<model.size();) {
      const auto prefix=p.geometry.prefix(first,layer); const auto position=p.geometry.position(first,layer);
      auto last=first+1;
      while (last<model.size() && p.geometry.prefix(last,layer)==prefix && p.geometry.position(last,layer)==position) ++last;
      const auto* native=reader.cell(layer,prefix,position);
      check(native->first==first && native->end==last && native->records==last-first,"summary membership/count");
      // Independent wide arithmetic, never the producer's carry/min-max reducer.
      for (unsigned f=0;f<8;++f) {
        std::uint64_t lo=UINT64_MAX,hi=0; Wide sum=0;
        for (auto i=first;i<last;++i) { auto v=initial(i)[f]; lo=std::min(lo,v); hi=std::max(hi,v); sum+=v; }
        const Field expected{lo,hi,std::uint64_t(sum),std::uint64_t(sum>>64),last-first};
        check(native->fields[f]==expected,"native summary all fields");
      }
      std::uint64_t lo=UINT64_MAX,hi=0; Wide sum=0;
      for (auto i=first;i<last;++i) { const auto v=model[i][3]; lo=std::min(lo,v); hi=std::max(hi,v); sum+=v; }
      const Field corrected{lo,hi,std::uint64_t(sum),std::uint64_t(sum>>64),last-first};
      check(reader.summary(layer,prefix,position)==corrected,"all-layer corrected min/max/SUM128/not-null");
      first=last;
    }
  }
}
void run(loom::Worker& worker,Geometry geometry,unsigned hot,unsigned batch,unsigned writes,unsigned trials) {
  Producer p(worker,geometry); Peer peer(geometry);
  std::vector<Row> model; model.reserve(geometry.size()); for (unsigned i=0;i<geometry.size();++i) model.push_back(initial(i));
  {
    Store::Scope old(p.store); MDB_txn* old_peer; lm(mdb_txn_begin(peer.env,nullptr,MDB_RDONLY,&old_peer));
    const auto initial_model=model;
    std::array<Command,64> commands;
    for (unsigned i=0;i<64;++i) commands[i]={unsigned(mix(i)%geometry.size()),mix(i+101)};
    p.update(commands); peer.update(commands); for (auto c:commands) model[c.row][3]=c.value;
    p.store.drain();
    // Neither old Reader nor peer performed any lookup before publication.
    verify(p,peer,old,old_peer,initial_model);
    Store::Scope fresh(p.store); MDB_txn* fresh_peer; lm(mdb_txn_begin(peer.env,nullptr,MDB_RDONLY,&fresh_peer));
    verify(p,peer,fresh,fresh_peer,model); mdb_txn_abort(old_peer); mdb_txn_abort(fresh_peer);
  }
  std::vector<unsigned> queries(1u<<17);
  for (unsigned i=0;i<queries.size();++i) queries[i]=mix(i+1)%hot;
  check(p.read(queries)==peer.read(queries),"pre-screen output");
  for (unsigned trial=0;trial<trials;++trial) {
    std::array<double,2> read_ns{},write_ns{}; std::array<std::uint64_t,2> output;
    for (unsigned order=0;order<2;++order) {
      const auto arm=order^(trial&1); auto read=[&] { return arm?peer.read(queries):p.read(queries); };
      (void)read(); const auto start=Clock::now(); output[arm]=read(); read_ns[arm]=std::chrono::duration<double,std::nano>(Clock::now()-start).count()/queries.size();
    }
    check(output[0]==output[1],"matched output");
    for (unsigned order=0;order<2;++order) {
      const auto arm=order^(trial&1); const auto start=Clock::now();
      for (unsigned first=0;first<writes;first+=batch) {
        std::array<Command,64> commands; const auto count=std::min(batch,writes-first);
        for (unsigned i=0;i<count;++i) commands[i]={queries[(first+i)%queries.size()],mix((std::uint64_t(trial)<<32)+first+i+1001)};
        const auto input=std::span(commands).first(count); if (arm) peer.update(input); else p.update(input);
      }
      if (!arm) p.store.drain();
      write_ns[arm]=std::chrono::duration<double,std::nano>(Clock::now()-start).count()/writes;
    }
    for (unsigned i=0;i<writes;++i) model[queries[i%queries.size()]][3]=mix((std::uint64_t(trial)<<32)+i+1001);
    { Store::Scope scope(p.store); MDB_txn* txn; lm(mdb_txn_begin(peer.env,nullptr,MDB_RDONLY,&txn)); verify(p,peer,scope,txn,model); mdb_txn_abort(txn); }
    check(p.directory_changes==0,"stable directory facts edited");
    std::printf("sample,segments=%u,rows_per_segment=%u,hot=%u,batch=%u,trial=%u,engine_read_ns=%.3f,lmdb_read_ns=%.3f,engine_write_ns=%.3f,lmdb_write_ns=%.3f,directory_changes=%llu,captures=%llu,publications=%llu\n",geometry.segments,geometry.rows,hot,batch,trial,read_ns[0],read_ns[1],write_ns[0],write_ns[1],(unsigned long long)p.directory_changes,(unsigned long long)p.captures,(unsigned long long)p.publications);
  }
}
}
int main(int argc,char** argv) {
  Geometry g{argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):2,argc>2?unsigned(std::strtoul(argv[2],nullptr,10)):1024};
  const unsigned hot=argc>3?std::strtoul(argv[3],nullptr,10):64,batch=argc>4?std::strtoul(argv[4],nullptr,10):64;
  const unsigned writes=argc>5?std::strtoul(argv[5],nullptr,10):4096,trials=argc>6?std::strtoul(argv[6],nullptr,10):4;
  check(g.segments && g.segments<=80 && g.rows && g.rows<=65536 && std::uint64_t(g.size())<=524288 && hot && hot<=g.size() && batch && batch<=64 && writes && trials,"arguments");
  std::printf("context,multirow_native_base_plus_snapshot_COW_afterimage_pages,field=3,physical_points,all5layers_exist,logical_keys_duplicate_runs3,scope_or_transaction=64,peer=INTEGERKEY_u64_CURRENT_RESERVE,memory_only_NOSYNC_NOMETASYNC_NOTLS,compiler=%s\n",__clang_version__);
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt"); check(bool(loom::configure(worker,take(orbital::allocation::open_arena(512u<<20)),0)),"configure");
  run(worker,g,hot,batch,writes,trials);
  for (;;) { auto s=loom::shutdown(worker); check(s.kind()!=orbital::Progress::Kind::failed,"shutdown"); if (s.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker);
}
