#include <engine/directory.h>
#include <engine/directory_hash.h>
#include "directory_admission.h"
#include <ikea/boundlist/search.h>
#include <loom/worker.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <vector>

namespace engine::directory {
namespace detail {
using Store=material::Store;
struct Key {
  std::uint64_t prefix;
  const std::byte* tail;
  unsigned size;
  friend bool operator<(Key a,Key b) noexcept {
    return a.prefix!=b.prefix?a.prefix<b.prefix:a.size && std::memcmp(a.tail,b.tail,a.size)<0;
  }
};
Key key(std::span<const std::byte> bytes) noexcept {
  std::uint64_t prefix=0; const unsigned n=std::min<std::size_t>(8,bytes.size());
  if (n==8) {
    std::memcpy(&prefix,bytes.data(),8);
    if constexpr(std::endian::native==std::endian::little) prefix=std::byteswap(prefix);
  } else {
    for (unsigned i=0;i<n;++i) prefix=(prefix<<8)|std::to_integer<unsigned>(bytes[i]);
    if (n) prefix<<=(8-n)*8;
  }
  return {prefix,bytes.size()>8?bytes.data()+8:nullptr,unsigned(bytes.size()>8?bytes.size()-8:0)};
}
struct Bounds {
  std::pmr::memory_resource* memory;
  mutable std::size_t references{1};
  unsigned count,width;
  std::array<std::uint8_t,32> slots{};
  std::size_t tail() const noexcept { return width>8?width-8:0; }
  std::uint64_t* minima() noexcept { return reinterpret_cast<std::uint64_t*>(this+1); }
  const std::uint64_t* minima() const noexcept { return reinterpret_cast<const std::uint64_t*>(this+1); }
  std::uint64_t* maxima() noexcept { return minima()+count; }
  const std::uint64_t* maxima() const noexcept { return minima()+count; }
  Key at(unsigned rank,bool maximum) const noexcept {
    auto* tails=reinterpret_cast<const std::byte*>(minima()+2*count);
    return {(maximum?maxima():minima())[rank],tails+(maximum?count:0)*tail()+rank*tail(),unsigned(tail())};
  }
  void set(unsigned rank,unsigned slot,Key low,Key high) noexcept {
    slots[rank]=slot; minima()[rank]=low.prefix; maxima()[rank]=high.prefix;
    if (tail()) {
      auto* tails=reinterpret_cast<std::byte*>(minima()+2*count);
      std::memcpy(tails+rank*tail(),low.tail,tail());
      std::memcpy(tails+(count+rank)*tail(),high.tail,tail());
    }
  }
  std::size_t bytes() const noexcept { return sizeof(Bounds)+count*(16+2*tail()); }
  static Bounds* make(std::pmr::memory_resource& r,unsigned count,unsigned width) {
    const auto n=sizeof(Bounds)+count*(16+2*(std::size_t(width)>8?width-8:0));
    auto* p=new(r.allocate(n,alignof(Bounds))) Bounds{&r,1,count,width};
    for (unsigned i=0;i<2*count;++i) std::construct_at(p->minima()+i,UINT64_C(0));
    return p;
  }
  void retain() const noexcept { ++references; }
  void release() noexcept {
    if (--references) return;
    auto* r=memory; const auto n=bytes(); std::destroy_at(this); r->deallocate(this,n,alignof(Bounds));
  }
};
static_assert(sizeof(Bounds)==56);
// The actual Ikea source handles each local count. Long keys first use its
// native abbreviation search, then its generic ordered source inside ONLY
// the equal-abbreviation run. No linear full-key repair or unique fence.
struct BoundSource {
  const Bounds* bounds;
  bool maximum;
  unsigned size() const noexcept { return bounds->count; }
  Key at(unsigned rank) const noexcept { return bounds->at(rank,maximum); }
  template<ikea::boundlist::edge E> unsigned boundary(const Key& point) const noexcept {
    namespace bl=ikea::boundlist;
    const bl::direct_source<std::uint64_t> abbreviated{maximum?bounds->maxima():bounds->minima(),size()};
    if (!bounds->tail()) return abbreviated.template boundary<E>(point.prefix);
    const auto first=bl::lower_bound(abbreviated,point.prefix),limit=bl::upper_bound(abbreviated,point.prefix);
    if (first==limit) return first;
    struct Full {
      const BoundSource& source; unsigned first,count;
      unsigned size() const noexcept { return count; }
      Key at(unsigned i) const noexcept { return source.at(first+i); }
    } full{*this,first,limit-first};
    return first+(E==bl::edge::at_or_after?bl::lower_bound(full,point):bl::upper_bound(full,point));
  }
};
struct Bundle;
// Per-node lifetime, even when several node bodies/headers share one finite
// allocation. Releasing a dead node immediately releases ITS outgoing edges.
// Keeping a sibling's backing does not keep dead ancestors' dependencies.
struct Proof {
  Bundle* bundle;
  std::size_t references{1};
  unsigned count;
  Bounds* bounds{};
  Proof** children() noexcept { return reinterpret_cast<Proof**>(this+1); }
  void retain() noexcept { ++references; }
  void release() noexcept;
};
static_assert(sizeof(Proof)==32);
struct Bundle {
  std::pmr::memory_resource& memory;
  orbital::allocation::Allocation bytes;
  std::size_t live{1},extent,used{sizeof(Bundle)};
  Bundle(std::pmr::memory_resource& m,orbital::allocation::Allocation a,std::size_t n)
      :memory(m),bytes(std::move(a)),extent(n) {}
  void release() noexcept {
    if (--live) return;
    auto& r=memory; const auto n=extent;
    std::destroy_at(this); r.deallocate(this,n,alignof(Bundle));
  }
  Proof* make(unsigned children) noexcept {
    const auto n=sizeof(Proof)+children*sizeof(Proof*);
    assert(used+n<=extent);
    auto* p=new(reinterpret_cast<std::byte*>(this)+used) Proof{this,1,children};
    for (unsigned i=0;i<children;++i) std::construct_at(p->children()+i,nullptr);
    used+=n; ++live; return p;
  }
};
void Proof::release() noexcept {
  if (--references) return;
  if (bounds) bounds->release();
  for (unsigned i=0;i<count;++i) if (auto* p=children()[i]) p->release();
  auto* b=bundle; std::destroy_at(this); b->release();
}
struct Owner {
  Proof* proof{};
  Owner()=default;
  explicit Owner(Proof* p):proof(p) {}
  Owner(Owner&& other) noexcept :proof(std::exchange(other.proof,nullptr)) {}
  Owner(const Owner& other) noexcept :proof(other.proof) { if (proof) proof->retain(); }
  Owner& operator=(Owner other) noexcept { std::swap(proof,other.proof); return *this; }
  ~Owner() { if (proof) proof->release(); }
  Proof* take() noexcept { return std::exchange(proof,nullptr); }
};
struct Batch {
  Bundle* bundle{};
  explicit Batch(Bundle* p):bundle(p) {}
  Batch(Batch&& other) noexcept :bundle(std::exchange(other.bundle,nullptr)) {}
  ~Batch() { if (bundle) bundle->release(); }
  Owner make(unsigned children,Work& work) {
    work.proof_bytes+=sizeof(Proof)+children*sizeof(Proof*); return Owner(bundle->make(children));
  }
};
struct Slot { const std::byte* bytes{}; std::uint16_t mask{}; std::byte reserved[6]{}; };
struct alignas(64) Node {
  std::uint16_t count{};
  std::uint8_t shift{};
  std::byte reserved0[5]{};
  std::uint32_t low{},high{};
  Proof* proof{};
  const Bounds* bounds{};
  std::uint32_t present{};
  std::byte reserved1[28]{};
  std::array<Slot,32> slots;
};
static_assert(sizeof(Node)==576 && offsetof(Node,slots)==64 && sizeof(Slot)==16);
struct Private { Owner owner; const std::byte* bytes; };
struct State {
  Store& store;
  loom::Worker& worker;
  std::pmr::memory_resource& memory;
  std::uint64_t space;
  material::Definition node;
  const std::optional<KeyShape> shape;
  State(Store& s,loom::Worker& w,std::pmr::memory_resource& m,std::uint64_t ns,std::optional<KeyShape> k)
      :store(s),worker(w),memory(m),space(ns),shape(k) {}
  orbital::Result<Batch> allocate_metadata(std::size_t bytes,std::size_t proof_bytes,Work& work) {
    auto allocation=loom::allocate(worker,{.bytes=bytes,.alignment=64});
    if (!allocation) return std::unexpected(allocation.error());
    ++work.allocations; work.body_bytes+=bytes;
    const auto n=sizeof(Bundle)+proof_bytes;
    auto* raw=memory.allocate(n,alignof(Bundle));
    return Batch(new(raw) Bundle(memory,std::move(*allocation),n));
  }
  orbital::Result<Batch> allocate(std::size_t bytes,unsigned nodes,bool group,Work& work) {
    return allocate_metadata(bytes,nodes*(sizeof(Proof)+32*sizeof(Proof*))+(group?sizeof(Proof):0),work);
  }
  orbital::Result<Store::Owner> finish(Private root,std::uint64_t name,Work& work) {
    // One actual Orbital capture for the published root. All private nodes are
    // physical interpretation dependencies, not separately versioned Objects.
    auto backing=std::allocate_shared<Owner>(std::pmr::polymorphic_allocator<Owner>(&memory),std::move(root.owner));
    const auto c=node.control(name,orbital::PartOwners::independent_shared);
    const std::array sources{orbital::PartSource{c.id(0),{root.bytes,sizeof(Node)},std::move(backing)}};
    ++work.captures; return store.capture(c,sources);
  }
  struct Group { std::uint32_t prefix; std::uint16_t mask; Private physical; };
  orbital::Result<Private> tree(std::span<Group> input,unsigned shift,Work& work) {
    auto batch=allocate(sizeof(Node),1,false,work);
    if (!batch) return std::unexpected(batch.error());
    auto proof=batch->make(32,work);
    auto* body=new(batch->bundle->bytes.address) Node{};
    body->proof=proof.proof; body->shift=shift;
    body->low=input.front().prefix; body->high=input.back().prefix;
    for (std::size_t first=0;first<input.size();) {
      const auto slot=(input[first].prefix>>shift)&31;
      auto last=first+1;
      while (last<input.size() && ((input[last].prefix>>shift)&31)==slot) ++last;
      Private child;
      if (shift) {
        auto result=tree(input.subspan(first,last-first),shift-5,work);
        if (!result) return std::unexpected(result.error());
        child=std::move(*result);
      } else { child=std::move(input[first].physical); body->slots[slot].mask=input[first].mask; }
      body->slots[slot].bytes=child.bytes;
      proof.proof->children()[slot]=child.owner.take(); body->present|=UINT32_C(1)<<slot;
      ++body->count; first=last;
    }
    if (shape) {
      auto* b=Bounds::make(memory,body->count,shape->bytes); proof.proof->bounds=b; body->bounds=b;
      unsigned rank=0;
      for (unsigned slot=0;slot<32;++slot) if (auto* child=proof.proof->children()[slot]) {
        auto* bounds=child->bounds; b->set(rank++,slot,bounds->at(0,false),bounds->at(bounds->count-1,true));
      }
    }
    return Private{std::move(proof),reinterpret_cast<const std::byte*>(body)};
  }
  orbital::Result<Private> edit(const Node* before,std::span<const LocatorChange> input,Work& work) {
    auto batch=allocate(sizeof(Node),1,false,work);
    if (!batch) return std::unexpected(batch.error());
    auto proof=batch->make(32,work);
    if (auto* b=before->bounds) { b->retain(); proof.proof->bounds=const_cast<Bounds*>(b); }
    auto* body=reinterpret_cast<Node*>(batch->bundle->bytes.address);
    std::memcpy(body,before,sizeof(Node)); work.copied_bytes+=sizeof(Node);
    body->proof=proof.proof;
    std::size_t first=0;
    for (unsigned slot=0;slot<32;++slot) {
      auto last=first;
      while (last<input.size() && (((input[last].prefix>>4)>>body->shift)&31)==slot) ++last;
      if (last==first) {
        if (auto* p=before->proof->children()[slot]) {
          p->retain(); proof.proof->children()[slot]=p; ++work.child_retains;
        }
        continue;
      }
      Private child;
      if (!before->slots[slot].bytes) return std::unexpected(orbital::Error::invalid_argument);
      if (body->shift) {
        auto result=edit(reinterpret_cast<const Node*>(before->slots[slot].bytes),input.subspan(first,last-first),work);
        if (!result) return std::unexpected(result.error());
        child=std::move(*result);
      } else {
        const unsigned count=std::popcount(body->slots[slot].mask);
        for (auto i=first;i<last;++i)
          if (!(body->slots[slot].mask&(1u<<(input[i].prefix&15)))) return std::unexpected(orbital::Error::invalid_argument);
        auto group=allocate(count*64,0,true,work);
        if (!group) return std::unexpected(group.error());
        child={group->make(0,work),group->bundle->bytes.address};
        if (auto* b=before->proof->children()[slot]->bounds) { b->retain(); child.owner.proof->bounds=b; }
        auto* bytes=group->bundle->bytes.address;
        std::memcpy(bytes,before->slots[slot].bytes,count*64); work.copied_bytes+=count*64;
        for (auto i=first;i<last;++i) {
          const unsigned rank=std::popcount(body->slots[slot].mask&((1u<<(input[i].prefix&15))-1));
          std::memcpy(bytes+rank*64,&input[i].value,64);
        }
      }
      body->slots[slot].bytes=child.bytes;
      proof.proof->children()[slot]=child.owner.take(); first=last;
    }
    assert(first==input.size());
    return Private{std::move(proof),reinterpret_cast<const std::byte*>(body)};
  }
};
}
static_assert(sizeof(detail::State)==56 && sizeof(Cursor)==168 && sizeof(Entry)==104 && sizeof(Range)==56);
namespace detail {
bool valid_prefix(unsigned layer,std::uint32_t prefix) noexcept {
  return layer<=4 && (layer==4 || std::uint64_t(prefix)<(UINT64_C(1)<<(8*layer)));
}
orbital::Result<void> admit_entries(unsigned layer,std::span<const Entry> input,std::optional<KeyShape> shape) noexcept {
  using orbital::Error;
  if (layer>4) return std::unexpected(Error::invalid_argument);
  for (std::size_t i=0;i<input.size();++i) {
    const auto& e=input[i];
    if (!valid_prefix(layer,e.prefix) || (i && input[i-1].prefix>=e.prefix))
      return std::unexpected(Error::invalid_argument);
    if (!shape) {
      if (!e.minimum.empty() || !e.maximum.empty()) return std::unexpected(Error::invalid_argument);
      continue;
    }
    if (e.minimum.size()!=shape->bytes || e.maximum.size()!=shape->bytes ||
        key(e.maximum)<key(e.minimum) || (i && key(e.minimum)<key(input[i-1].maximum)))
      return std::unexpected(Error::invalid_argument);
    for (unsigned byte=0;byte<std::min(layer,shape->shared);++byte) {
      const auto expected=std::byte(e.prefix>>(8*(layer-1-byte)));
      if (e.minimum[byte]!=expected || e.maximum[byte]!=expected) return std::unexpected(Error::invalid_argument);
    }
  }
  return {};
}
}
namespace { using namespace detail; }
const Dirent* Read::find(std::uint32_t prefix) const noexcept {
  auto* node=root_; if (!node) return nullptr;
  const auto group=prefix>>4;
  if (group<node->low || group>node->high) return nullptr;
  int shift=node->shift;
  for (;;) {
    const auto& slot=node->slots[(group>>shift)&31];
    if (!slot.bytes) return nullptr;
    if (shift) { node=reinterpret_cast<const detail::Node*>(slot.bytes); shift-=5; continue; }
    const auto bit=1u<<(prefix&15);
    if (!(slot.mask&bit)) return nullptr;
    const auto rank=std::popcount(slot.mask&(bit-1));
    return reinterpret_cast<const Dirent*>(slot.bytes)+rank;
  }
}
orbital::Result<void> Read::find_many(std::span<const std::uint32_t> keys,
    std::span<const Dirent*> output) const noexcept {
  if (output.size()<keys.size()) return std::unexpected(orbital::Error::capacity);
  if (!root_) { std::fill_n(output.begin(),keys.size(),nullptr); return {}; }
  struct Lane { const detail::Node* node; std::uint32_t key; };
  static_assert(sizeof(Lane)==16);
  // Demand-load one whole independent bank per routing level, then compute
  // original suffix rank. Caller owns the next locator/material stages under
  // the same Scope. No prefetch, catalog re-selection or reader protocol here.
  for (std::size_t first=0;first<keys.size();first+=32) {
    std::array<Lane,32> bank;
    const unsigned count=std::min<std::size_t>(32,keys.size()-first);
    for (unsigned i=0;i<count;++i) {
      const auto key=keys[first+i],group=key>>4;
      bank[i]={group>=root_->low && group<=root_->high?root_:nullptr,key};
    }
    for (int shift=root_->shift;shift>0;shift-=5)
      for (unsigned i=0;i<count;++i) if (auto* node=bank[i].node)
        bank[i].node=reinterpret_cast<const detail::Node*>(node->slots[(bank[i].key>>(shift+4))&31].bytes);
    for (unsigned i=0;i<count;++i) {
      const Dirent* result=nullptr;
      if (auto* node=bank[i].node) {
        const auto& slot=node->slots[(bank[i].key>>4)&31];
        const auto bit=1u<<(bank[i].key&15);
        if (slot.bytes && (slot.mask&bit))
          result=reinterpret_cast<const Dirent*>(slot.bytes)+std::popcount(slot.mask&(bit-1));
      }
      output[first+i]=result;
    }
  }
  return {};
}
namespace {
constexpr unsigned has_low=1,has_high=2,low_closed=4,high_closed=8,pending_low=16;
}
unsigned Cursor::first(const detail::Bounds& bounds) const noexcept {
  if ((flags_&(has_low|pending_low))!=(has_low|pending_low)) return 0;
  if (bounds.width<=8) {
    const ikea::boundlist::direct_source<std::uint64_t> maxima{bounds.maxima(),bounds.count};
    return flags_&low_closed?ikea::boundlist::lower_bound(maxima,low_.word):ikea::boundlist::upper_bound(maxima,low_.word);
  }
  const detail::BoundSource maxima{&bounds,true}; const detail::Key point{low_.word,low_.tail,tail_bytes_};
  return flags_&low_closed?ikea::boundlist::lower_bound(maxima,point):ikea::boundlist::upper_bound(maxima,point);
}
bool Cursor::past_high(const detail::Bounds& bounds,unsigned rank) const noexcept {
  if (!(flags_&has_high)) return false;
  if (bounds.width<=8) return flags_&high_closed?high_.word<bounds.minima()[rank]:high_.word<=bounds.minima()[rank];
  const auto minimum=bounds.at(rank,false); const detail::Key high{high_.word,high_.tail,tail_bytes_};
  return flags_&high_closed?high<minimum:!(minimum<high);
}
void Cursor::setup(const detail::Node* root,detail::Key low,detail::Key high,unsigned flags) noexcept {
  low_={low.prefix,low.tail}; high_={high.prefix,high.tail}; tail_bytes_=std::max(low.size,high.size); flags_=flags|pending_low;
  depth_=0; rank_=limit_=0; group_=nullptr; bounds_=nullptr;
  if (root && push(root)) advance();
}
bool Cursor::push(const detail::Node* node) noexcept {
  const auto rank=first(*node->bounds);
  if (rank==node->bounds->count) return false;
  const auto slot=node->bounds->slots[rank];
  assert(depth_<frames_.size()); frames_[depth_++]={node,node->present&(UINT32_MAX<<slot),rank}; return true;
}
bool Cursor::advance() noexcept {
  while (depth_) {
    auto& frame=frames_[depth_-1];
    if (!frame.remaining) { --depth_; continue; }
    const auto* node=frame.node;
    if (past_high(*node->bounds,frame.rank)) { depth_=0; return false; }
    const unsigned slot=std::countr_zero(frame.remaining); frame.remaining&=frame.remaining-1; ++frame.rank;
    if (node->shift) { push(reinterpret_cast<const detail::Node*>(node->slots[slot].bytes)); continue; }
    const auto* bounds=node->proof->children()[slot]->bounds;
    const auto rank=first(*bounds);
    if (rank==bounds->count) continue;
    if (past_high(*bounds,rank)) { depth_=0; return false; }
    group_=reinterpret_cast<const Dirent*>(node->slots[slot].bytes); bounds_=bounds;
    group_index_=(node->low&~31u)|slot; rank_=rank; limit_=bounds->count;
    return true;
  }
  return false;
}
bool Cursor::next(EntryRef& output) noexcept {
  if (rank_==limit_ && !advance()) return false;
  if (past_high(*bounds_,rank_)) { depth_=0; rank_=limit_; return false; }
  output={(group_index_<<4)|bounds_->slots[rank_],group_+rank_}; ++rank_; flags_&=~pending_low; return true;
}
std::size_t Cursor::visit(std::span<EntryRef> output) noexcept {
  std::size_t count=0;
  while (count<output.size() && next(output[count])) ++count;
  return count;
}
orbital::Result<void> LogicalRead::equal(std::span<const std::byte> value,Cursor& cursor) const noexcept {
  if (value.size()!=shape_.bytes) return std::unexpected(orbital::Error::invalid_argument);
  const auto point=detail::key(value);
  cursor.setup(root_,point,point,has_low|has_high|low_closed|high_closed); return {};
}
orbital::Result<void> LogicalRead::seek(Range range,Cursor& cursor) const noexcept {
  if ((range.low && range.low->size()!=shape_.bytes) || (range.high && range.high->size()!=shape_.bytes))
    return std::unexpected(orbital::Error::invalid_argument);
  const auto low=range.low?detail::key(*range.low):detail::Key{};
  const auto high=range.high?detail::key(*range.high):detail::Key{};
  bool empty=false;
  if (range.low && range.high)
    empty=high<low || (!(low<high) && (!range.low_closed || !range.high_closed));
  const unsigned flags=(range.low?has_low:0)|(range.high?has_high:0)|(range.low_closed?low_closed:0)|(range.high_closed?high_closed:0);
  cursor.setup(empty?nullptr:root_,low,high,flags); return {};
}
Directory::Directory(std::unique_ptr<detail::State> s):state_(std::move(s)) {}
Directory::~Directory()=default;
orbital::Result<std::unique_ptr<Directory>> Directory::open(material::Store& store,
    loom::Worker& worker,std::pmr::memory_resource& memory,std::uint64_t ns,std::optional<KeyShape> shape) {
  if (!ns || (shape && (shape->shared>6 || shape->shared>shape->bytes)))
    return std::unexpected(orbital::Error::invalid_argument);
  try {
    auto s=std::make_unique<detail::State>(store,worker,memory,ns,shape);
    const std::array geometry{material::Geometry{sizeof(detail::Node)}};
    auto node=material::Definition::native(memory,ns,0,geometry);
    if (!node) return std::unexpected(node.error());
    s->node=std::move(*node);
    return std::unique_ptr<Directory>(new Directory(std::move(s)));
  } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<std::unique_ptr<HashLayer>> Directory::hashed(unsigned layer) {
  if (!state_->shape || layer<2 || layer>4 || layer>state_->shape->shared)
    return std::unexpected(orbital::Error::invalid_argument);
  return HashLayer::open(state_->store,state_->worker,state_->memory,state_->space,*state_->shape,layer);
}
orbital::ObjectId Directory::root_id(unsigned layer) const noexcept { return {state_->space,std::uint64_t(layer)+1}; }
orbital::Result<material::Store::Owner> Directory::build(unsigned layer,std::span<const Entry> input,Work* observed) {
  using orbital::Error;
  if (layer>4 || input.empty()) return std::unexpected(Error::invalid_argument);
  if (auto admission=detail::admit_entries(layer,input,state_->shape);!admission)
    return std::unexpected(admission.error());
  Work unused; auto& work=observed?*observed:unused;
  try {
    std::pmr::vector<detail::State::Group> groups(&state_->memory);
    groups.reserve((input.size()+15)/16);
    for (std::size_t first=0;first<input.size();) {
      auto last=first+1;
      while (last<input.size() && (input[last].prefix>>4)==(input[first].prefix>>4)) ++last;
      const unsigned count=last-first;
      auto batch=state_->allocate(count*64,0,true,work);
      if (!batch) return std::unexpected(batch.error());
      auto proof=batch->make(0,work);
      if (state_->shape) {
        auto* bounds=detail::Bounds::make(state_->memory,count,state_->shape->bytes); proof.proof->bounds=bounds;
        for (unsigned i=0;i<count;++i)
          bounds->set(i,input[first+i].prefix&15,detail::key(input[first+i].minimum),detail::key(input[first+i].maximum));
      }
      std::uint16_t mask=0;
      for (auto i=first;i<last;++i) {
        mask|=std::uint16_t(1u<<(input[i].prefix&15));
        std::memcpy(batch->bundle->bytes.address+(i-first)*64,&input[i].value,64);
      }
      groups.push_back({input[first].prefix>>4,mask,{std::move(proof),batch->bundle->bytes.address}}); first=last;
    }
    const auto bits=std::bit_width(groups.front().prefix^groups.back().prefix);
    auto root=state_->tree(groups,bits?((bits-1)/5)*5:0,work);
    if (!root) return std::unexpected(root.error());
    return state_->finish(std::move(*root),root_id(layer).low,work);
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
orbital::Result<Read> Directory::read(const material::Store::Scope& basis,unsigned layer) const noexcept {
  if (layer>4) return std::unexpected(orbital::Error::invalid_argument);
  const auto selected=state_->store.bound(basis,root_id(layer));
  if (!selected) {
    if (selected.error()==orbital::Error::absent) return Read(nullptr);
    return std::unexpected(selected.error());
  }
  const auto s=selected->selection();
  if (s.control().layout!=state_->node.get()) return std::unexpected(orbital::Error::invalid_argument);
  return Read(reinterpret_cast<const detail::Node*>(s.part(0)->address()));
}
orbital::Result<LogicalRead> Directory::logical(const material::Store::Scope& basis,unsigned layer) const noexcept {
  if (!state_->shape) return std::unexpected(orbital::Error::invalid_argument);
  auto selected=read(basis,layer);
  if (!selected) return std::unexpected(selected.error());
  return LogicalRead(selected->root_,*state_->shape);
}
orbital::Result<material::Store::Owner> Directory::replace(const material::Store::Scope& basis,
    unsigned layer,std::uint32_t prefix,const Dirent& replacement,Work* observed) {
  using orbital::Error;
  if (!detail::valid_prefix(layer,prefix)) return std::unexpected(Error::invalid_argument);
  auto read=this->read(basis,layer);
  if (!read) return std::unexpected(read.error());
  if (!read->root_ || (prefix>>4)<read->root_->low || (prefix>>4)>read->root_->high)
    return std::unexpected(Error::invalid_argument);
  Work unused; auto& work=observed?*observed:unused;
  try {
    std::array<const detail::Node*,6> path;
    std::array<unsigned,6> slots;
    unsigned depth=0;
    const auto group=prefix>>4; const auto* node=read->root_;
    for (;;) {
      const unsigned slot=(group>>node->shift)&31;
      path[depth]=node; slots[depth++]=slot;
      if (!node->slots[slot].bytes) return std::unexpected(Error::invalid_argument);
      if (!node->shift) break;
      node=reinterpret_cast<const detail::Node*>(node->slots[slot].bytes);
    }
    const auto& old_group=node->slots[slots[depth-1]];
    if (!(old_group.mask&(1u<<(prefix&15)))) return std::unexpected(Error::invalid_argument);
    const unsigned count=std::popcount(old_group.mask);
    const unsigned rank=std::popcount(old_group.mask&((1u<<(prefix&15))-1));
    auto batch=state_->allocate(count*64+depth*sizeof(detail::Node),depth,true,work);
    if (!batch) return std::unexpected(batch.error());
    auto* bytes=batch->bundle->bytes.address;
    std::memcpy(bytes,old_group.bytes,count*64); work.copied_bytes+=count*64;
    std::memcpy(bytes+rank*64,&replacement,64);
    auto child=batch->make(0,work); bytes+=count*64;
    if (auto* b=node->proof->children()[slots[depth-1]]->bounds) { b->retain(); child.proof->bounds=b; }
    for (unsigned d=depth;d--;) {
      auto proof=batch->make(32,work);
      if (auto* b=path[d]->bounds) { b->retain(); proof.proof->bounds=const_cast<detail::Bounds*>(b); }
      for (unsigned slot=0;slot<32;++slot) {
        if (slot==slots[d]) proof.proof->children()[slot]=child.take();
        else if (auto* p=path[d]->proof->children()[slot]) {
          p->retain(); proof.proof->children()[slot]=p; ++work.child_retains;
        }
      }
      auto* body=reinterpret_cast<detail::Node*>(bytes);
      std::memcpy(body,path[d],sizeof(detail::Node)); work.copied_bytes+=sizeof(detail::Node);
      body->proof=proof.proof;
      body->slots[slots[d]].bytes=d==depth-1?batch->bundle->bytes.address:bytes-sizeof(detail::Node);
      child=std::move(proof); bytes+=sizeof(detail::Node);
    }
    return state_->finish({std::move(child),bytes-sizeof(detail::Node)},root_id(layer).low,work);
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
orbital::Result<material::Store::Owner> Directory::replace_many(const material::Store::Scope& basis,
    unsigned layer,std::span<const LocatorChange> input,Work* observed) {
  using orbital::Error;
  if (input.empty() || layer>4) return std::unexpected(Error::invalid_argument);
  if (input.size()==1) return replace(basis,layer,input[0].prefix,input[0].value,observed);
  auto read=this->read(basis,layer);
  if (!read) return std::unexpected(read.error());
  for (std::size_t i=0;i<input.size();++i)
    if (!detail::valid_prefix(layer,input[i].prefix) || (i && input[i-1].prefix>=input[i].prefix))
      return std::unexpected(Error::invalid_argument);
  if (!read->root_ || (input.front().prefix>>4)<read->root_->low ||
      (input.back().prefix>>4)>read->root_->high) return std::unexpected(Error::invalid_argument);
  Work unused; auto& work=observed?*observed:unused;
  try {
    auto result=state_->edit(read->root_,input,work);
    if (!result) return std::unexpected(result.error());
    return state_->finish(std::move(*result),root_id(layer).low,work);
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
}
