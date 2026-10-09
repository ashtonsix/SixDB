#include "run.h"
#include "codec_candidate.h"

namespace orbital_versioned_performance {
template<class Factory>
Interval sized_reads(Factory factory,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  const auto run=[&]<std::size_t Capacity>() {
    auto acquire=[&]{return factory.template operator()<Capacity>();};
    return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,bindings):
                   opaque_read_trial<false>(acquire,input,objects,output,batch,bindings);
  };
  return batch==1?run.template operator()<1>():run.template operator()<64>();
}
Interval run_orbital_consume_reads(Orbital& store,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto factory=[&]<std::size_t N>{return orbital_codec_bodies<true,N>(store);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_lmdb_consume_reads(Lmdb& store,const Corpus& corpus,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto factory=[&]<std::size_t N>{return lmdb_codec_bodies<true,N>(store,corpus);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_orbital_codec_reads(Orbital& store,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto factory=[&]<std::size_t N>{return orbital_codec_bodies<false,N>(store);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_lmdb_codec_reads(Lmdb& store,const Corpus& corpus,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto factory=[&]<std::size_t N>{return lmdb_codec_bodies<false,N>(store,corpus);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_orbital_reads(Orbital& store,bool borrowed,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  if(borrowed) {
    auto factory=[&]<std::size_t N>{return orbital_borrowed_bodies<N>(store);};
    return sized_reads(factory,latency,input,objects,output,batch,bindings);
  }
  auto factory=[&]<std::size_t N>{return orbital_bodies<N>(store);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_lmdb_reads(Lmdb& store,const Corpus& corpus,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto factory=[&]<std::size_t N>{return lmdb_bodies<N>(store,corpus);};
  return sized_reads(factory,latency,input,objects,output,batch,bindings);
}
Interval run_direct_reads(const Corpus& corpus,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch) {
  // Actual immutable native bindings are qualified once before this interval.
  // No selection/snapshot is claimed by the direct control.
  std::vector<data::TupleCursor> bodies;bodies.reserve(corpus.images.size());
  for(std::size_t i=0;i<corpus.images.size();++i)bodies.push_back(corpus.direct_cursor(i));
  struct Direct {
    const std::vector<data::TupleCursor>& values;const Corpus& corpus;std::uint64_t bindings{};
    data::TupleBody body(std::size_t i)const{return values.at(i).open(corpus.control(i));}
  };
  std::uint64_t ignored{};auto acquire=[&]{return Direct{bodies,corpus};};
  return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,ignored):opaque_read_trial<false>(acquire,input,objects,output,batch,ignored);
}
Interval run_row_lmdb_reads(Lmdb& store,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<orbital_bench::Outcome> output,std::size_t batch) {
  auto acquire=[&]{return store.read();};
  return latency?read_trial<true>(acquire,input,output,batch):read_trial<false>(acquire,input,output,batch);
}
} // namespace orbital_versioned_performance
