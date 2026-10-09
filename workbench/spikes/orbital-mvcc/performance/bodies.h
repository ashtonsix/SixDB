#pragma once
#include "runtime.h"
#include "trial.h"
#include "../consumer/native.h"
#include "request_cache.h"

namespace orbital_versioned_performance {
// This cache is bounded by the offered request group, with protection owned
// by the actual selected snapshot. Collisions rebind opaque identities; the
// cache does not substitute for the baseline access path.
template<class Context,class Resolve,std::size_t Capacity=64>
class Bodies {
  Context context_;
  Resolve resolve_;
  const Corpus& corpus_;
  RequestCache<data::TupleCursor,Capacity> bodies_;
public:
  std::uint64_t bindings{};
  Bodies(Context context,Resolve resolve,const Corpus& corpus)
      :context_(std::move(context)),resolve_(std::move(resolve)),corpus_(corpus) {}
  data::TupleBody body(std::size_t object) {
    if (object>=corpus_.images.size()) throw std::invalid_argument("opaque fixture object extent exceeded");
    return bodies_.get(object,[&]{return resolve_(context_,object);},bindings).open(corpus_.control(object));
  }
};
template<class Context,class Resolve> Bodies(Context,Resolve,const Corpus&)->Bodies<Context,Resolve>;
template<std::size_t Capacity=64> inline auto lmdb_bodies(Lmdb& store,const Corpus& corpus) {
  auto resolve=[&corpus](const Lmdb::Read& context,std::size_t object) {
    const auto& image=corpus.images.at(object);auto body=context.part(image.address);
    if (!body) throw std::runtime_error("missing opaque LMDB value");
    return corpus.cursor(object,*body,image.shape.parts[0].interpretation);
  };
  return Bodies<Lmdb::Read,decltype(resolve),Capacity>{store.read(),resolve,corpus};
}
template<std::size_t Capacity=64> inline auto orbital_bodies(Orbital& store) {
  auto resolve=[&store](const obj::ReadContext& context,std::size_t object){return store.cursor(context,object);};
  return Bodies<obj::ReadContext,decltype(resolve),Capacity>{store.read(),resolve,store.corpus()};
}
template<std::size_t Capacity=64> inline auto orbital_borrowed_bodies(Orbital& store) {
  auto resolve=[&store](const obj::BorrowedContext& context,std::size_t object){return store.cursor(context,object);};
  return Bodies<obj::BorrowedContext,decltype(resolve),Capacity>{store.borrow(),resolve,store.corpus()};
}
template<bool Latency,class Acquire>
Interval opaque_read_trial(Acquire&& acquire,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,
    std::uint64_t& bindings) {
  if (!batch || batch>64 || input.size()!=output.size() || objects.size()!=input.size())
    throw std::invalid_argument("invalid opaque trial extents");
  Interval interval;
  prepare_samples<Latency>(interval,(input.size()+batch-1)/batch);
  hardware_start();
  const auto process_cpu=process_cpu_ns(),cpu=caller_cpu_ns(),start=clock_ns();
  for (std::size_t first=0;first<input.size();first+=batch) {
    const auto count=std::min(batch,input.size()-first);
    std::uint64_t batch_start{};
    if constexpr (Latency) batch_start=clock_ns();
    {
      auto selected=acquire();
      for (std::size_t i=first;i<first+count;++i) output[i]=read_operation(selected.body(objects[i]),input[i]);
      bindings+=selected.bindings;
    }
    if constexpr (Latency) interval.batch_ns.push_back(clock_ns()-batch_start);
    ++interval.snapshot_scopes;
  }
  interval.wall_ns=clock_ns()-start;interval.caller_cpu_ns=caller_cpu_ns()-cpu;interval.logical_requests=input.size();
  interval.process_cpu_ns=process_cpu_ns()-process_cpu;
  hardware_stop();
  for (const auto& outcome:output) interval.logical_rows+=outcome.rows;
  return interval;
}
void lmdb_update(Lmdb&,const Corpus&,std::span<const orbital_bench::Operation>,
    std::span<const std::size_t>,std::span<orbital_bench::Outcome>,data::Costs&);
} // namespace orbital_versioned_performance
