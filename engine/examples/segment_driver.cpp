#include <engine/directory.h>
#include <engine/directory_hash.h>
#include <ikea/hashtable/lookup.h>
#include <ikea/boundlist/search.h>
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
struct Counted final : std::pmr::memory_resource {
  std::pmr::memory_resource& upstream;
  std::size_t live{},peak{},allocations{},frees{};
  explicit Counted(std::pmr::memory_resource& r):upstream(r) {}
  void* do_allocate(std::size_t n,std::size_t a) override {
    auto* p=upstream.allocate(n,a); live+=n; peak=std::max(peak,live); ++allocations; return p;
  }
  void do_deallocate(void* p,std::size_t n,std::size_t a) override { live-=n; ++frees; upstream.deallocate(p,n,a); }
  bool do_is_equal(const std::pmr::memory_resource& x) const noexcept override { return this==&x; }
};
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
  Counted backing{*std::pmr::new_delete_resource()};
  std::pmr::unsynchronized_pool_resource pool{{128,2048},&backing};
  Counted memory{pool};
  Store store{memory};
  std::unique_ptr<directory::Directory> directory;
  material::Definition corrections;
  std::array<std::unique_ptr<directory::HashLayer>,5> hashes;
  const unsigned shared;
  ikea::hashtable::lookup_selector equality_selector;
  std::array<std::uint64_t,5> root_versions{};
  std::uint64_t publications{},captures{},directory_changes{};
  static orbital::ObjectId segment_id(unsigned layer,std::uint32_t prefix) { return {2,(std::uint64_t(layer)<<32)|prefix}; }
  static orbital::ObjectId correction_id(std::uint64_t key) { return {3,(key>>6)+1}; }
  Producer(loom::Worker& w,Geometry g,unsigned j):geometry(g),worker(w),shared(j) {
    directory=take(directory::Directory::open(store,worker,memory,1,directory::KeyShape{8,shared}));
    const std::array cg{material::Geometry{576}};
    corrections=take(material::Definition::native(memory,3,300,cg));
    std::vector<Store::Change> changes;
    std::array<std::vector<directory::Entry>,5> entries;
    std::vector<std::array<std::byte,16>> occupied; occupied.reserve(g.segments*5);
    auto finish=[&](unsigned layer,std::uint32_t prefix,const std::byte* header,
        std::span<const std::byte> body,std::span<const std::byte> keys,
        std::uint64_t first,std::uint64_t end) {
      const auto id=segment_id(layer,prefix);
      const std::array sizes{material::Geometry{64},material::Geometry{aligned(body.size())},material::Geometry{aligned(keys.size())}};
      auto definition=take(material::Definition::native(memory,2,100+layer,std::span(sizes).first(keys.empty()?2:3)));
      auto h=take(loom::allocate(worker,{.bytes=64,.alignment=64})); std::memcpy(h.address,header,64);
      auto b=take(loom::allocate(worker,{.bytes=aligned(body.size()),.alignment=64}));
      std::memset(b.address,0,b.size); std::memcpy(b.address,body.data(),body.size());
      const auto c=definition.control(id.low);
      orbital::allocation::Allocation k;
      if (!keys.empty()) { k=take(loom::allocate(worker,{.bytes=aligned(keys.size()),.alignment=64})); std::memset(k.address,0,k.size); std::memcpy(k.address,keys.data(),keys.size()); }
      const std::array src{orbital::PartSource{c.id(0),loom::bytes(h),{},&h},orbital::PartSource{c.id(1),loom::bytes(b),{},&b},orbital::PartSource{c.id(2),loom::bytes(k),{},&k}};
      auto owner=take(store.capture(c,std::span(src).first(keys.empty()?2:3)));
      directory::Dirent d; d.representation=100+layer;
      d.parts[0]=std::uintptr_t(owner.selection().part(0)->address()); d.parts[1]=std::uintptr_t(owner.selection().part(1)->address());
      if (!keys.empty()) d.parts[2]=std::uintptr_t(owner.selection().part(2)->address());
      std::array<std::byte,16> bounds;
      for (unsigned i=0;i<8;++i) { bounds[i]=std::byte((first/3)>>(56-8*i)); bounds[8+i]=std::byte(((end-1)/3)>>(56-8*i)); }
      occupied.push_back(bounds); const auto encoded=std::span<const std::byte>(occupied.back());
      entries[layer].push_back({prefix,d,encoded.first(8),encoded.last(8)}); changes.push_back({id,std::move(owner)});
    };
    for (unsigned s=0;s<g.segments;++s) {
      std::vector<std::array<std::uint64_t,7>> data; data.reserve(g.rows);
      std::vector<std::uint64_t> keys; keys.reserve(g.rows);
      for (unsigned i=0;i<g.rows;++i) {
        const auto row=initial(s*g.rows+i); keys.push_back(row[0]); std::array<std::uint64_t,7> payload;
        std::copy(row.begin()+1,row.end(),payload.begin()); data.push_back(payload);
      }
      std::array<std::byte,64> h{}; std::memcpy(h.data(),&g.rows,4);
      finish(4,s*17,h.data(),std::as_bytes(std::span(data)),std::as_bytes(std::span(keys)),s*g.rows,(s+1)*g.rows);
    }
    for (unsigned layer=0;layer<4;++layer) {
      std::uint32_t prefix=0; std::vector<Cell> segment;
      auto flush=[&] {
        if (segment.empty()) return;
        std::array<std::byte,64> h{}; const auto n=std::uint32_t(segment.size()); std::memcpy(h.data(),&n,4);
        finish(layer,prefix,h.data(),std::as_bytes(std::span(segment)),{},segment.front().first,segment.back().end); segment.clear();
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
      if (layer>=2 && layer<=shared) {
        hashes[layer]=take(directory->hashed(layer)); Store::Scope basis(store);
        auto groups=take(hashes[layer]->initialize(basis,entries[layer]));
        for (auto& group:groups) changes.push_back(std::move(group));
      } else {
        auto root=take(directory->build(layer,entries[layer])); changes.push_back({directory->root_id(layer),std::move(root)});
      }
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
    Row row_at(std::uint32_t prefix,const directory::Dirent& d,unsigned slot) {
      const auto physical=(std::uint64_t(prefix)<<16)|slot;
      const auto* data=reinterpret_cast<const std::byte*>(std::uintptr_t(d.parts[1]));
      const auto* keys=reinterpret_cast<const std::uint64_t*>(std::uintptr_t(d.parts[2]));
      Row r; r[0]=keys[slot]; std::memcpy(r.data()+1,data+slot*56,56);
      if (const auto* p=page(physical);p && (load<std::uint64_t>(p)&(UINT64_C(1)<<(slot&63))))
        r[3]=load<std::uint64_t>(p+64+(slot&63)*8);
      return r;
    }
    Row row(unsigned index) {
      const auto physical=owner.geometry.physical(index); const auto prefix=std::uint32_t(physical>>16);
      const auto* d=base.find(prefix); check(d,"base membership"); return row_at(prefix,*d,physical&65535);
    }
    std::pair<std::uint64_t,unsigned> equal(const directory::LogicalRead& read,std::uint64_t id,directory::Cursor& cursor) {
      std::array<std::byte,8> key; for (unsigned i=0;i<8;++i) key[i]=std::byte(id>>(56-8*i));
      const auto span=std::span<const std::byte>(key); check(bool(read.equal(span,cursor)),"logical equality seek");
      std::array<directory::EntryRef,8> candidates; std::uint64_t sum=0; unsigned count=0;
      for (;;) {
        const auto n=cursor.visit(candidates); if (!n) break;
        for (unsigned i=0;i<n;++i) {
          const auto& entry=candidates[i];
          const auto rows=load<std::uint32_t>(reinterpret_cast<const std::byte*>(std::uintptr_t(entry.value->parts[0])));
          const ikea::boundlist::direct_source<std::uint64_t> keys{
              reinterpret_cast<const std::uint64_t*>(std::uintptr_t(entry.value->parts[2])),rows};
          const auto first=ikea::boundlist::lower_bound(keys,id);
          for (auto slot=first;slot<rows && keys.at(slot)==id;++slot) { sum+=digest(row_at(entry.prefix,*entry.value,slot)); ++count; }
        }
      }
      return {sum,count};
    }
    template<ikea::hashtable::lookup_hint Hint>
    std::pair<std::uint64_t,unsigned> equal_shared(directory::HashRead& read,std::uint64_t id) {
      const auto layer=owner.shared;
      const auto prefix=std::uint32_t(id>>(64-8*layer));
      const auto result=take(read.group(prefix>>4)).template get<Hint>(prefix&15);
      if constexpr(Hint==ikea::hashtable::lookup_hint::predictable) if (!result.found) return {0,0};
      directory::Dirent d; std::memcpy(&d,result.value.data(),64);
      // Both candidate addresses are readable. U consumes zero misses without
      // reintroducing a random found branch around the whole useful consumer.
      alignas(64) static constexpr std::array<std::byte,64> empty_header{};
      const auto mask=std::uintptr_t(0)-std::uintptr_t(result.found);
      const auto* header=reinterpret_cast<const std::byte*>((std::uintptr_t(d.parts[0])&mask)|(std::uintptr_t(empty_header.data())&~mask));
      const auto count=load<std::uint32_t>(header);
      const auto* cells=reinterpret_cast<const Cell*>(std::uintptr_t(d.parts[1]));
      struct Maxima {
        const Cell* data; unsigned count;
        unsigned size() const noexcept { return count; }
        std::uint64_t at(unsigned i) const noexcept { return data[i].fields[0].maximum; }
      } maxima{cells,count};
      const auto first=ikea::boundlist::lower_bound(maxima,id);
      std::uint64_t sum=0; unsigned found=0;
      for (auto rank=first;rank<count && cells[rank].fields[0].minimum<=id;++rank) {
        const auto position=cells[rank].position;
        const auto base_prefix=layer==2?(prefix<<16)|position:(prefix<<8)|(position>>8);
        const auto* base_entry=base.find(base_prefix); check(base_entry,"summary to base directory");
        const auto rows=load<std::uint32_t>(reinterpret_cast<const std::byte*>(std::uintptr_t(base_entry->parts[0])));
        const unsigned low=layer==2?0:(position&255)*256;
        const unsigned high=layer==2?rows:std::min(rows,low+256);
        check(low<high,"summary cell slot range");
        const ikea::boundlist::direct_source<std::uint64_t> keys{
            reinterpret_cast<const std::uint64_t*>(std::uintptr_t(base_entry->parts[2]))+low,high-low};
        for (auto slot=ikea::boundlist::lower_bound(keys,id);slot<keys.size() && keys.at(slot)==id;++slot) {
          sum+=digest(row_at(base_prefix,*base_entry,low+slot)); ++found;
        }
      }
      return {sum,found};
    }
    const Cell* cell(unsigned layer,std::uint32_t prefix,std::uint16_t position) {
      check(layer<4,"summary layer");
      directory::Dirent copied; const directory::Dirent* d;
      if (owner.hashes[layer]) {
        directory::HashRead read; check(bool(owner.hashes[layer]->read(scope,read)),"hash summary read");
        auto selected=take(read.group(prefix>>4)).get(prefix&15); check(selected.found,"hash summary segment");
        std::memcpy(&copied,selected.value.data(),64); d=&copied;
      } else { auto read=take(owner.directory->read(scope,layer)); d=read.find(prefix); check(d,"summary segment"); }
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
  std::uint64_t read(std::span<const unsigned> queries,bool logical=false) {
    std::uint64_t sum=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      Store::Scope scope(store); Reader reader(*this,scope);
      if (logical && shared) {
        directory::HashRead read; check(bool(hashes[shared]->read(scope,read)),"shared-prefix hash read");
        const auto count=std::min<std::size_t>(64,queries.size()-first);
        equality_selector.run(count,[&](auto hint,std::size_t begin,std::size_t n) {
          for (auto i=begin;i<begin+n;++i) sum+=reader.template equal_shared<decltype(hint)::value>(read,queries[first+i]/3).first;
        });
      } else if (logical) {
        auto read=take(directory->logical(scope,4)); directory::Cursor cursor;
        for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=reader.equal(read,i/3,cursor).first;
      } else for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=digest(reader.row(i));
    }
    return sum;
  }
  std::uint64_t directory_queries(std::span<const unsigned> queries) {
    std::uint64_t sum=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      Store::Scope scope(store); auto read=take(directory->logical(scope,4)); directory::Cursor cursor;
      for (auto row:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) {
        std::array<std::byte,8> key; for (unsigned i=0;i<8;++i) key[i]=std::byte(std::uint64_t(row/3)>>(56-8*i));
        auto bytes=std::span<const std::byte>(key);
        check(bool(read.equal(bytes,cursor)),"directory diagnostic seek");
        std::array<directory::EntryRef,8> output;
        for (;;) { auto n=cursor.visit(output); if (!n) break; for (unsigned i=0;i<n;++i) sum+=std::uint64_t(output[i].prefix)+1; }
      }
    }
    return sum;
  }
};
struct Peer {
  Geometry geometry; bool logical; MDB_env* env{}; MDB_dbi db{}; std::string path;
  static int duplicate_order(const MDB_val* a,const MDB_val* b) { return std::memcmp(a->mv_data,b->mv_data,6); }
  static std::array<std::byte,62> datum(std::uint64_t physical,const Row& row) {
    std::array<std::byte,62> value;
    for (unsigned i=0;i<6;++i) value[i]=std::byte(physical>>(40-8*i));
    std::memcpy(value.data()+6,row.data()+1,56); return value;
  }
  Peer(Geometry g,bool ordered):geometry(g),logical(ordered) {
    char name[]="/tmp/sixdb-segment-driver-XXXXXX"; auto* p=mkdtemp(name); check(p,"temp directory"); path=p;
    lm(mdb_env_create(&env)); lm(mdb_env_set_mapsize(env,512u<<20));
    lm(mdb_env_open(env,path.c_str(),MDB_NOSYNC|MDB_NOMETASYNC|MDB_NOTLS,0600));
    MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,0,&txn));
    lm(mdb_dbi_open(txn,nullptr,MDB_INTEGERKEY|(logical?MDB_DUPSORT|MDB_DUPFIXED:0),&db));
    if (logical) lm(mdb_set_dupsort(txn,db,duplicate_order));
    for (unsigned i=0;i<g.size();++i) {
      auto key=logical?std::uint64_t(i/3):g.physical(i); auto r=initial(i); auto d=datum(g.physical(i),r);
      MDB_val k{8,&key},v{logical?62u:64u,logical?static_cast<void*>(d.data()):r.data()};
      lm(mdb_put(txn,db,&k,&v,logical?0:MDB_APPEND));
    }
    lm(mdb_txn_commit(txn));
  }
  ~Peer() { mdb_env_close(env); unlink((path+"/data.mdb").c_str()); unlink((path+"/lock.mdb").c_str()); rmdir(path.c_str()); }
  Row row(MDB_txn* txn,unsigned index) {
    if (logical) {
      auto key=std::uint64_t(index/3); auto lookup=datum(geometry.physical(index),{});
      MDB_val k{8,&key},v{62,lookup.data()}; MDB_cursor* cursor; lm(mdb_cursor_open(txn,db,&cursor));
      lm(mdb_cursor_get(cursor,&k,&v,MDB_GET_BOTH_RANGE));
      check(v.mv_size==62 && !std::memcmp(v.mv_data,lookup.data(),6),"peer logical identity");
      Row r; std::memcpy(r.data(),k.mv_data,8); std::memcpy(r.data()+1,static_cast<const std::byte*>(v.mv_data)+6,56); mdb_cursor_close(cursor); return r;
    }
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
      if (logical) {
        auto key=std::uint64_t(command.row/3); auto value=datum(geometry.physical(command.row),{});
        MDB_val k{8,&key},v{62,value.data()}; lm(mdb_cursor_get(cursor,&k,&v,MDB_GET_BOTH_RANGE));
        check(v.mv_size==62 && !std::memcmp(v.mv_data,value.data(),6),"peer update identity");
        std::memcpy(value.data(),v.mv_data,62); std::memcpy(value.data()+6+16,&command.value,8);
        v={62,value.data()}; lm(mdb_cursor_put(cursor,&k,&v,MDB_CURRENT)); continue;
      }
      auto key=geometry.physical(command.row); MDB_val k{8,&key},v; lm(mdb_cursor_get(cursor,&k,&v,MDB_SET));
      Row r; std::memcpy(r.data(),v.mv_data,64); r[3]=command.value;
      v={64,nullptr}; lm(mdb_cursor_put(cursor,&k,&v,MDB_CURRENT|MDB_RESERVE)); std::memcpy(v.mv_data,r.data(),64);
    }
    mdb_cursor_close(cursor); lm(mdb_txn_commit(txn));
  }
  std::pair<std::uint64_t,unsigned> equal(MDB_cursor* cursor,std::uint64_t id) {
    MDB_val key{8,&id},value; auto s=mdb_cursor_get(cursor,&key,&value,MDB_SET);
    std::uint64_t sum=0; unsigned count=0;
    while (!s) {
      check(value.mv_size==62,"logical duplicate width");
      Row r; std::memcpy(r.data(),key.mv_data,8); std::memcpy(r.data()+1,static_cast<const std::byte*>(value.mv_data)+6,56); sum+=digest(r); ++count;
      s=mdb_cursor_get(cursor,&key,&value,MDB_NEXT_DUP);
    }
    check(s==MDB_NOTFOUND,"logical duplicate status"); return {sum,count};
  }
  std::uint64_t read(std::span<const unsigned> queries) {
    std::uint64_t sum=0;
    for (std::size_t first=0;first<queries.size();first+=64) {
      MDB_txn* txn; lm(mdb_txn_begin(env,nullptr,MDB_RDONLY,&txn));
      if (logical) {
        MDB_cursor* cursor; lm(mdb_cursor_open(txn,db,&cursor));
        for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=equal(cursor,i/3).first;
        mdb_cursor_close(cursor);
      } else for (auto i:queries.subspan(first,std::min<std::size_t>(64,queries.size()-first))) sum+=digest(row(txn,i));
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
  std::vector<std::pair<std::uint64_t,unsigned>> groups((model.size()+2)/3+2);
  for (const auto& row:model) { groups[row[0]].first+=digest(row); ++groups[row[0]].second; }
  auto logical=take(p.directory->logical(scope,4)); directory::Cursor ordered_cursor;
  directory::HashRead hash; if (p.shared) check(bool(p.hashes[p.shared]->read(scope,hash)),"hash equality admission");
  if (p.shared) for (const auto id:{UINT64_MAX,UINT64_C(1)<<(64-8*p.shared)}) {
    const std::pair<std::uint64_t,unsigned> empty{};
    check(reader.equal_shared<ikea::hashtable::lookup_hint::predictable>(hash,id)==empty,"actual hash miss P");
    check(reader.equal_shared<ikea::hashtable::lookup_hint::unpredictable>(hash,id)==empty,"actual hash miss U zero header");
  }
  MDB_cursor* cursor=nullptr; if (peer.logical) lm(mdb_cursor_open(txn,peer.db,&cursor));
  for (unsigned id=0;id<groups.size();++id) {
    if (p.shared) {
      check(reader.equal_shared<ikea::hashtable::lookup_hint::predictable>(hash,id)==groups[id],"shared-prefix equality P oracle");
      check(reader.equal_shared<ikea::hashtable::lookup_hint::unpredictable>(hash,id)==groups[id],"shared-prefix equality U oracle");
    }
    check(reader.equal(logical,id,ordered_cursor)==groups[id],"full duplicate equality oracle");
    if (cursor) check(peer.equal(cursor,id)==groups[id],"peer duplicate equality oracle");
  }
  if (cursor) mdb_cursor_close(cursor);
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
void run(loom::Worker& worker,Geometry geometry,unsigned hot,unsigned batch,unsigned writes,unsigned trials,bool logical,unsigned shared,bool mixed) {
  Producer p(worker,geometry,shared); Peer peer(geometry,logical);
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
  auto read_queries=queries;
  if (mixed) for (unsigned i=0;i<read_queries.size();++i)
    if (!(mix(i+917)&3)) read_queries[i]+=3*((geometry.size()+2)/3+1);
  check(p.read(read_queries,logical)==peer.read(read_queries),"pre-screen output");
  for (unsigned trial=0;trial<trials;++trial) {
    std::array<double,2> read_ns{},write_ns{}; std::array<std::uint64_t,2> output;
    for (unsigned order=0;order<2;++order) {
      const auto arm=order^(trial&1); auto read=[&] { return arm?peer.read(read_queries):p.read(read_queries,logical); };
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
  if (logical) {
    // A separate changed-cache-state diagnostic, not an additive attribution
    // or a comparison with the covering row peer.
    std::uint64_t expected=0;
    for (auto row:read_queries) for (unsigned s=0;s<geometry.segments;++s)
      if (row/3>=(s*geometry.rows)/3 && row/3<=((s+1)*geometry.rows-1)/3) expected+=s*17+1;
    check(p.directory_queries(read_queries)==expected,"directory-only candidate oracle");
    const auto start=Clock::now(); const auto sum=p.directory_queries(read_queries);
    const auto ns=std::chrono::duration<double,std::nano>(Clock::now()-start).count()/queries.size();
    check(sum==expected,"timed directory candidate oracle");
    std::printf("diagnostic_directory_seek_all_candidates_ns=%.3f,not_additive=1,digest=%llu\n",ns,(unsigned long long)sum);
  }
  if (shared) {
    const auto selection=p.equality_selector.statistics();
    check(selection.predictable && selection.unpredictable,"automatic selector did not persist");
    std::printf("automatic_hash_lookup,predictable=%llu,unpredictable=%llu,probes=%llu,switches=%llu,scope=complete_shared_prefix_summary_record_afterimage_full64_consumer\n",
        (unsigned long long)selection.predictable,(unsigned long long)selection.unpredictable,(unsigned long long)selection.probes,(unsigned long long)selection.switches);
  }
  p.store.drain(); const auto native=orbital::allocation::statistics(*worker.memory);
  std::printf("settled_footprint,pmr_requested_live=%zu,pmr_requested_peak=%zu,pmr_backing_live=%zu,pmr_backing_peak=%zu,native_mapped=%llu,native_idle=%llu,native_metadata=%llu,native_reserved=%llu,excludes_process_RSS=1\n",
      p.memory.live,p.memory.peak,p.backing.live,p.backing.peak,(unsigned long long)native.mapped_bytes,(unsigned long long)native.idle_bytes,
      (unsigned long long)native.metadata_bytes,(unsigned long long)native.reserved_bytes);
}
}
int main(int argc,char** argv) {
  Geometry g{argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):2,argc>2?unsigned(std::strtoul(argv[2],nullptr,10)):1024};
  const unsigned hot=argc>3?std::strtoul(argv[3],nullptr,10):64,batch=argc>4?std::strtoul(argv[4],nullptr,10):64;
  const unsigned writes=argc>5?std::strtoul(argv[5],nullptr,10):4096,trials=argc>6?std::strtoul(argv[6],nullptr,10):4;
  const bool shared_mode=argc>7 && std::string(argv[7])=="shared";
  const bool logical=shared_mode || (argc>7 && std::string(argv[7])=="logical");
  const unsigned shared=shared_mode?(g.segments<=16?3:2):0;
  const bool mixed=argc>8 && std::string(argv[8])=="mixed";
  check(!mixed || logical,"mixed queries require logical mode");
  check(g.segments && g.segments<=80 && g.rows && g.rows<=65536 && std::uint64_t(g.size())<=524288 && hot && hot<=g.size() && batch && batch<=64 && writes && trials,"arguments");
  std::printf("context,multirow_native_base_plus_snapshot_COW_afterimage_pages,field=3,all5layers_exist,logical_keys_duplicate_runs3,scope_or_transaction=64,memory_only_NOSYNC_NOMETASYNC_NOTLS,compiler=%s\n",__clang_version__);
  std::printf("query_mode,%s,base_parts=header_payload_keys_no_duplicated_id\n",shared_mode?"logical_equality_shared_prefix_hash_summary_plan_peer_DUP62":logical?"logical_equality_all_duplicates_peer_DUPSORT_DUPFIXED62_current":"physical_points_peer_native_u64");
  loom::Worker worker; check(bool(loom::adopt(worker)),"adopt"); check(bool(loom::configure(worker,take(orbital::allocation::open_arena(512u<<20)),0)),"configure");
    std::printf("workload,shared_bytes=%u,read_misses=%s,queries=131072,writes_offered=%u\n",shared,mixed?"25pct_deterministic":"none",writes);
  run(worker,g,hot,batch,writes,trials,logical,shared,mixed);
  for (;;) { auto s=loom::shutdown(worker); check(s.kind()!=orbital::Progress::Kind::failed,"shutdown"); if (s.kind()==orbital::Progress::Kind::done) break; loom::poll(worker,{64,0}); }
  loom::relinquish(worker);
}
