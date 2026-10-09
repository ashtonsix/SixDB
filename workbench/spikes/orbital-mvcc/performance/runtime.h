#pragma once
#include "encoded.h"
#include "../consumer/consumer.h"

namespace orbital_versioned_performance {
struct RangeManifest;
namespace obj = orbital::object;
class Runtime {
public:
  orbital::worker::Lane lane;
  orbital::Ref<orbital::allocation::Arena> arena;
  orbital::allocation::Pool* pool{};
  explicit Runtime(std::size_t body_limit);
  ~Runtime();
  Runtime(const Runtime&)=delete;
};
class Orbital {
  friend RangeManifest diagnostic_ranges(const Orbital&);
  Runtime runtime_;
  obj::Store store_;
  const Corpus& corpus_;
public:
  data::Costs costs;
  Orbital(const Corpus&,std::size_t byte_limit,data::Format initial=data::Format::rows,std::size_t metadata_bytes=0);
  ~Orbital();
  obj::ReadContext read() const { return need(store_.read()); }
  obj::BorrowedContext borrow() const { return store_.borrow(); }
  const Corpus& corpus() const { return corpus_; }
  data::TupleCursor cursor(const obj::ReadContext&,std::size_t object) const;
  data::TupleCursor cursor(const obj::BorrowedContext&,std::size_t object) const;
  obj::View part(const obj::ReadContext&,std::size_t object) const;
  data::ReadData body(const obj::ReadContext& context,std::size_t object,bool alternative=false) const;
  data::ReadData body(const obj::BorrowedContext& context,std::size_t object,bool alternative=false) const;
  void update(std::span<const orbital_bench::Operation>,std::span<orbital_bench::Outcome>,
              std::span<const std::size_t> objects);
  void convert(std::size_t object,data::Format);
  obj::Statistics statistics() const { return store_.statistics(); }
  std::uint64_t charged_bytes() const { return runtime_.arena->charged_bytes.load(); }
  orbital::allocation::Statistics pool_statistics() const { return orbital::allocation::statistics(*runtime_.pool); }
  std::size_t drain(std::size_t budget) { return store_.drain(budget); }
  void poll(std::size_t budget) { (void)orbital::worker::poll(runtime_.lane,budget); }
};
} // namespace orbital_versioned_performance
