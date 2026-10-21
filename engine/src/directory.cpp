#include <engine/directory.h>
#include <loom/worker.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <vector>

namespace engine::directory {
namespace detail {
using Store=material::Store;
struct Bundle;
// Per-node lifetime, even when several node bodies/headers share one finite
// allocation. Releasing a dead node immediately releases ITS outgoing edges.
// Keeping a sibling's backing does not keep dead ancestors' dependencies.
struct Proof {
  Bundle* bundle;
  std::size_t references{1};
  unsigned count;
  Proof** children() noexcept { return reinterpret_cast<Proof**>(this+1); }
  void retain() noexcept { ++references; }
  void release() noexcept;
};
static_assert(sizeof(Proof)==24);
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
  std::byte reserved1[40]{};
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
  State(Store& s,loom::Worker& w,std::pmr::memory_resource& m,std::uint64_t ns)
      :store(s),worker(w),memory(m),space(ns) {}
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
      proof.proof->children()[slot]=child.owner.take(); ++body->count; first=last;
    }
    return Private{std::move(proof),reinterpret_cast<const std::byte*>(body)};
  }
  orbital::Result<Private> edit(const Node* before,std::span<const Entry> input,Work& work) {
    auto batch=allocate(sizeof(Node),1,false,work);
    if (!batch) return std::unexpected(batch.error());
    auto proof=batch->make(32,work);
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
namespace {
using namespace detail;
bool admitted(unsigned layer,std::uint32_t prefix) noexcept {
  return layer<=4 && (layer==4 || std::uint64_t(prefix)<(UINT64_C(1)<<(8*layer)));
}
}
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
Directory::Directory(std::unique_ptr<detail::State> s):state_(std::move(s)) {}
Directory::~Directory()=default;
orbital::Result<std::unique_ptr<Directory>> Directory::open(material::Store& store,
    loom::Worker& worker,std::pmr::memory_resource& memory,std::uint64_t ns) {
  if (!ns) return std::unexpected(orbital::Error::invalid_argument);
  try {
    auto s=std::make_unique<detail::State>(store,worker,memory,ns);
    const std::array geometry{material::Geometry{sizeof(detail::Node)}};
    auto node=material::Definition::native(memory,ns,0,geometry);
    if (!node) return std::unexpected(node.error());
    s->node=std::move(*node);
    return std::unique_ptr<Directory>(new Directory(std::move(s)));
  } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::ObjectId Directory::root_id(unsigned layer) const noexcept { return {state_->space,std::uint64_t(layer)+1}; }
orbital::Result<material::Store::Owner> Directory::build(unsigned layer,std::span<const Entry> input,Work* observed) {
  using orbital::Error;
  if (layer>4 || input.empty()) return std::unexpected(Error::invalid_argument);
  for (std::size_t i=0;i<input.size();++i)
    if (!admitted(layer,input[i].prefix) || (i && input[i-1].prefix>=input[i].prefix))
      return std::unexpected(Error::invalid_argument);
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
orbital::Result<material::Store::Owner> Directory::replace(const material::Store::Scope& basis,
    unsigned layer,std::uint32_t prefix,const Dirent& replacement,Work* observed) {
  using orbital::Error;
  if (!admitted(layer,prefix)) return std::unexpected(Error::invalid_argument);
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
    for (unsigned d=depth;d--;) {
      auto proof=batch->make(32,work);
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
    unsigned layer,std::span<const Entry> input,Work* observed) {
  using orbital::Error;
  if (input.empty() || layer>4) return std::unexpected(Error::invalid_argument);
  if (input.size()==1) return replace(basis,layer,input[0].prefix,input[0].value,observed);
  auto read=this->read(basis,layer);
  if (!read) return std::unexpected(read.error());
  for (std::size_t i=0;i<input.size();++i)
    if (!admitted(layer,input[i].prefix) || (i && input[i-1].prefix>=input[i].prefix))
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
