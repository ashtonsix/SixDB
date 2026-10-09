#pragma once
#include "bodies.h"
#include "../metadata/consumer/candidates.h"
#include "../metadata/consumer/operations.h"

namespace orbital_versioned_performance {
namespace codec = orbital_mvcc::metadata_consumer;
// Actual direct admission and native operation placement from Ikea's spike.
// This first treatment keeps the frozen catalog, snapshot and full parent law.
// The same treatment is applied to mapped LMDB's identical encoded value.
struct FusedTuple16 {
  const data::Schema& schema;
  codec::ExplicitTuple16 state;
  auto get(orbital_bench::Key key) const { return codec::tuple_get(schema,state,key); }
  std::size_t scan(orbital_bench::Key key,std::span<orbital_bench::Row> output) const {
    return codec::tuple_scan(schema,state,key,output);
  }
  auto chain(orbital_bench::Key key,std::span<orbital_bench::Record> output) const {
    return codec::tuple_chain(schema,state,key,output);
  }
};
struct ConsumedTuple16 : FusedTuple16 {
  orbital_bench::Outcome consume(const orbital_bench::Operation& op) const {
    using namespace orbital_bench;
    if (op.kind==Kind::scan) return read_operation(static_cast<const FusedTuple16&>(*this),op);
    if (op.kind!=Kind::read && op.kind!=Kind::chain) throw std::invalid_argument("read-only candidate received a write");
    Outcome output;
    codec::read_chain_into(schema,state,op.key,op.kind==Kind::chain?op.count:1,output);
    return output;
  }
};
template<class Context,class Resolve,class Body=FusedTuple16,std::size_t Capacity=64>
class CodecBodies {
  Context context_;
  Resolve resolve_;
  const Corpus& corpus_;
  RequestCache<codec::ExplicitTuple16,Capacity> bodies_;
public:
  std::uint64_t bindings{};
  CodecBodies(Context context,Resolve resolve,const Corpus& corpus)
      :context_(std::move(context)),resolve_(std::move(resolve)),corpus_(corpus) {}
  Body body(std::size_t object) {
    if (object>=corpus_.images.size()) throw std::invalid_argument("opaque fixture object extent exceeded");
    const auto state=bodies_.get(object,[&]{return resolve_(context_,object);},bindings);
    if constexpr (std::same_as<Body,ConsumedTuple16>) return {{corpus_.schema,state}};
    else return {corpus_.schema,state};
  }
};
template<class Context,class Resolve> CodecBodies(Context,Resolve,const Corpus&)->CodecBodies<Context,Resolve>;
template<bool Consume=false,std::size_t Capacity=64> inline auto orbital_codec_bodies(Orbital& store) {
  auto resolve=[&store](const obj::ReadContext& context,std::size_t object) {
    // View's enclosing Description proves ProgramId, logical count and full
    // parent interpretation; the requested opaque PartId was resolved by part().
    return need(codec::qualify_tuple16(store.corpus().control(object),store.part(context,object)));
  };
  using Body=std::conditional_t<Consume,ConsumedTuple16,FusedTuple16>;
  return CodecBodies<obj::ReadContext,decltype(resolve),Body,Capacity>{store.read(),resolve,store.corpus()};
}
template<bool Consume=false,std::size_t Capacity=64> inline auto lmdb_codec_bodies(Lmdb& store,const Corpus& corpus) {
  auto resolve=[&corpus](const Lmdb::Read& context,std::size_t object) {
    const auto& image=corpus.images.at(object);
    const auto bytes=context.part(image.address);
    if (!bytes) throw std::runtime_error("missing opaque LMDB value");
    const data::ReadPart part{image.address.part,*bytes,image.shape.parts[0].interpretation};
    return need(codec::qualify_tuple16(corpus.control(object),image.shape.interpretation,
                                     std::span<const data::ReadPart>{&part,1}));
  };
  using Body=std::conditional_t<Consume,ConsumedTuple16,FusedTuple16>;
  return CodecBodies<Lmdb::Read,decltype(resolve),Body,Capacity>{store.read(),resolve,corpus};
}
} // namespace orbital_versioned_performance
