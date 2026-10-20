#include "group_fixture.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace index_spike {
namespace {
void admit(bool good,const char* why) {
  if (good) return;
  std::fprintf(stderr,"group producer admission: %s\n",why); std::abort();
}
template<class T> T setup(orbital::Result<T> result) {
  admit(bool(result),"actual module refused initial material");
  return std::move(*result);
}
}
orbital::Result<Store::Owner> GroupFixture::finish(std::uint64_t name,
    const Format& format,orbital::allocation::Allocation& allocation) {
  const Control control{&format,name,0,1};
  const std::array sources{orbital::PartSource{control.id(0),
      loom::bytes(allocation),{},&allocation}};
  return store_.capture(control,sources);
}
GroupFixture::GroupFixture(loom::Worker& worker,std::span<const Record> input,
    std::pmr::memory_resource* metadata,bool graph,unsigned directory_grain)
    : worker_(worker),metadata_(metadata?metadata:&memory_),store_(*metadata_),graph_(graph),
      directory_grain_(directory_grain) {
  static_assert(root_base<=std::numeric_limits<std::uint64_t>::max()-(UINT32_MAX>>4));
  admit(directory_grain==16 || directory_grain==64 || directory_grain==256,"directory allocation grain must be16/64/256");
  admit(graph || directory_grain==16,"stable-name control has only16-dirent allocation grain");
  data_.memory=metadata_;
  for (unsigned n=1;n<=16;++n) groups_[n]={{(graph?0u:64u)+n*64},1,directory_space,nullptr,metadata_};
  std::vector<Store::Change> changes;
  std::vector<GraphGroup> graph_groups;
  changes.reserve(graph_?input.size()+1:input.size()*2);
  if (graph_) for (std::size_t first=0;first<input.size();) {
    struct Group { std::size_t first,last; std::uint16_t mask; };
    std::array<Group,256> groups;
    unsigned group_count=0; std::size_t end=first;
    // Allocation grain,16-position logical group, F32 routing, one-Part
    // publication component and Scope64 cohort are independent choices.
    while (end<input.size()) {
      std::size_t last=end+1;
      while (last<input.size() && (input[last].prefix>>4)==(input[end].prefix>>4)) ++last;
      if (group_count && last-first>directory_grain_) break;
      admit(last-first<=directory_grain_ && last-end<=16,"oversized/duplicate directory group");
      std::uint16_t mask=0;
      for (auto i=end;i<last;++i) {
        admit(!i || input[i-1].prefix<input[i].prefix,"input prefixes not unique and sorted");
        mask|=std::uint16_t(1u<<(input[i].prefix&15));
      }
      admit(std::popcount(mask)==int(last-end),"original directory positions alias");
      groups[group_count++]={end,last,mask}; end=last;
    }
    const auto bytes=(end-first)*64;
    auto directory=setup(loom::allocate(worker_,{.bytes=bytes,.alignment=64}));
    ++initial_directory_allocations_; initial_directory_bytes_+=bytes;
    for (auto i=first;i<end;++i) {
      // Segments retain independent allocations and logical identities.
      auto material=setup(loom::allocate(worker_,{.bytes=64,.alignment=64}));
      idx_asm_copy64(material.address,input[i].material.data());
      auto finished=setup(finish(material_id(input[i].prefix).low,data_,material));
      const auto pointer=setup(finished.selection().part(0)).address();
      auto* d=directory.address+(i-first)*64;
      idx_asm_copy64(d,input[i].descriptor.data()); std::memcpy(d,&pointer,8);
      changes.push_back({material_id(input[i].prefix),std::move(finished)});
    }
    if (group_count==1) {
      const auto& group=groups[0];
      auto owner=setup(finish(root_base+(input[first].prefix>>4),groups_[end-first],directory));
      graph_groups.push_back({input[first].prefix>>4,group.mask,std::move(owner)});
      first=end; continue;
    }
    // Consume the actual array Allocation ONCE. Native subranges keep backing
    // directly; no child retains this temporary Control or Representation.
    const Format array{{std::uint32_t(bytes)},1,directory_space,nullptr,metadata_};
    auto array_owner=setup(finish(0,array,directory));
    const auto source=setup(orbital::source(array_owner.selection(),0));
    for (unsigned g=0;g<group_count;++g) {
      const auto& group=groups[g];
      const auto name=root_base+(input[group.first].prefix>>4);
      const Control control{&groups_[group.last-group.first],name,0,1};
      auto slice=source; slice.id=control.id(0);
      slice.body=source.body.subspan((group.first-first)*64,(group.last-group.first)*64);
      const std::array sources{slice};
      auto owner=setup(store_.capture(control,sources));
      graph_groups.push_back({input[group.first].prefix>>4,group.mask,std::move(owner)});
    }
    first=end;
  }
  else for (std::size_t first=0;first<input.size();) {
    std::size_t last=first;
    while (last<input.size() && (input[last].prefix>>4)==(input[first].prefix>>4)) ++last;
    const auto count=last-first;
    admit(count<=16,"more than16 unique suffix positions");
    const auto& format=groups_[count];
    auto directory=setup(loom::allocate(worker_,{.bytes=format.bytes[0],.alignment=64}));
    ++initial_directory_allocations_; initial_directory_bytes_+=format.bytes[0];
    std::memset(directory.address,0,64);
    std::uint16_t mask=0;
    for (auto i=first;i<last;++i) {
      admit(!i || input[i-1].prefix<input[i].prefix,"input prefixes not unique and sorted");
      const auto& row=input[i];
      mask|=std::uint16_t(1u<<(row.prefix&15));
      auto material=setup(loom::allocate(worker_,{.bytes=64,.alignment=64}));
      idx_asm_copy64(material.address,row.material.data());
      auto finished=setup(finish(material_id(row.prefix).low,data_,material));
      const auto part=setup(finished.selection().part(0));
      auto* destination=directory.address+64+(i-first)*64;
      idx_asm_copy64(destination,row.descriptor.data());
      const auto pointer=std::uintptr_t(part.address());
      std::memcpy(destination,&pointer,8);
      changes.push_back({material_id(row.prefix),std::move(finished)});
    }
    std::memcpy(directory.address,&mask,2);
    admit(std::popcount(mask)==int(count),"rank/body extent differs from original mask");
    const auto name=root_base+(input[first].prefix>>4);
    auto group=setup(finish(name,format,directory));
    changes.push_back({{directory_space,name},std::move(group)});
    first=last;
  }
  if (!graph_groups.empty()) {
    const unsigned bits=std::bit_width(graph_groups.front().index^graph_groups.back().index);
    const unsigned shift=bits?((bits-1)/5)*5:0;
    changes.push_back({{graph_space,root_base},build_graph(graph_groups,shift,root_base)});
  }
  if (!changes.empty()) {
    // This fresh Store has not escaped its constructor; no reader can observe
    // intermediate setup. This is NOT chunking an atomic update request. Keep
    // current quadratic duplicate validation out of memory-sized setup packets.
    constexpr std::size_t setup_batch=1024;
    for (std::size_t first=0;first<changes.size();first+=setup_batch) {
      Store::Scope basis(store_); Store::Prepared packet(store_);
      const auto batch=std::span(changes).subspan(first,
          std::min(setup_batch,changes.size()-first));
      admit(bool(store_.prepare(basis,batch,packet)),"initial packet preparation");
      admit(bool(store_.install(packet)),"initial packet installation");
    }
  }
}
Store::Owner GroupFixture::build_graph(std::span<GraphGroup> groups,unsigned shift,
    std::uint64_t name) {
  auto drop=[](DirectoryDependencies* p) { p->release(); };
  std::unique_ptr<DirectoryDependencies,decltype(drop)> proof(
      DirectoryDependencies::make(*metadata_,graph_space,name),drop);
  ++initial_graph_nodes_;
  auto body=setup(loom::allocate(worker_,{.bytes=576,.alignment=64}));
  std::memset(body.address,0,576);
  const auto stored_shift=std::uint8_t(shift);
  std::memcpy(body.address+2,&stored_shift,1);
  std::memcpy(body.address+8,&groups.front().index,4);
  std::memcpy(body.address+12,&groups.back().index,4);
  std::uint16_t count=0;
  for (std::size_t first=0;first<groups.size();) {
    const auto slot=(groups[first].index>>shift)&31;
    std::size_t last=first+1;
    while (last<groups.size() && ((groups[last].index>>shift)&31)==slot) ++last;
    Store::Owner child;
    if (shift) child=build_graph(groups.subspan(first,last-first),shift-5,next_graph_name_++);
    else {
      admit(last==first+1,"two groups alias one admitted radix slot");
      child=std::move(groups[first].owner);
      std::memcpy(body.address+64+slot*16+8,&groups[first].mask,2);
    }
    const auto pointer=setup(child.selection().part(0)).address();
    std::memcpy(body.address+64+slot*16,&pointer,8);
    proof->children[slot]=std::move(child);
    ++count; first=last;
  }
  std::memcpy(body.address,&count,2);
  return setup(finish(name,proof->format,body));
}
bool GroupFixture::graph_lookup(const Store::Scope& basis,std::uint32_t prefix,
    Lookup& found,GraphPath& path) const {
  auto selected=basis.bind({graph_space,root_base});
  if (!selected) return false;
  auto bytes=setup(selected.part(0)).address();
  std::uint32_t lo,hi;
  std::memcpy(&lo,bytes+8,4); std::memcpy(&hi,bytes+12,4);
  const auto group=prefix>>4;
  if (group<lo || group>hi) return false;
  while (true) {
    const auto control=selected.control();
    auto* proof=control.format->dependencies;
    admit(proof && path.count<path.nodes.size(),"unowned/too-deep physical graph");
    const auto shift=std::to_integer<unsigned>(bytes[2]);
    const auto slot=(group>>shift)&31;
    path.nodes[path.count]=selected; path.slots[path.count++]=slot;
    const auto child=proof->children[slot].selection();
    if (!child) return false;
    const auto* child_bytes=setup(child.part(0)).address();
    if (shift) { selected=child; bytes=child_bytes; continue; }
    std::uint16_t mask; std::memcpy(&mask,bytes+64+slot*16+8,2);
    if (!(mask&(1u<<(prefix&15)))) return false;
    const unsigned rank=std::popcount(mask&((1u<<(prefix&15))-1));
    found={child_bytes+rank*64,child.realization,child.realization,
        root_base+group,rank,rank,0,0};
    return true;
  }
}
orbital::Result<Store::Owner> GroupFixture::copy_graph(const GraphPath& path,Store::Owner child) {
  try {
    for (unsigned i=path.count;i--;) {
      const auto before=path.nodes[i];
      const auto old_control=before.control();
      const auto* old=old_control.format->dependencies;
      auto drop=[](DirectoryDependencies* p) { p->release(); };
      std::unique_ptr<DirectoryDependencies,decltype(drop)> proof(
          DirectoryDependencies::make(*metadata_,graph_space,old->name),drop);
      for (unsigned slot=0;slot<32;++slot) {
        if (slot==path.slots[i]) proof->children[slot]=std::move(child);
        else proof->children[slot]=old->children[slot];
      }
      auto body=loom::allocate(worker_,{.bytes=576,.alignment=64});
      if (!body) return std::unexpected(body.error());
      const auto source=before.part(0);
      if (!source) return std::unexpected(source.error());
      const auto pointer=proof->children[path.slots[i]].selection().part(0);
      if (!pointer) return std::unexpected(pointer.error());
      idx_asm_clone_patch64(body->address,source->address(),9,64+path.slots[i]*16,
          std::uintptr_t(pointer->address()));
      auto captured=finish(old->name,proof->format,*body);
      if (!captured) return std::unexpected(captured.error());
      child=std::move(*captured);
    }
    return child;
  } catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> GroupFixture::replace(std::uint32_t prefix,unsigned word,
    std::uint64_t replacement) {
  using orbital::Error;
  if (word>=8) return std::unexpected(Error::invalid_argument);
  Store::Scope basis(store_);
  ReadContext context{directory_namespace(),&basis,{}};
  const auto read=view(context);
  Lookup found;
  GraphPath path;
  const auto* descriptor=graph_?(graph_lookup(basis,prefix,found,path)?found.descriptor:nullptr):
      idx_asm_group16(&read,prefix,&found);
  if (!descriptor) return std::unexpected(Error::invalid_argument);
  const auto before=basis.bind(material_id(prefix));
  if (!before) return std::unexpected(Error::invalid_argument);
  const auto old=before.part(0);
  if (!old) return std::unexpected(old.error());
  const std::byte* old_pointer;
  std::memcpy(&old_pointer,descriptor,8);
  if (old_pointer!=old->address()) return std::unexpected(Error::conflict);
  auto data=loom::allocate(worker_,{.bytes=64,.alignment=64});
  if (!data) return std::unexpected(data.error());
  idx_asm_clone_patch64(data->address,old->address(),1,word*8,replacement);
  auto finished=finish(material_id(prefix).low,data_,*data);
  if (!finished) return std::unexpected(finished.error());
  const auto part=finished->selection().part(0);
  if (!part) return std::unexpected(part.error());
  const Selection leaf{static_cast<Physical::Value*>(found.leaf_selection)};
  const auto source=leaf.part(0);
  if (!source) return std::unexpected(source.error());
  const auto control=leaf.control();
  const auto extent=control.extent(0);
  if (!extent) return std::unexpected(extent.error());
  auto directory=loom::allocate(worker_,{.bytes=*extent,.alignment=64});
  if (!directory) return std::unexpected(directory.error());
  idx_asm_clone_patch64(directory->address,source->address(),*extent/64,
      (graph_?0:64)+found.rank*64,std::uintptr_t(part->address()));
  auto changed=finish(found.leaf_name,*control.format,*directory);
  if (!changed) return std::unexpected(changed.error());
  if (graph_) {
    auto root=copy_graph(path,std::move(*changed));
    if (!root) return std::unexpected(root.error());
    changed=std::move(root);
  }
  const std::array changes{
      Store::Change{material_id(prefix),std::move(*finished)},
      Store::Change{{directory_namespace(),graph_?root_base:found.leaf_name},std::move(*changed)}};
  Store::Prepared packet(store_);
  if (auto prepared=store_.prepare(basis,changes,packet);!prepared) return prepared;
  return store_.install(packet);
}
}
