#include "runtime.h"
#include <chrono>
#include <thread>

namespace orbital_versioned_performance {
namespace {
std::size_t metadata_limit(std::size_t total) {
  if (total<1024*1024) throw std::invalid_argument("common byte limit must be at least 1 MiB");
  return std::min<std::size_t>(total/4,1024*1024);
}
template<class Context>
data::ReadData bind(const Corpus& corpus,const Context& context,std::size_t object,bool alternative) {
  auto selected=need(obj::select(context,corpus.images.at(object).address.object,alternative));
  // Actual baseline consumer: selected format, interpretation, body and
  // logical row count are qualified once around the native body.
  if (selected.part_count()==1)
    return need(data::bind(corpus.schema,need(selected.part(corpus.images[object].address.part))));
  return need(data::bind(corpus.schema,selected));
}
template<class Context>
data::TupleCursor tuple_cursor(const Corpus& corpus,const Context& context,std::size_t object) {
  const auto& address=corpus.images.at(object).address;
  const auto selected=need(obj::select(context,address.object));
  return need(data::qualify_tuple(corpus.control(object),need(selected.part(address.part))));
}
}
Runtime::Runtime(std::size_t limit) {
  need(orbital::worker::adopt(lane));
  try {
    arena=need(orbital::allocation::open_arena(limit));
    pool=need(orbital::allocation::add_pool(*arena,lane));
  } catch (...) { arena={};orbital::worker::relinquish(lane);throw; }
}
Runtime::~Runtime() {
  if (pool) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (orbital::allocation::close(*pool).kind()!=orbital::Progress::Kind::done) {
      if (std::chrono::steady_clock::now()>deadline) std::terminate();
      orbital::worker::poll(lane,1024);std::this_thread::yield();
    }
    if (arena->charged_bytes.load()) std::terminate();
    need(orbital::allocation::remove_pool(*pool));
  }
  arena={};orbital::worker::relinquish(lane);
}
Orbital::Orbital(const Corpus& corpus,std::size_t limit,data::Format format)
    :runtime_(limit-metadata_limit(limit)),store_(need(obj::Store::open(runtime_.lane,metadata_limit(limit)))),corpus_(corpus) {
  auto draft=need(store_.edit());
  for (const auto& image:corpus.images) {
    auto representation=need(data::construct(corpus.schema,store_,*runtime_.pool,image.address.object,
        draft.version(),image.rows,format,costs));
    need(draft.put(std::move(representation)));
  }
  auto accepted=need(store_.prepare(draft));need(store_.install(accepted));accepted.retire();
  store_.drain(SIZE_MAX);
}
Orbital::~Orbital() { store_={}; }
obj::View Orbital::part(const obj::ReadContext& context,std::size_t object) const {
  const auto& address=corpus_.images.at(object).address;
  auto selected=need(obj::select(context,address.object));
  return need(selected.part(address.part));
}
data::TupleCursor Orbital::cursor(const obj::ReadContext& context,std::size_t object) const {
  return tuple_cursor(corpus_,context,object);
}
data::TupleCursor Orbital::cursor(const obj::BorrowedContext& context,std::size_t object) const {
  return tuple_cursor(corpus_,context,object);
}
data::ReadData Orbital::body(const obj::ReadContext& context,std::size_t object,bool alternative) const {
  return bind(corpus_,context,object,alternative);
}
data::ReadData Orbital::body(const obj::BorrowedContext& context,std::size_t object,bool alternative) const {
  return bind(corpus_,context,object,alternative);
}
void Orbital::update(std::span<const orbital_bench::Operation> input,std::span<orbital_bench::Outcome> output,
                     std::span<const std::size_t> objects) {
  using namespace orbital_bench;
  if (input.empty() || input.size()>64 || output.size()!=input.size() || objects.size()!=input.size())
    throw std::invalid_argument("invalid publication batch");
  auto draft=need(store_.edit());
  for (std::size_t i=0;i<input.size();++i) {
    if (objects[i]>=corpus_.images.size() || input[i].kind!=Kind::update || input[i].record.value>value_mask)
      throw std::invalid_argument("publication requires valid fixed-membership updates");
    output[i]={};output[i].changes=1;detail::mix(output[i].digest,1);
  }
  std::array<data::Update,64> updates;
  for (std::size_t object=0;object<corpus_.images.size();++object) {
    if (std::find(objects.begin(),objects.end(),object)==objects.end()) continue;
    auto selected=need(draft.select(corpus_.images[object].address.object));
    const auto source=need(data::bind(corpus_.schema,selected));
    std::size_t used{};
    for (std::size_t i=0;i<input.size();++i) if (objects[i]==object) {
      auto old=source.get(input[i].key);
      if (!old) throw std::runtime_error("absent fixed-row update");
      old->value=input[i].record.value;old->tag=input[i].record.tag;
      updates[used++]={input[i].key,*old};
    }
    auto representation=need(data::update(corpus_.schema,store_,*runtime_.pool,selected,draft.version(),
        std::span{updates}.first(used),costs));
    need(draft.put(std::move(representation)));
  }
  auto accepted=need(store_.prepare(draft));need(store_.install(accepted));accepted.retire();
  // Actual bounded maintenance belongs to this complete write request.
  store_.drain(128);orbital::worker::poll(runtime_.lane,128);
}
void Orbital::convert(std::size_t object,data::Format format) {
  auto context=store_.borrow();
  const auto id=corpus_.images.at(object).address.object;
  auto selected=obj::select(context,id,true);
  if (!selected && selected.error()==orbital::Error::absent)
    selected=obj::select(context,id,false); // First conversion has no alternative yet.
  auto source=need(std::move(selected));
  auto converted=need(data::convert(corpus_.schema,store_,*runtime_.pool,source,format,costs));
  need(store_.adopt(source,std::move(converted)));
  store_.drain(128);orbital::worker::poll(runtime_.lane,128);
}
} // namespace orbital_versioned_performance
