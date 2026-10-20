#include "group_fixture.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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
  const Control control{metadata_,&format,name,0,1};
  const std::array sources{orbital::PartSource{control.id(0),
      loom::bytes(allocation),{},&allocation}};
  return store_.capture(control,sources);
}
GroupFixture::GroupFixture(loom::Worker& worker,std::span<const Record> input,
    std::pmr::memory_resource* metadata)
    : worker_(worker),metadata_(metadata?metadata:&memory_),store_(*metadata_) {
  static_assert(root_base<=std::numeric_limits<std::uint64_t>::max()-(UINT32_MAX>>4));
  for (unsigned n=1;n<=16;++n) groups_[n]={{64+n*64},1,directory_space};
  std::vector<Store::Change> changes;
  changes.reserve(input.size()*2);
  for (std::size_t first=0;first<input.size();) {
    std::size_t last=first;
    while (last<input.size() && (input[last].prefix>>4)==(input[first].prefix>>4)) ++last;
    const auto count=last-first;
    admit(count<=16,"more than16 unique suffix positions");
    const auto& format=groups_[count];
    auto directory=setup(loom::allocate(worker_,{.bytes=format.bytes[0],.alignment=64}));
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
    changes.push_back({{directory_space,name},setup(finish(name,format,directory))});
    first=last;
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
orbital::Result<void> GroupFixture::replace(std::uint32_t prefix,unsigned word,
    std::uint64_t replacement) {
  using orbital::Error;
  if (word>=8) return std::unexpected(Error::invalid_argument);
  Store::Scope basis(store_);
  ReadContext context{directory_space,&basis,{}};
  const auto read=view(context);
  Lookup found;
  const auto* descriptor=idx_asm_group16(&read,prefix,&found);
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
      64+found.rank*64,std::uintptr_t(part->address()));
  auto changed=finish(found.leaf_name,*control.format,*directory);
  if (!changed) return std::unexpected(changed.error());
  const std::array changes{
      Store::Change{material_id(prefix),std::move(*finished)},
      Store::Change{{directory_space,found.leaf_name},std::move(*changed)}};
  Store::Prepared packet(store_);
  if (auto prepared=store_.prepare(basis,changes,packet);!prepared) return prepared;
  return store_.install(packet);
}
}
