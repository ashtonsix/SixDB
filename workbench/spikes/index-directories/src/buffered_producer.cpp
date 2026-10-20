#include "buffered_producer.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace index_spike::driver {
namespace {
struct Failure { orbital::Error error; };
template<class T> T take(orbital::Result<T> r) {
  if (!r) throw Failure{r.error()};
  return std::move(*r);
}
void take(orbital::Result<void> r) { if (!r) throw Failure{r.error()}; }
template<class T> T load(const std::byte* p,std::size_t at=0) {
  T x; std::memcpy(&x,p+at,sizeof(x)); return x;
}
template<class T> void put(std::byte* p,std::size_t at,T x) {
  std::memcpy(p+at,&x,sizeof(x));
}
void require(bool b) { if (!b) throw Failure{orbital::Error::invalid_argument}; }
std::size_t cells_at(unsigned n) { return (64+n*2+63)&~std::size_t(63); }
Sum unpack(const std::byte* p) { return Sum(load<std::uint64_t>(p))|(Sum(load<std::uint64_t>(p,8))<<64); }
void pack(std::byte* p,Sum x) {
  put(p,0,std::uint64_t(x)); put(p,8,std::uint64_t(x>>64));
}
FieldSummary one(std::uint64_t x) { return {1,x,x,x,0,1}; }
}
void* BufferedProducer::Memory::do_allocate(std::size_t n,std::size_t a) {
  if (fail) { fail=false; throw std::bad_alloc(); }
  auto* p=pool_.allocate(n,a); live+=n; peak=std::max(peak,live); return p;
}
void BufferedProducer::Memory::do_deallocate(void* p,std::size_t n,std::size_t a) {
  live-=n; pool_.deallocate(p,n,a);
}
std::shared_ptr<BufferedProducer::Backing> BufferedProducer::allocate(
    std::size_t bytes,std::shared_ptr<Advice> advice) {
  auto body=take(loom::allocate(worker_,{.bytes=bytes,.alignment=64}));
  return std::make_shared<Backing>(Backing{std::move(body),std::move(advice),0});
}
Store::Owner BufferedProducer::finish(orbital::ObjectId id,const Format& format,
    const std::shared_ptr<Backing>& backing,std::size_t bytes,bool prefix) {
  if (!bytes) bytes=format.bytes[0];
  require(bytes<=backing->allocation.size && !(bytes&7));
  const Control control{&format,id.low,prefix?std::uint32_t(bytes/8):0,1,
      orbital::PartOwners::independent_shared,prefix?ExtentMode::prefix8:ExtentMode::parts};
  const std::array sources{orbital::PartSource{control.id(0),
      {backing->allocation.address,bytes},backing,{}}};
  // Seal before capture, including its failure path. Never reissue any store
  // in this attempted prefix, even if no catalog publication follows.
  backing->sealed=std::max(backing->sealed,bytes);
  auto owner=take(store_.capture(control,sources)); ++counters_.captures;
  return owner;
}
Store::Owner BufferedProducer::native(orbital::ObjectId id,const Format& format,
    const std::byte* bytes) {
  auto body=take(loom::allocate(worker_,{.bytes=format.bytes[0],.alignment=64}));
  idx_log_copy8(body.address,bytes,format.bytes[0]);
  return capture_native(id,format,body);
}
Store::Owner BufferedProducer::capture_native(orbital::ObjectId id,const Format& format,
    orbital::allocation::Allocation& body) {
  const Control c{&format,id.low,0,1};
  const std::array sources{orbital::PartSource{c.id(0),loom::bytes(body),{},&body}};
  auto result=take(store_.capture(c,sources)); ++counters_.captures; return result;
}
const std::byte* BufferedProducer::part(Selection s) {
  require(bool(s)); return take(s.part(0)).address();
}
const std::byte* BufferedProducer::descriptor(const Store::Scope& scope,unsigned row) const {
  require(row<rows_);
  ReadContext context{dir_ns,&scope,{}};
  NativeRead view{&context,1,0,0,0,0,0,0,0,0};
  auto* p=idx_asm_group16_read(&view,row); require(p); return p;
}
std::pair<const std::byte*,std::size_t> BufferedProducer::log(const Store::Scope& scope) const {
  const auto selected=scope.bind(collection());
  const auto extent=take(selected.control().extent(0));
  return {part(selected),extent};
}
const std::byte* BufferedProducer::Reader::descriptor(unsigned r) {
  require(r<producer_.rows_);
  auto& group=groups_[r/16];
  if (!group) group=part(scope_.bind(lower(r/16)));
  // This closed producer has dense original positions0..rows-1. Admission and
  // membership remain explicit; a cached group is not a cross-publication map.
  const auto mask=load<std::uint16_t>(group);
  require(mask&(1u<<(r&15)));
  const unsigned rank=std::popcount(mask&((1u<<(r&15))-1));
  return group+64+rank*64;
}
void BufferedProducer::Reader::pending() {
  if (pending_) return;
  if (producer_.policy_==BufferPolicy::eager) pending_=part(scope_.bind(eager_sum()));
  else std::tie(pending_,extent_)=producer_.log(scope_);
}
std::uint64_t BufferedProducer::Reader::corrected(unsigned r,const std::byte* d,std::uint64_t base) {
  const auto* advice=load<const Advice*>(d,56);
  if (!(advice->dirty&(std::uint64_t(1)<<load<std::uint16_t>(d,54)))) return base;
  pending();
  if (producer_.policy_==BufferPolicy::eager)
    return (load<std::uint64_t>(pending_,16)&(std::uint64_t(1)<<r))?
        load<std::uint64_t>(pending_,64+r*8):base;
  return producer_.policy_==BufferPolicy::plain?idx_log_find(pending_,extent_,r,base):
      idx_log_index_find(pending_,extent_,r,base);
}
std::uint64_t BufferedProducer::Reader::field3(unsigned r) {
  const auto* d=descriptor(r); return corrected(r,d,load<std::uint64_t>(load<const std::byte*>(d),24));
}
Row BufferedProducer::Reader::row(unsigned r) {
  const auto* d=descriptor(r); Row out;
  std::memcpy(out.data(),load<const std::byte*>(d),64);
  out[3]=corrected(r,d,out[3]); return out;
}
std::uint64_t BufferedProducer::Reader::digest(unsigned r) {
  const auto* d=descriptor(r); const auto* bytes=load<const std::byte*>(d);
  std::uint64_t digest=0;
  for (unsigned f=0;f<8;++f) digest^=load<std::uint64_t>(bytes,f*8);
  const auto before=load<std::uint64_t>(bytes,24);
  return digest^before^corrected(r,d,before);
}
RangeResult BufferedProducer::Reader::range(unsigned low,unsigned high,std::uint64_t mask) {
  RangeResult out;
  if (low>high || low>=producer_.rows_) return out;
  high=std::min(high,producer_.rows_-1);
  mask&=UINT64_MAX<<low;
  if (high<63) mask&=(UINT64_C(1)<<(high+1))-1;
  // Only selected slots are read. idx_log_apply performs full8B afterimage
  // stores, without reading unselected slots; their initial values are unused.
  std::array<std::uint64_t,64> base,digests,values;
  bool plain_dirty=false;
  for (auto bits=mask;bits;bits&=bits-1) {
    const unsigned r=std::countr_zero(bits);
    const auto* d=descriptor(r); // at most one bind per actual selected group
    const auto* bytes=load<const std::byte*>(d);
    base[r]=values[r]=load<std::uint64_t>(bytes,24);
    std::uint64_t digest=0;
    for (unsigned f=0;f<8;++f) digest^=load<std::uint64_t>(bytes,f*8);
    digests[r]=digest;
    if (producer_.policy_==BufferPolicy::plain) {
      const auto* advice=load<const Advice*>(d,56);
      plain_dirty|=bool(advice->dirty&(UINT64_C(1)<<load<std::uint16_t>(d,54)));
    } else values[r]=corrected(r,d,base[r]);
  }
  if (plain_dirty) { pending(); idx_log_apply(pending_,extent_,values.data()); }
  for (auto bits=mask;bits;bits&=bits-1) {
    const unsigned r=std::countr_zero(bits);
    out.sum+=digests[r]^base[r]^values[r]; ++out.selected;
  }
  return out;
}
std::array<std::uint64_t,64> BufferedProducer::Reader::values() {
  std::array<std::uint64_t,64> out{};
  for (unsigned r=0;r<producer_.rows_;++r) {
    const auto* d=descriptor(r);
    const auto base=load<std::uint64_t>(load<const std::byte*>(d),24);
    out[r]=producer_.policy_==BufferPolicy::plain?base:corrected(r,d,base);
  }
  if (producer_.policy_==BufferPolicy::plain) { pending(); idx_log_apply(pending_,extent_,out.data()); }
  return out;
}
Sum BufferedProducer::Reader::sum() {
  const auto* d=part(scope_.bind(upper(0)));
  if (!(load<const Advice*>(d,56)->dirty&8)) {
    const auto s=load<FieldSummary>(load<const std::byte*>(d),cells_at(1)+3*sizeof(FieldSummary));
    return Sum(s.sum_low)|(Sum(s.sum_high)<<64);
  }
  if (producer_.policy_!=BufferPolicy::plain) {
    pending(); return unpack(pending_+(producer_.policy_==BufferPolicy::indexed?extent_-160:0));
  }
  const auto x=values(); Sum total=0;
  for (unsigned r=0;r<producer_.rows_;++r) total+=x[r]; return total;
}
std::uint64_t BufferedProducer::field3(const Store::Scope& scope,unsigned r) const {
  auto view=reader(scope); return view.field3(r);
}
Row BufferedProducer::read(const Store::Scope& scope,unsigned r) const {
  auto view=reader(scope); return view.row(r);
}
std::array<std::uint64_t,64> BufferedProducer::values(const Store::Scope& scope) const {
  auto view=reader(scope); return view.values();
}
Sum BufferedProducer::sum(const Store::Scope& scope) const {
  auto view=reader(scope); return view.sum();
}
FieldSummary BufferedProducer::layer_summary(const Store::Scope& scope,unsigned layer,
    std::uint16_t position,unsigned field) const {
  require(layer<4 && field<8);
  const auto* directory=part(scope.bind(upper(layer)))+(layer==0?0:64);
  const auto* p=load<const std::byte*>(directory);
  const unsigned count=load<std::uint16_t>(p);
  unsigned rank=0;
  while (rank<count && load<std::uint16_t>(p,64+rank*2)<position) ++rank;
  require(rank<count && load<std::uint16_t>(p,64+rank*2)==position);
  const auto* advice=load<const Advice*>(directory,56);
  if (field!=3 || !(advice->dirty&8))
    return load<FieldSummary>(p,cells_at(count)+(rank*8+field)*sizeof(FieldSummary));
  if (layer>=2) return one(field3(scope,layer==2?position:position>>8));
  const auto current=values(scope);
  auto result=one(current[0]); Sum total=current[0];
  for (unsigned r=1;r<rows_;++r) {
    result.min=std::min(result.min,current[r]); result.max=std::max(result.max,current[r]); total+=current[r];
  }
  result.count=result.not_null=rows_; result.sum_low=std::uint64_t(total); result.sum_high=std::uint64_t(total>>64);
  return result;
}
Store::Owner BufferedProducer::make_summary(unsigned layer,const std::array<Row,64>& rows,
    std::shared_ptr<Advice> advice) {
  const unsigned n=layer<2?1:rows_;
  const auto& f=layer<2?summary_small_:summary_large_;
  auto body=allocate(f.bytes[0],std::move(advice)); auto* p=body->allocation.address;
  std::memset(p,0,f.bytes[0]); put(p,0,std::uint16_t(n)); put(p,2,std::uint8_t(layer));
  for (unsigned i=0;i<n;++i) {
    put(p,64+i*2,std::uint16_t(layer<2?0:layer==2?i:i<<8));
    for (unsigned field=0;field<8;++field) {
      FieldSummary s=one(rows[i][field]);
      if (layer<2) {
        Sum total=rows[0][field];
        for (unsigned r=1;r<rows_;++r) {
          s.min=std::min(s.min,rows[r][field]); s.max=std::max(s.max,rows[r][field]); total+=rows[r][field];
        }
        s.count=s.not_null=rows_; s.sum_low=std::uint64_t(total); s.sum_high=std::uint64_t(total>>64);
      }
      put(p,cells_at(n)+(i*8+field)*sizeof(FieldSummary),s);
    }
  }
  return finish(summary(layer),f,body);
}
void BufferedProducer::append_change(std::vector<Store::Change>& changes,unsigned& n,
    orbital::ObjectId id,Store::Owner owner) {
  require(n<144 && changes.size()==n); changes.push_back({id,std::move(owner)}); ++n;
}
void BufferedProducer::fold_drafts(const Store::Scope&,const std::array<Row,64>& rows,
    std::vector<Store::Change>& changes,unsigned& n,unsigned fail_at) {
  auto advice=std::make_shared<Advice>();
  std::array<std::array<std::byte,1088>,4> dirs{};
  for (unsigned r=0;r<rows_;++r) {
    auto b=allocate(64,advice); idx_asm_copy64(b->allocation.address,reinterpret_cast<const std::byte*>(rows[r].data()));
    if (r==fail_at) memory_.fail=true;
    auto owner=finish(data(r),data_format,b);
    auto* d=dirs[r/16].data()+64+(r%16)*64;
    put(d,0,part(owner.selection())); put(d,48,std::uint32_t{1});
    put(d,54,std::uint16_t(r)); put(d,56,advice.get());
    append_change(changes,n,data(r),std::move(owner));
  }
  for (unsigned g=0;g<(rows_+15)/16;++g) {
    const unsigned count=std::min(16u,rows_-g*16);
    put(dirs[g].data(),0,std::uint16_t((1u<<count)-1));
    append_change(changes,n,lower(g),native(lower(g),groups_[count],dirs[g].data()));
    ++counters_.directory_changes;
  }
  auto summary_advice=std::make_shared<Advice>();
  for (unsigned layer=0;layer<4;++layer) {
    auto s=make_summary(layer,rows,summary_advice);
    const auto& format=layer==0?top_format:layer==1?l1_format:middle_format;
    auto directory=take(loom::allocate(worker_,{.bytes=format.bytes[0],.alignment=64}));
    // L0 has one slot; L1 is one ordinary256-slot table allocation. Reserved
    // zero slots do not create Segments. This profile populates only prefix0.
    std::memset(directory.address,0,format.bytes[0]); const unsigned at=layer==0?0:64;
    if (at) put(directory.address,0,std::uint16_t{1});
    put(directory.address,at,part(s.selection())); put(directory.address,at+48,std::uint32_t(layer+2));
    put(directory.address,at+54,std::uint16_t{3}); put(directory.address,at+56,summary_advice.get());
    append_change(changes,n,summary(layer),std::move(s));
    append_change(changes,n,upper(layer),capture_native(upper(layer),format,directory));
    ++counters_.directory_changes;
  }
}
void BufferedProducer::mark_summaries(const Store::Scope& scope) {
  // The four current summary material Backings OWN the SAME advice in this
  // closed producer. Initial construction/full fold changes all four together.
  // Thus one selected L0 dirent proves the actual shared mark for every layer.
  load<Advice*>(part(scope.bind(upper(0))),56)->dirty|=8;
}
void BufferedProducer::publish(const Store::Scope& basis,std::span<const Store::Change> changes,Fault fault) {
  struct Clear { Memory& m; ~Clear() { m.fail=false; } } clear{memory_};
  if (fault==Fault::prepare) memory_.fail=true;
  if (fault==Fault::stale) {
    // Actual independent Object publication advances the GLOBAL basis. It is
    // not a fake old-cut number or a second Engine visibility mechanism.
    std::array<std::byte,64> marker{};
    const orbital::ObjectId id{log_ns,2};
    const std::array advance{Store::Change{id,native(id,marker_format,marker.data())}};
    Store::Scope current(store_); Store::Prepared packet(store_);
    take(store_.prepare(current,advance,packet)); take(store_.install(packet));
  }
  Store::Prepared packet(store_);
  take(store_.prepare(basis,changes,packet)); take(store_.install(packet));
  ++counters_.publications;
}
BufferedProducer::BufferedProducer(loom::Worker& worker,BufferPolicy policy,
    std::span<const Row> input,std::size_t capacity)
    : worker_(worker),policy_(policy),rows_(input.size()),capacity_(capacity),
      summary_small_{{std::uint32_t(cells_at(1)+384)},1,summary_ns},
      summary_large_{{std::uint32_t(cells_at(rows_)+rows_*384)},1,summary_ns},
      log_format_{{std::uint32_t(capacity),std::uint32_t(policy==BufferPolicy::plain?8:160)},1,log_ns},
      store_(memory_) {
  static_assert(sizeof(Backing)==56 && sizeof(Advice)==8 && sizeof(Store::Change)==24 && sizeof(Format)==96);
  require(rows_ && rows_<=64 && capacity<=256u<<10 && !(capacity&7)
      && capacity>=(policy==BufferPolicy::plain?8:160)+64*24+160);
  for (auto* f:{&summary_small_,&summary_large_,&log_format_,&data_format,&top_format,&l1_format,&middle_format,&eager_format,&marker_format})
    f->memory=&memory_;
  for (unsigned i=1;i<=16;++i) groups_[i]={{64+i*64},1,dir_ns,nullptr,&memory_};
  std::array<Row,64> rows{}; std::copy(input.begin(),input.end(),rows.begin());
  work_.reserve(144); auto& changes=work_; unsigned n=0;
  Store::Scope basis(store_); fold_drafts(basis,rows,changes,n);
  Sum total=0; for (const auto& row:input) total+=row[3];
  if (policy_==BufferPolicy::eager) {
    std::array<std::byte,576> bytes{}; pack(bytes.data(),total);
    append_change(changes,n,eager_sum(),native(eager_sum(),eager_format,bytes.data()));
  } else {
    writer_=allocate(capacity_); auto* p=writer_->allocation.address;
    const unsigned initial=policy_==BufferPolicy::plain?8:160;
    std::memset(p,0,initial); if (policy_==BufferPolicy::indexed) pack(p,total);
    append_change(changes,n,collection(),finish(collection(),log_format_,writer_,initial,true));
  }
  publish(basis,std::span(changes).first(n),Fault::none);
  changes.clear();
}
void BufferedProducer::update_impl(std::span<const Command> commands,bool force_fold,Fault fault) {
  require(commands.size()<=64);
  Store::Scope basis(store_);
  auto selected=reader(basis);
  std::array<std::uint64_t,64> before,after;
  std::array<Advice*,64> advice; std::uint64_t touched=0;
  for (const auto& command:commands) {
    require(command.row<rows_); const auto bit=std::uint64_t(1)<<command.row;
    if (!(touched&bit)) {
      const auto* d=selected.descriptor(command.row);
      before[command.row]=selected.corrected(command.row,d,
          load<std::uint64_t>(load<const std::byte*>(d),24));
      advice[command.row]=load<Advice*>(d,56);
    }
    // Commands have ordinary assignment semantics. Repeats see the private
    // prior command, and coalescing never reads the logical oracle's state.
    after[command.row]=command.after; touched|=bit;
  }
  for (auto bits=touched;bits;bits&=bits-1) {
    const unsigned r=std::countr_zero(bits);
    if (before[r]==after[r]) touched&=~(std::uint64_t(1)<<r);
  }
  if (!touched && !force_fold) return;
  const auto count=std::popcount(touched);
  std::size_t old_extent=0; const std::byte* old=nullptr;
  if (policy_!=BufferPolicy::eager) {
    selected.pending(); old=selected.pending_; old_extent=selected.extent_;
  }
  const auto suffix=std::size_t(count)*24+(policy_==BufferPolicy::indexed?160:0);
  const bool fold=force_fold || (policy_!=BufferPolicy::eager && old_extent+suffix>capacity_);
  auto& changes=work_; require(changes.empty()); unsigned n=0;
  struct Clear { std::vector<Store::Change>& x; ~Clear() { x.clear(); } } clear{changes};
  if (fold) {
    std::array<Row,64> rows;
    std::array<std::uint64_t,64> current;
    for (unsigned r=0;r<rows_;++r) {
      const auto* d=selected.descriptor(r);
      std::memcpy(rows[r].data(),load<const std::byte*>(d),64);
      current[r]=policy_==BufferPolicy::plain?rows[r][3]:selected.corrected(r,d,rows[r][3]);
    }
    // One selected stream application, not64 backward scans or a second
    // directory-binding pass. All four group views and pending bytes reuse basis.
    if (policy_==BufferPolicy::plain) { selected.pending(); idx_log_apply(selected.pending_,selected.extent_,current.data()); }
    for (unsigned r=0;r<rows_;++r)
      rows[r][3]=(touched&(std::uint64_t(1)<<r))?after[r]:current[r];
    fold_drafts(basis,rows,changes,n,fault==Fault::capture?std::min(2u,rows_-1):64);
    Sum total=0; for (unsigned r=0;r<rows_;++r) total+=rows[r][3];
    if (policy_==BufferPolicy::eager) {
      // Existing absolute afterimages ALREADY equal the folded base. Keeping
      // them is safe even if a subsequent refusal conservatively dirties advice.
      // Only actual command changes require new row/SUM material, never64
      // artificial rebasing publications for an ordinary full compaction.
      require(!touched); // public eager full fold is compaction, not a write request
    } else {
      writer_=allocate(capacity_); const unsigned initial=policy_==BufferPolicy::plain?8:160;
      std::memset(writer_->allocation.address,0,initial);
      if (policy_==BufferPolicy::indexed) pack(writer_->allocation.address,total);
      append_change(changes,n,collection(),finish(collection(),log_format_,writer_,initial,true));
    }
    ++counters_.folds;
  } else if (policy_==BufferPolicy::eager) {
    selected.pending(); const auto* old=selected.pending_;
    Sum total=unpack(old);
    auto body=take(loom::allocate(worker_,{.bytes=576,.alignment=64}));
    idx_asm_clone_patch64(body.address,old,9,16,load<std::uint64_t>(old,16)|touched);
    for (auto bits=touched;bits;bits&=bits-1) {
      const unsigned r=std::countr_zero(bits);
      total-=before[r]; total+=after[r];
      put(body.address,64+r*8,after[r]);
    }
    pack(body.address,total);
    if (fault==Fault::capture) memory_.fail=true;
    append_change(changes,n,eager_sum(),capture_native(eager_sum(),eager_format,body));
  } else {
    if (!writer_ || writer_->allocation.address!=old || writer_->sealed!=old_extent) {
      writer_=allocate(capacity_); idx_log_copy8(writer_->allocation.address,old,old_extent);
      writer_->sealed=old_extent; counters_.log_copy_bytes+=old_extent; ++counters_.rotations;
    }
    auto* p=writer_->allocation.address;
    // Reserve/seal the complete private attempt BEFORE stores. A later capture
    // or prepare failure cannot make this suffix writable again.
    const auto next=old_extent+suffix; writer_->sealed=next;
    std::uint64_t seq=policy_==BufferPolicy::plain?(old_extent-8)/24:load<std::uint64_t>(old+old_extent-160,16);
    std::byte* footer=nullptr; Sum total=0;
    if (policy_==BufferPolicy::indexed) {
      footer=p+next-160; idx_log_copy8(footer,old+old_extent-160,160);
      total=unpack(footer); counters_.log_copy_bytes+=160;
    }
    std::size_t at=old_extent;
    for (auto bits=touched;bits;bits&=bits-1) {
      const unsigned r=std::countr_zero(bits);
      idx_log_append24(p+at,r,++seq,after[r]);
      if (footer) { put(footer,24+r*2,std::uint16_t(at/8)); total-=before[r]; total+=after[r]; }
      at+=24;
    }
    if (footer) { pack(footer,total); put(footer,16,seq); }
    if (fault==Fault::capture) memory_.fail=true;
    append_change(changes,n,collection(),finish(collection(),log_format_,writer_,next,true));
    counters_.events+=count;
  }
  // Conservative false-positive dirtiness on refusal is safe. No old advice
  // generation is cleared or recycled, and no private logical state is read.
  for (auto bits=touched;bits;bits&=bits-1) {
    const unsigned r=std::countr_zero(bits); advice[r]->dirty|=std::uint64_t(1)<<r;
  }
  mark_summaries(basis);
  publish(basis,std::span(changes).first(n),fault);
}
orbital::Result<void> BufferedProducer::update(std::span<const Command> commands,Fault fault) noexcept {
  try { update_impl(commands,false,fault); return {}; }
  catch (const Failure& x) { memory_.fail=false; ++counters_.refusals; return std::unexpected(x.error); }
  catch (const std::bad_alloc&) { memory_.fail=false; ++counters_.refusals; return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> BufferedProducer::compact_all(Fault fault) noexcept {
  try { update_impl({},true,fault); return {}; }
  catch (const Failure& x) { memory_.fail=false; ++counters_.refusals; return std::unexpected(x.error); }
  catch (const std::bad_alloc&) { memory_.fail=false; ++counters_.refusals; return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> BufferedProducer::compact_row(unsigned r) noexcept {
  try {
    require(r<rows_); Store::Scope basis(store_); const auto current=read(basis,r);
    auto advice=std::make_shared<Advice>(); auto body=allocate(64,advice);
    idx_asm_copy64(body->allocation.address,reinterpret_cast<const std::byte*>(current.data()));
    auto material=finish(data(r),data_format,body);
    const unsigned count=std::min(16u,rows_-(r/16)*16);
    auto directory=take(loom::allocate(worker_,{.bytes=groups_[count].bytes[0],.alignment=64}));
    const auto* old=part(basis.bind(lower(r/16)));
    idx_asm_clone_patch64(directory.address,old,count+1,64+(r%16)*64,
        std::uintptr_t(part(material.selection())));
    put(directory.address,64+(r%16)*64+56,advice.get());
    const Control c{&groups_[count],lower(r/16).low,0,1};
    const std::array sources{orbital::PartSource{c.id(0),loom::bytes(directory),{},&directory}};
    auto d=take(store_.capture(c,sources)); ++counters_.captures; ++counters_.directory_changes;
    const std::array changes{Store::Change{data(r),std::move(material)},Store::Change{lower(r/16),std::move(d)}};
    publish(basis,changes,Fault::none); return {};
  } catch (const Failure& x) { ++counters_.refusals; return std::unexpected(x.error); }
  catch (const std::bad_alloc&) { ++counters_.refusals; return std::unexpected(orbital::Error::capacity); }
}
std::array<const void*,5> BufferedProducer::directories(const Store::Scope& scope,unsigned row) const {
  std::array<const void*,5> d{};
  for (unsigned l=0;l<4;++l) d[l]=scope.bind(upper(l)).realization;
  d[4]=scope.bind(lower(row/16)).realization; return d;
}
}
