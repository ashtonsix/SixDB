#include "encoded.h"
#include "oracle.h"
#include "read.h"
#include <iostream>

using namespace orbital_versioned_performance;
using namespace orbital_bench;
void require(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
int main(int argc,char** argv) {
  if (argc!=2) throw std::invalid_argument("usage: body_check NEW_DB_DIRECTORY");
  Corpus corpus(68,4,91381);Lmdb store(argv[1],16*1024*1024);
  {auto write=store.edit();for (const auto& image:corpus.images)write.put_part(image.address,image.encoded);write.publish();}
  auto old=store.read();
  std::vector<Golden> golden;
  for (const auto& image:corpus.images) {
    Golden map;for (const auto& row:image.rows)map.emplace(row.key,row.record);golden.push_back(std::move(map));
    auto part=old.part(image.address);require(part && part->size()==image.encoded.size(),"opaque Tuple body missing");
    require(std::equal(part->begin(),part->end(),image.encoded.begin()),"LMDB encoded value differs from original Tuple bytes");
  }
  require(!old.part({{0x31b809fa2,123456789},corpus.images[0].address.part}),"absent opaque Object admitted");
  const auto original=golden;
  // Construct directly in legal unaligned/non-overreading LMDB reserve spans.
  // Complete each native body before the next LMDB operation can move its value.
  {auto write=store.edit();for (std::size_t i=0;i<corpus.images.size();++i) {
    auto rows=corpus.images[i].rows; rows[3].record.value=unsigned(i+7);rows[3].record.tag=unsigned(i+19);
    golden[i][rows[3].key]=rows[3].record;
    auto body=write.reserve_part(corpus.images[i].address,corpus.images[i].encoded.size());
    const std::array<data::WritePart,1> target{{{corpus.images[i].address.part,body}}};data::KernelCosts costs;
    need(data::construct_into(corpus.schema,corpus.images[i].shape,target,rows,costs));
  }write.publish();}
  auto current=store.read();std::uint64_t comparisons{};
  for (std::size_t i=0;i<corpus.images.size();++i) {
    const auto& image=corpus.images[i];
    auto before=corpus.bind(i,*old.part(image.address),image.shape.parts[0].interpretation);
    auto after=corpus.bind(i,*current.part(image.address),image.shape.parts[0].interpretation);
    for (auto name:{"point-hit","point-miss","dependent-16","scan-16"}) {
      const auto trace=make_trace(name,image.rows.size(),131,3145);
      for (const auto& op:trace.operations) {
        require(read_operation(before,op)==oracle(original[i],op),"old opaque snapshot/body changed");
        require(read_operation(after,op)==oracle(golden[i],op),"new native mapped Tuple body disagrees with map");++comparisons;
      }
    }
  }
  std::cout<<"{\"passed\":true,\"mapped_tuple_compares\":"<<comparisons<<",\"objects\":4,\"parts_per_object\":1,\"mapped_body_copy\":false,\"old_new_snapshot\":true}\n";
}
