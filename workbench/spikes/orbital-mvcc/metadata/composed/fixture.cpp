#include "fixture.h"
#include <chrono>
#include <thread>

namespace orbital_versioned_performance {
Corpus::Corpus(std::size_t total_rows,std::size_t objects,std::uint64_t seed) {
  if (!objects || objects>4096 || total_rows<objects || total_rows%objects)
    throw std::invalid_argument("fixture requires 1..4096 Objects and divisible nonempty total row count");
  images.reserve(objects);
  for (std::size_t i=0;i<objects;++i) {
    auto shape=need(data::shape(schema,data::Format::rows,total_rows/objects));
    if (shape.part_count!=1 || shape.parts[0].alignment!=1 || shape.parts[0].readable_padding!=0)
      throw std::runtime_error("common Tuple body must admit alignment 1 and no readable overrun");
    Image image;
    image.address={{0x31b809fa2,i*17+3},shape.parts[0].id};
    image.shape=std::move(shape);
    image.rows=orbital_bench::initial_rows(total_rows/objects,seed+i*109);
    image.encoded.resize(image.shape.parts[0].body_bytes);
    const std::array<data::WritePart,1> target{{{image.address.part,image.encoded}}};
    need(data::construct_into(schema,image.shape,target,image.rows,construction));
    images.push_back(std::move(image));
  }
}
data::ReadData Corpus::bind(std::size_t object,orbital::Bytes body,orbital::Bytes interpretation) const {
  const auto& image=images.at(object);
  const std::array<data::ReadPart,1> parts{{{image.address.part,body,interpretation}}};
  // Match the actual consumer's interpretation overload; the Shape overload
  // would additionally revalidate already-prepared fixture metadata.
  return need(data::ReadData::bind(schema,orbital::Bytes{image.shape.interpretation},parts));
}
data::ReadData Corpus::direct(std::size_t object) const {
  const auto& image=images.at(object);
  return bind(object,image.encoded,image.shape.parts[0].interpretation);
}
data::TupleCursor Corpus::cursor(std::size_t object,orbital::Bytes body,orbital::Bytes interpretation) const {
  const auto& image=images.at(object);
  const std::array<data::ReadPart,1> parts{{{image.address.part,body,interpretation}}};
  return need(data::qualify_tuple(control(object),image.shape.interpretation,parts));
}
data::TupleCursor Corpus::direct_cursor(std::size_t object) const {
  const auto& image=images.at(object);
  return cursor(object,image.encoded,image.shape.parts[0].interpretation);
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
} // namespace orbital_versioned_performance
