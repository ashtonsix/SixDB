#include <engine/directory_hash.h>
#include "directory_admission.h"
#include "material_internal.h"
#include <loom/worker.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace engine::directory {
namespace {
using Store=material::Store;
using Table=detail::DirectoryTable;
using orbital::Error;
struct AllocationError { Error error; };
constexpr std::uint32_t root_rep=0x48525431,group_rep=0x48475031;
constexpr std::uint64_t magic=UINT64_C(0x3148534148524944);
orbital::ObjectId group_id(std::uint64_t ns,unsigned layer,std::uint32_t group) noexcept {
  return {ns,(UINT64_C(1)<<63)|(std::uint64_t(layer)<<32)|group};
}
struct alignas(64) Header {
  std::uint64_t signature{magic},space{},entries{},groups{};
  std::uint32_t layer{},width{},shared{};
  std::byte reserved[20]{};
};
static_assert(sizeof(Header)==64);
struct Image {
  detail::HashStorage::context context;
  Table table;
  const std::uint64_t space;
  const unsigned layer,group;
  Image(loom::Worker& w,std::uint64_t ns,unsigned l,unsigned g)
      :context{&w},table(0,&context),space(ns),layer(l),group(g) {}
  explicit Image(const Image& old):context(old.context),table(old.table.clone(&context)),space(old.space),layer(old.layer),group(old.group) {}
};
static_assert(sizeof(Table)==192 && sizeof(Image)==216 && sizeof(HashRead)==160);
struct Body { const std::byte* bytes{}; std::size_t size{}; };
Body body(const Table& table) noexcept {
  Body out;
  table.allocations([&](const char*,const std::uint8_t* p,std::size_t n) { out={reinterpret_cast<const std::byte*>(p),n}; });
  return out; // W4 bitmap owns exactly one primary allocation, no Swiss storage.
}
orbital::Result<const Image*> image(material::Selection selected,std::uint64_t ns,unsigned layer,std::uint32_t group) noexcept {
  auto* layout=selected.control().layout;
  if (layout->kind!=material::detail::Kind::hash || layout->representation!=group_rep ||
      layout->object_namespace!=ns || !layout->interpretation || layout->parts.size()!=1)
    return std::unexpected(Error::invalid_argument);
  auto* value=static_cast<const Image*>(layout->interpretation.get());
  const auto actual=body(value->table); auto part=selected.part(0);
  if (value->space!=ns || value->layer!=layer || value->group!=group || !part ||
      part->address()!=actual.bytes || layout->parts[0].bytes!=actual.size)
    return std::unexpected(Error::invalid_argument);
  return value;
}
}
namespace detail {
HashStorage::buffer::buffer(std::size_t n,context* memory,bool zero,std::size_t reserve) {
  if (!n && !reserve) return;
  if (!memory || !memory->worker || reserve>n || n>std::numeric_limits<std::size_t>::max()-63)
    throw AllocationError{Error::invalid_argument};
  n=(n+63)&~std::size_t(63);
  auto result=loom::allocate(*memory->worker,{.bytes=n,.alignment=64});
  if (!result) throw AllocationError{result.error()};
  allocation=std::move(*result); data=reinterpret_cast<std::uint8_t*>(allocation.address); size=n;
  if (zero) std::memset(data,0,size);
}
HashStorage::buffer::buffer(buffer&& other) noexcept
    :allocation(std::move(other.allocation)),data(std::exchange(other.data,nullptr)),size(std::exchange(other.size,0)) {}
HashStorage::buffer& HashStorage::buffer::operator=(buffer&& other) noexcept {
  allocation=std::move(other.allocation); data=std::exchange(other.data,nullptr); size=std::exchange(other.size,0); return *this;
}
std::size_t HashStorage::buffer::reserved_bytes() const noexcept { return allocation.size; }
ikea::hashtable::storage_usage HashStorage::buffer::usage() const noexcept { return {size,allocation.size,allocation.size}; }
void HashStorage::buffer::grow(std::size_t,std::size_t,bool) { throw AllocationError{Error::capacity}; }
HashStorage::trim_result HashStorage::buffer::relinquish_tail(std::size_t n) noexcept {
  if (n!=size) return std::unexpected(std::errc::invalid_argument);
  return {};
}
}
struct HashLayer::State {
  Store& store; loom::Worker& worker; std::pmr::memory_resource& memory;
  std::uint64_t space; KeyShape shape; unsigned layer;
  material::Definition root;
  orbital::Result<Store::Owner> capture(std::shared_ptr<Image> value) {
    const auto bytes=body(value->table);
    const std::array geometry{material::Geometry{bytes.size}};
    auto definition=material::detail::Access::make(memory,space,group_rep,material::detail::Kind::hash,geometry,value);
    if (!definition) return std::unexpected(definition.error());
    const auto control=definition->control(group_id(space,layer,value->group).low,orbital::PartOwners::independent_shared);
    const std::array sources{orbital::PartSource{control.id(0),{bytes.bytes,bytes.size},std::move(value)}};
    return store.capture(control,sources);
  }
};
HashLayer::HashLayer(std::unique_ptr<State> s):state_(std::move(s)) {}
HashLayer::~HashLayer()=default;
orbital::Result<std::unique_ptr<HashLayer>> HashLayer::open(Store& store,loom::Worker& worker,
    std::pmr::memory_resource& memory,std::uint64_t ns,KeyShape shape,unsigned layer) {
  try {
    const std::array geometry{material::Geometry{64}};
    auto root=material::detail::Access::make(memory,ns,root_rep,material::detail::Kind::hash,geometry);
    if (!root) return std::unexpected(root.error());
    auto state=std::make_unique<State>(State{store,worker,memory,ns,shape,layer,std::move(*root)});
    return std::unique_ptr<HashLayer>(new HashLayer(std::move(state)));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}
orbital::ObjectId HashLayer::root_id() const noexcept { return {state_->space,state_->layer+1}; }
orbital::Result<std::pmr::vector<Store::Change>> HashLayer::initialize(const Store::Scope& basis,std::span<const Entry> entries) {
  if (auto admission=detail::admit_entries(state_->layer,entries,state_->shape);!admission) return std::unexpected(admission.error());
  auto existing=state_->store.bound(basis,root_id());
  if (existing) return std::unexpected(Error::invalid_argument);
  if (existing.error()!=Error::absent) return std::unexpected(existing.error());
  try {
    std::pmr::vector<Store::Change> changes(&state_->memory);
    for (std::size_t first=0;first<entries.size();) {
      const auto group=entries[first].prefix>>4;
      if (basis.bind(group_id(state_->space,state_->layer,group))) return std::unexpected(Error::invalid_argument);
      auto value=std::allocate_shared<Image>(std::pmr::polymorphic_allocator<Image>(&state_->memory),state_->worker,state_->space,state_->layer,group);
      auto last=first;
      do {
        Table::value_type bytes; std::memcpy(bytes.data(),&entries[last].value,64);
        value->table.insert(entries[last].prefix&15,bytes); ++last;
      } while (last<entries.size() && entries[last].prefix>>4==group);
      auto owner=state_->capture(std::move(value)); if (!owner) return std::unexpected(owner.error());
      changes.push_back({group_id(state_->space,state_->layer,group),std::move(*owner)}); first=last;
    }
    auto allocation=loom::allocate(state_->worker,{.bytes=64,.alignment=64});
    if (!allocation) return std::unexpected(allocation.error());
    new(allocation->address) Header{.space=state_->space,.entries=entries.size(),.groups=changes.size(),.layer=state_->layer,.width=state_->shape.bytes,.shared=state_->shape.shared};
    const auto control=state_->root.control(root_id().low);
    const std::array source{orbital::PartSource{control.id(0),loom::bytes(*allocation),{},&*allocation}};
    auto root=state_->store.capture(control,source); if (!root) return std::unexpected(root.error());
    changes.push_back({root_id(),std::move(*root)}); return changes;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    catch (const AllocationError& e) { return std::unexpected(e.error); }
}
orbital::Result<void> HashLayer::read(const Store::Scope& basis,HashRead& read) const noexcept {
  auto selected=state_->store.bound(basis,root_id());
  bool empty=true;
  if (!selected) { if (selected.error()!=Error::absent) return std::unexpected(selected.error()); }
  else {
    const auto s=selected->selection(); auto* layout=s.control().layout;
    if (layout->kind!=material::detail::Kind::hash || layout->representation!=root_rep || layout->parts.size()!=1)
      return std::unexpected(Error::invalid_argument);
    auto part=s.part(0); if (!part || layout->parts[0].bytes!=64) return std::unexpected(Error::invalid_argument);
    auto* h=reinterpret_cast<const Header*>(part->address());
    if (h->signature!=magic || h->space!=state_->space || h->layer!=state_->layer ||
        h->width!=state_->shape.bytes || h->shared!=state_->shape.shared) return std::unexpected(Error::invalid_argument);
    empty=!h->entries;
  }
  read.scope_=&basis; read.space_=state_->space; read.layer_=state_->layer; read.next_=0; read.empty_=empty;
  read.cache_.fill({}); return {};
}
orbital::Result<HashGroup> HashRead::bind(std::uint32_t group) const noexcept {
  if (!scope_ || !detail::valid_prefix(layer_,group<<4) || group>(UINT32_MAX>>4))
    return std::unexpected(Error::invalid_argument);
  if (empty_) return HashGroup(nullptr);
  const auto selected=scope_->bind(group_id(space_,layer_,group)); if (!selected) return HashGroup(nullptr);
  auto value=image(selected,space_,layer_,group); if (!value) return std::unexpected(value.error());
  return HashGroup(&(*value)->table);
}
orbital::Result<std::pmr::vector<Store::Change>> HashLayer::replace(const Store::Scope& basis,std::span<const LocatorChange> input) {
  for (std::size_t i=0;i<input.size();++i)
    if (!detail::valid_prefix(state_->layer,input[i].prefix) || (i && input[i-1].prefix>=input[i].prefix)) return std::unexpected(Error::invalid_argument);
  HashRead read; if (auto admitted=this->read(basis,read);!admitted) return std::unexpected(admitted.error());
  try {
    std::pmr::vector<Store::Change> changes(&state_->memory);
    for (std::size_t first=0;first<input.size();) {
      const auto group=input[first].prefix>>4;
      const auto selected=basis.bind(group_id(state_->space,state_->layer,group));
      if (!selected || read.empty_) return std::unexpected(Error::invalid_argument);
      auto previous=image(selected,state_->space,state_->layer,group); if (!previous) return std::unexpected(previous.error());
      auto value=std::allocate_shared<Image>(std::pmr::polymorphic_allocator<Image>(&state_->memory),**previous);
      auto last=first;
      do {
        const auto key=input[last].prefix&15;
        if (!value->table.get(key).found) return std::unexpected(Error::invalid_argument);
        Table::value_type bytes; std::memcpy(bytes.data(),&input[last].value,64); value->table.insert(key,bytes); ++last;
      } while (last<input.size() && input[last].prefix>>4==group);
      auto owner=state_->capture(std::move(value)); if (!owner) return std::unexpected(owner.error());
      changes.push_back({group_id(state_->space,state_->layer,group),std::move(*owner)}); first=last;
    }
    return changes;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    catch (const AllocationError& e) { return std::unexpected(e.error); }
}
}
