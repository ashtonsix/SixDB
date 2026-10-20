#pragma once
#include "material.h"
#include <loom/worker.h>
#include <memory>
#include <vector>

extern "C" {
void idx_log_copy8(std::byte*,const std::byte*,std::size_t) noexcept;
void idx_log_append24(std::byte*,std::uint32_t,std::uint64_t,std::uint64_t) noexcept;
std::uint64_t idx_log_find(const std::byte*,std::size_t,std::uint32_t,std::uint64_t) noexcept;
std::uint64_t idx_log_index_find(const std::byte*,std::size_t,std::uint32_t,std::uint64_t) noexcept;
void idx_log_apply(const std::byte*,std::size_t,std::uint64_t*) noexcept;
}

namespace index_spike::driver {
// Finite end-to-end producer, separate from the directory algorithms. J=K=4,
// one64B row per L4 prefix, slot0; all five summary layers actually exist.
// Field3 alone is updated. All fields are unsigned/non-null; SUM uses128bits.
using Row=std::array<std::uint64_t,8>;
using Sum=unsigned __int128;
struct FieldSummary {
  std::uint64_t count,min,max,sum_low,sum_high,not_null;
  bool operator==(const FieldSummary&) const=default;
};
static_assert(sizeof(FieldSummary)==48);
enum class BufferPolicy { plain, indexed, eager };
enum class Fault { none, capture, prepare, stale };
struct Command { std::uint32_t row; std::uint64_t after; };
struct ProducerCounters {
  std::uint64_t publications{},captures{},directory_changes{},events{},log_copy_bytes{},
      rotations{},folds{},refusals{};
};

class BufferedProducer {
  struct Advice { std::uint64_t dirty{}; };
  struct Backing {
    orbital::allocation::Allocation allocation;
    std::shared_ptr<Advice> advice;
    std::size_t sealed{};
    // This owner NEVER grows/relocates its Allocation. Its shared lifetime is
    // physical custody, not a version/reader ledger or write permission.
  };
  class Memory final : public std::pmr::memory_resource {
    std::pmr::unsynchronized_pool_resource pool_;
    void* do_allocate(std::size_t,std::size_t) override;
    void do_deallocate(void*,std::size_t,std::size_t) override;
    bool do_is_equal(const std::pmr::memory_resource& r) const noexcept override { return this==&r; }
  public:
    bool fail{};
    std::size_t live{},peak{};
  };
  loom::Worker& worker_;
  BufferPolicy policy_;
  unsigned rows_;
  std::size_t capacity_;
  Memory memory_;
  std::array<Format,17> groups_{};
  Format summary_small_,summary_large_,log_format_;
  Store store_;
  std::shared_ptr<Backing> writer_;
  std::vector<Store::Change> work_; // Private bounded attempt workspace, never read authority.
  ProducerCounters counters_;
  static constexpr std::uint64_t data_ns=0x425546444154,dir_ns=0x425546444952,
      summary_ns=0x42554653554d,upper_ns=0x425546555050,
      log_ns=0x4255464c4f47,eager_ns=0x425546454147;
  static constexpr Format data_format{{64},1,data_ns};
  static constexpr Format top_format{{64},1,upper_ns};
  static constexpr Format middle_format{{128},1,upper_ns};
  static constexpr Format eager_format{{576},1,eager_ns};
  static constexpr Format marker_format{{64},1,log_ns};
  static orbital::ObjectId data(unsigned r) { return {data_ns,r+1}; }
  static orbital::ObjectId lower(unsigned group) { return {dir_ns,group+1}; }
  static orbital::ObjectId upper(unsigned layer) { return {upper_ns,layer+1}; }
  static orbital::ObjectId summary(unsigned layer) { return {summary_ns,layer+1}; }
  static orbital::ObjectId collection() { return {log_ns,1}; }
  static orbital::ObjectId eager_sum() { return {eager_ns,65}; }
  std::shared_ptr<Backing> allocate(std::size_t,std::shared_ptr<Advice> = {});
  Store::Owner finish(orbital::ObjectId,const Format&,const std::shared_ptr<Backing>&,
      std::size_t=0,bool prefix=false);
  Store::Owner native(orbital::ObjectId,const Format&,const std::byte*);
  Store::Owner capture_native(orbital::ObjectId,const Format&,orbital::allocation::Allocation&);
  const std::byte* descriptor(const Store::Scope&,unsigned) const;
  static const std::byte* part(Selection);
  std::pair<const std::byte*,std::size_t> log(const Store::Scope&) const;
  Row read(const Store::Scope&,unsigned) const;
  std::uint64_t selected_field3(const Store::Scope&,unsigned,const std::byte*) const;
  std::array<std::uint64_t,64> values(const Store::Scope&) const;
  Store::Owner make_summary(unsigned,const std::array<Row,64>&,std::shared_ptr<Advice>);
  void append_change(std::vector<Store::Change>&,unsigned&,orbital::ObjectId,Store::Owner);
  void fold_drafts(const Store::Scope&,const std::array<Row,64>&,
      std::vector<Store::Change>&,unsigned&,unsigned fail_at=64);
  void mark_summaries(const Store::Scope&);
  void publish(const Store::Scope&,std::span<const Store::Change>,Fault);
  void update_impl(std::span<const Command>,bool,Fault);
public:
  BufferedProducer(loom::Worker&,BufferPolicy,std::span<const Row>,std::size_t capacity=256u<<10);
  BufferedProducer(const BufferedProducer&)=delete;
  Store& store() noexcept { return store_; }
  BufferPolicy policy() const noexcept { return policy_; }
  unsigned size() const noexcept { return rows_; }
  Row row(const Store::Scope& scope,unsigned r) const { return read(scope,r); }
  std::uint64_t field3(const Store::Scope&,unsigned) const;
  Sum sum(const Store::Scope&) const;
  FieldSummary layer_summary(const Store::Scope&,unsigned,std::uint16_t,unsigned) const;
  orbital::Result<void> update(std::span<const Command>,Fault=Fault::none) noexcept;
  orbital::Result<void> compact_all(Fault=Fault::none) noexcept;
  orbital::Result<void> compact_row(unsigned) noexcept;
  std::array<const void*,5> directories(const Store::Scope&,unsigned row=0) const;
  ProducerCounters counters() const noexcept { return counters_; }
  std::size_t metadata_bytes() const noexcept { return memory_.live; }
  std::size_t metadata_peak() const noexcept { return memory_.peak; }
};
}
