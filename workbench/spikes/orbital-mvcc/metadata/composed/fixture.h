#pragma once
#include <orbital/allocation.h>
#include <orbital/worker.h>
#include "../../consumer/data.h"

namespace orbital_versioned_performance {
namespace data = orbital_bench::versioned;
using orbital_bench::Row;
struct Address {
  orbital::ObjectId object{};
  orbital::PartId part{};
  auto operator<=>(const Address&) const = default;
};
template<class T> T need(orbital::Result<T> result) {
  if (!result) throw std::runtime_error("versioned fixture operation refused: "+std::to_string(unsigned(result.error())));
  return std::move(*result);
}
inline void need(orbital::Result<void> result) {
  if (!result) throw std::runtime_error("versioned fixture operation refused: "+std::to_string(unsigned(result.error())));
}
struct Image {
  Address address;
  data::Shape shape;
  std::vector<Row> rows;
  std::vector<std::byte> encoded;
};
// Opaque fixture only: no container navigation, membership or index policy.
// Total row footprint is held fixed as the number of Objects varies.
struct Corpus {
  data::Schema schema;
  std::vector<Image> images;
  data::KernelCosts construction;
  Corpus(std::size_t total_rows,std::size_t objects,std::uint64_t seed);
  data::ReadData bind(std::size_t object,orbital::Bytes body,
                      orbital::Bytes part_interpretation) const;
  data::ReadData direct(std::size_t object) const;
  data::TupleControl control(std::size_t object) const { return {schema,images.at(object).shape.records}; }
  data::TupleCursor cursor(std::size_t object,orbital::Bytes body,orbital::Bytes part_interpretation) const;
  data::TupleCursor direct_cursor(std::size_t object) const;
};
class Runtime {
public:
  orbital::worker::Lane lane;
  orbital::Ref<orbital::allocation::Arena> arena;
  orbital::allocation::Pool* pool{};
  explicit Runtime(std::size_t body_limit);
  ~Runtime();
  Runtime(const Runtime&)=delete;
};
} // namespace orbital_versioned_performance
