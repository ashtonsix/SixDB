#include "run.h"

namespace orbital_versioned_performance {
Interval run_orbital_reads(Orbital& store,bool borrowed,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  if(borrowed) {
    auto acquire=[&]{return orbital_borrowed_bodies(store);};
    return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,bindings):opaque_read_trial<false>(acquire,input,objects,output,batch,bindings);
  }
  auto acquire=[&]{return orbital_bodies(store);};
  return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,bindings):opaque_read_trial<false>(acquire,input,objects,output,batch,bindings);
}
Interval run_lmdb_reads(Lmdb& store,const Corpus& corpus,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  auto acquire=[&]{return lmdb_bodies(store,corpus);};
  return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,bindings):opaque_read_trial<false>(acquire,input,objects,output,batch,bindings);
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
