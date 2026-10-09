#pragma once
#include "codec_candidate.h"
#include "../metadata/orbital/candidates/candidate_store.h"
#include <cstring>

namespace orbital_versioned_performance {
// Same real Allocation/Pool backing as the frozen catalog; the candidate's
// explicit Memory charges descriptors, nodes, registry and shared body owners.
// Its resource lifetime is fixture-owned, not a production ownership proposal.
template<class Store>
class Candidate {
  struct Backing { orbital::allocation::Allocation allocation; };
  compact_history::Memory memory_;
  Runtime runtime_;
  Store store_;
  const Corpus& corpus_;
  static std::size_t metadata_limit(std::size_t total,std::size_t offered=0) {
    if (offered && offered>=total) throw std::invalid_argument("metadata partition must leave body budget");
    return offered?offered:std::min<std::size_t>(total/4,1024*1024);
  }
public:
  using Scope=typename Store::Scope;
  data::Costs costs;
  Candidate(const Corpus& corpus,std::size_t total,std::size_t metadata_bytes=0)
      :runtime_(total-metadata_limit(total,metadata_bytes)),store_(memory_),corpus_(corpus) {
    memory_.limit=metadata_limit(total,metadata_bytes);
    {
      Scope basis(store_);
      std::pmr::vector<compact_history::Change> changes(&memory_);
      changes.reserve(corpus.images.size());
      for (std::size_t object=0;object<corpus.images.size();++object) {
        const auto& image=corpus.images[object];
        auto backing=std::allocate_shared<Backing>(std::pmr::polymorphic_allocator<Backing>{&memory_});
        backing->allocation=need(orbital::allocation::allocate(*runtime_.pool,
            {.bytes=image.encoded.size(),.alignment=1,.initialization=orbital::allocation::Initialization::uninitialized}));
        const data::WritePart output{image.address.part,orbital::allocation::bytes(backing->allocation)};
        need(data::construct_into(corpus.schema,image.shape,std::span<const data::WritePart>{&output,1},image.rows,costs.kernel));
        const auto logical=codec::detail::count_bytes(image.shape.records);
        const compact_descriptor::RepresentationSpec spec{image.address.object,basis.cut()+1,{},data::data_program,
            logical,image.shape.interpretation,{}};
        const compact_descriptor::PartSource part{image.address.part,orbital::allocation::bytes(backing->allocation),
            image.shape.parts[0].interpretation,0,1,backing};
        changes.push_back({image.address.object,need(store_.capture(spec,std::span<const compact_descriptor::PartSource>{&part,1}))});
      }
      auto accepted=need(store_.prepare(basis,changes));need(store_.install(accepted));
    }
    store_.drain();costs={};
  }
  ~Candidate() {store_=Store{}; if (memory_.live) std::terminate();}
  const Corpus& corpus() const {return corpus_;}
  const Store& catalog() const {return store_;}
  data::ReadData body(const Scope& scope,std::size_t object) const {
    const auto& image=corpus_.images.at(object);
    const auto selected=scope.bind(image.address.object);
    if (!selected) throw std::runtime_error("candidate selected update basis absent");
    const auto& parent=compact_descriptor::description(selected);
    if (parent.object!=image.address.object || parent.version>scope.cut() || parent.format!=data::data_program || selected.part_count()!=1 ||
        !std::ranges::equal(parent.logical_metadata(),codec::detail::count_bytes(image.shape.records)))
      throw std::runtime_error("candidate update parent law differs");
    const auto view=need(selected.part(image.address.part));
    const data::ReadPart part{view.id(),view.body(),view.interpretation()};
    return need(data::ReadData::bind(corpus_.schema,parent.interpretation(),std::span<const data::ReadPart>{&part,1}));
  }
  void update(std::span<const orbital_bench::Operation> input,std::span<orbital_bench::Outcome> output,
              std::span<const std::size_t> objects) {
    using namespace orbital_bench;
    if (input.empty() || input.size()>64 || output.size()!=input.size() || objects.size()!=input.size())
      throw std::invalid_argument("invalid candidate publication batch");
    // Prepared pins its exact Core; the writer read Scope ends before install
    // and drain, so it cannot artificially retain the previous version.
    auto accepted=[&] {
      Scope basis(store_);
      std::pmr::vector<compact_history::Change> changes(&memory_);
      std::array<std::size_t,64> positions;
      std::array<Record,64> changed;
      for (std::size_t i=0;i<input.size();++i) {
        if (objects[i]>=corpus_.images.size() || input[i].kind!=Kind::update || input[i].record.value>value_mask)
          throw std::invalid_argument("invalid candidate fixed-row update");
        output[i]={};output[i].changes=1;detail::mix(output[i].digest,1);
      }
      for (std::size_t offered=0;offered<objects.size();++offered) {
        const auto object=objects[offered];
        if (std::find(objects.begin(),objects.begin()+offered,object)!=objects.begin()+offered) continue;
        const auto& image=corpus_.images[object];
        const auto selected=basis.bind(image.address.object);
        const auto source=body(basis,object);
        const auto original=need(selected.part(image.address.part));
        std::size_t used{};
        for (std::size_t i=0;i<input.size();++i) if (objects[i]==object) {
          auto record=source.get(input[i].key);
          if (!record) throw std::runtime_error("absent candidate fixed-row update");
          record->value=input[i].record.value;record->tag=input[i].record.tag;
          positions[used]=source.lower_bound(input[i].key);changed[used++]=*record;
        }
        auto backing=std::allocate_shared<Backing>(std::pmr::polymorphic_allocator<Backing>{&memory_});
        backing->allocation=need(orbital::allocation::allocate(*runtime_.pool,
            {.bytes=original.body().size(),.alignment=1,.initialization=orbital::allocation::Initialization::uninitialized}));
        auto bytes=orbital::allocation::bytes(backing->allocation);
        std::memcpy(bytes.data(),original.body().data(),bytes.size());costs.copied_bytes+=bytes.size();++costs.copied_spans;
        costs.requested_body_bytes+=bytes.size();++costs.requested_parts;
        const data::WritePart output_part{image.address.part,bytes};
        for (std::size_t i=0;i<used;++i)
          need(data::update_into(corpus_.schema,image.shape,std::span<const data::WritePart>{&output_part,1},positions[i],changed[i],costs.kernel));
        const auto logical=codec::detail::count_bytes(image.shape.records);
        const compact_descriptor::RepresentationSpec spec{image.address.object,basis.cut()+1,{},data::data_program,
            logical,image.shape.interpretation,{}};
        const compact_descriptor::PartSource part{image.address.part,bytes,image.shape.parts[0].interpretation,0,1,backing};
        changes.push_back({image.address.object,need(store_.capture(spec,std::span<const compact_descriptor::PartSource>{&part,1}))});
      }
      return need(store_.prepare(basis,changes));
    }();
    need(store_.install(accepted));
    store_.drain();orbital::worker::poll(runtime_.lane,128);
  }
  std::size_t drain(std::size_t) {return store_.drain();}
  void poll(std::size_t count) {orbital::worker::poll(runtime_.lane,count);}
  auto statistics() const {return store_.statistics();}
  std::uint64_t charged_bytes() const {return runtime_.arena->charged_bytes.load();}
  auto pool_statistics() const {return orbital::allocation::statistics(*runtime_.pool);}
  std::size_t memory_calls() const {return memory_.calls;}
  std::size_t metadata_budget() const {return memory_.limit;}
  std::uint64_t cut() const {return store_.borrow().cut;}
};

template<class Store,bool Direct,std::size_t Capacity=64>
class CandidateBodies {
  typename Store::Scope scope_;
  const Corpus& corpus_;
  using State=std::conditional_t<Direct,codec::ExplicitTuple16,data::TupleCursor>;
  RequestCache<State,Capacity> cache_;
public:
  std::uint64_t bindings{};
  explicit CandidateBodies(const Candidate<Store>& owner):scope_(owner.catalog()),corpus_(owner.corpus()) {}
  auto body(std::size_t object) {
    if (object>=corpus_.images.size()) throw std::invalid_argument("candidate fixture object extent exceeded");
    const auto state=cache_.get(object,[&]() -> State {
      const auto& image=corpus_.images.at(object);
      const auto selected=scope_.bind(image.address.object);
      if (!selected) throw std::runtime_error("candidate selection absent");
      // Borrow the actual frozen parent, never return/copy its 88-byte facade.
      const auto& parent=compact_descriptor::description(selected);
      if (parent.object!=image.address.object || parent.version>scope_.cut() ||
          parent.format!=data::data_program || selected.part_count()!=1)
        throw std::runtime_error("candidate parent identity/format differs");
      const auto logical=codec::detail::count_bytes(image.shape.records);
      if (!std::ranges::equal(parent.logical_metadata(),logical))
        throw std::runtime_error("candidate logical count differs");
      const auto view=need(selected.part(image.address.part));
      const data::ReadPart part{view.id(),view.body(),view.interpretation()};
      if constexpr (Direct) return need(codec::qualify_tuple16(corpus_.control(object),parent.interpretation(),
          std::span<const data::ReadPart>{&part,1}));
      else return need(data::qualify_tuple(corpus_.control(object),parent.interpretation(),
          std::span<const data::ReadPart>{&part,1}));
    },bindings);
    if constexpr (Direct) return ConsumedTuple16{{corpus_.schema,state}};
    else return state.open(corpus_.control(object));
  }
};
template<class Store,bool Direct>
Interval candidate_reads(Candidate<Store>& owner,bool latency,std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects,std::span<orbital_bench::Outcome> output,std::size_t batch,std::uint64_t& bindings) {
  const auto run=[&]<std::size_t Capacity>() {
    const auto acquire=[&]{return CandidateBodies<Store,Direct,Capacity>{owner};};
    return latency?opaque_read_trial<true>(acquire,input,objects,output,batch,bindings):
                   opaque_read_trial<false>(acquire,input,objects,output,batch,bindings);
  };
  return batch==1?run.template operator()<1>():run.template operator()<64>();
}
} // namespace orbital_versioned_performance
