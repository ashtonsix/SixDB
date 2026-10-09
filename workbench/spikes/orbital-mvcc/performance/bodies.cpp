#include "bodies.h"

namespace orbital_versioned_performance {
void lmdb_update(Lmdb& store,const Corpus& corpus,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,data::Costs& costs) {
  using namespace orbital_bench;
  if (input.empty() || input.size()>64 || input.size()!=objects.size() || input.size()!=output.size())
    throw std::invalid_argument("invalid LMDB publication batch");
  for (std::size_t i=0;i<input.size();++i) {
    if (objects[i]>=corpus.images.size() || input[i].kind!=Kind::update || input[i].record.value>value_mask)
      throw std::invalid_argument("invalid LMDB fixed-row update");
    output[i]={};output[i].changes=1;detail::mix(output[i].digest,1);
  }
  // A separate real readonly basis keeps original mapped bytes valid while
  // MDB_RESERVE/COW can move private values. Its cost belongs to this write.
  auto basis=store.read();auto write=store.edit();
  std::array<std::size_t,64> positions;
  std::array<Record,64> changed;
  for (std::size_t object=0;object<corpus.images.size();++object) {
    if (std::find(objects.begin(),objects.end(),object)==objects.end())continue;
    const auto& image=corpus.images[object];auto bytes=basis.part(image.address);
    if (!bytes) throw std::runtime_error("missing LMDB update basis");
    auto source=corpus.bind(object,*bytes,image.shape.parts[0].interpretation);
    std::size_t used{};
    for (std::size_t i=0;i<input.size();++i)if(objects[i]==object) {
      auto old=source.get(input[i].key);
      if(!old)throw std::runtime_error("absent LMDB fixed-row update");
      old->value=input[i].record.value;old->tag=input[i].record.tag;
      // Match the actual adapter's preserving read and independent key/row
      // resolution before cloning a complete initialized Tuple Part.
      positions[used]=source.lower_bound(input[i].key);changed[used++]=*old;
    }
    auto target=write.reserve_part(image.address,bytes->size());
    std::memcpy(target.data(),bytes->data(),bytes->size());costs.copied_bytes+=bytes->size();++costs.copied_spans;
    costs.requested_body_bytes+=bytes->size();++costs.requested_parts;
    const std::array<data::WritePart,1> parts{{{image.address.part,target}}};
    for(std::size_t i=0;i<used;++i)need(data::update_into(corpus.schema,image.shape,parts,positions[i],changed[i],costs.kernel));
  }
  write.publish();
}
} // namespace orbital_versioned_performance
