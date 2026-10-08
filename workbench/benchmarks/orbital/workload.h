#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace orbital_bench {
using Key = std::uint64_t;
inline constexpr std::uint32_t value_mask = (1u << 20) - 1;
struct Record {
  Key next{};
  std::uint32_t value{}, tag{};
  auto operator<=>(const Record&) const = default;
};
struct Row {
  Key key{};
  Record record;
  auto operator<=>(const Row&) const = default;
};
enum class Kind { read, update, insert, erase, rmw, scan, chain, order, status, stock };
struct Operation {
  Kind kind{};
  Key key{};
  Record record;
  std::uint32_t count{1};
};
struct Outcome {
  std::uint64_t digest{}, rows{}, changes{};
  auto operator<=>(const Outcome&) const = default;
};
enum class ReadExecution { serial, native_direct, manual, ring };
struct Trace {
  std::vector<Operation> operations;
  std::uint64_t fingerprint{};
};
struct Counters {
  // Page-construction effects are merged on owner adoption. With preparation,
  // these totals are not all producer writes: discarded/unused construction
  // needs separate completed-work counters at explicit interval boundaries.
  std::uint64_t payload_bytes{}, metadata_bytes{}, allocated_pages{}, splits{},
      lease_calls{}, issued_write_bytes{}, issued_write_spans{};
  std::uint64_t index_write_bytes{}, index_write_spans{};
  std::uint64_t pool_mapped_bytes{}, pool_reserved_bytes{}, pool_metadata_bytes{}, arena_charged_bytes{};
  std::uint64_t lmdb_file_used_bytes{};
  std::uint64_t live_payload_bytes{}, pool_idle_bytes{}, pool_allocations{}, pool_reuses{}, pool_growths{}, pool_trim_pages{};
  std::uint64_t logical_live_rows{}, logical_live_field_bytes{};
};
// Cumulative owner demand observations, changed only at Page creation.
struct PreparationDemands {
  std::uint64_t demands{}, ready_hits{}, stockouts{}, inline_fallbacks{};
};
struct PreparationStats {
  std::uint64_t inventory_limit{}, inventory{}, ready{}, inflight{}, repair_wait{};
  std::uint64_t peak_inventory{}, peak_ready{}, peak_arena_charge{}, arena_charge{};
  std::uint64_t ready_backing_bytes{}, preparing_bytes{}, preparing_reserved_bytes{};
  std::uint64_t submissions{}, admission_failures{}, demands{}, ready_hits{}, stockouts{}, inline_fallbacks{};
  std::uint64_t owner_messages{}, cleanup_retries{}, unused_inventory{}, discarded_completed_pages{};
  std::uint64_t discarded_pack_bytes{}, discarded_pack_spans{}, discarded_index_bytes{}, discarded_index_spans{};
  std::uint64_t private_metadata_bound_bytes{};
  std::uint64_t completed_pages{}, completed_pack_bytes{}, completed_pack_spans{},
      completed_index_bytes{}, completed_index_spans{}, constructor_failures{};
  std::uint64_t startup_ns{}, startup_process_cpu_ns{}, startup_producer_cpu_ns{},
      producer_cpu_ns{}, final_producer_cpu_ns{}, drain_ns{};
  int requested_cpu{-1}, observed_cpu{-1}, owner_cpu{-1};
  bool storage_observed{};
};
struct MemoryRegion {
  std::uintptr_t address;
  std::size_t bytes;
  std::string_view kind;
};
/// One assigned caller. begin/end delimit one logical request. This fixture
/// provides no candidate isolation, snapshots or durable publication.
class Store {
public:
  virtual ~Store() = default;
  virtual void load(std::span<const Row>) = 0;
  virtual void begin(bool write) = 0;
  virtual void end() = 0;
  virtual std::optional<Record> get(Key) = 0;
  /// Caller proved existence through get/scan in this same request. This avoids
  /// forcing LMDB to repeat an unnecessary existence lookup before mdb_put.
  virtual bool update(Key, Record) = 0;
  virtual bool insert(Key, Record) = 0;
  virtual bool erase(Key) = 0;
  virtual std::size_t scan(Key first, std::span<Row>) = 0;
  /// A bounded, read-only nonsuspending batch. Serial default preserves each
  /// logical request's begin/end semantics (including LMDB's actual snapshot).
  virtual void read_batch(std::span<const Operation>, std::span<Outcome>, ReadExecution);
  /// Cold preparation only; it must not execute a request or touch row data.
  /// All subsequent admission, visits and retirement belong to timed batches.
  virtual void prepare_read_batch(ReadExecution, std::size_t active_width = 16);
  virtual void start_preparation(std::size_t count, int = -1, bool = true) {
    if (count) throw std::invalid_argument("prepared pages require Packed");
  }
  virtual PreparationStats preparation_stats(bool = false) const { return {}; }
  virtual PreparationDemands preparation_demands() const { return {}; }
  virtual void drain() {}
  virtual void audit() {}
  /// Skip inventory walks when capturing operation deltas after conditioning.
  virtual Counters counters(bool = true) const { return {}; }
  /// Cold post-interval diagnostic. Regions may overlap and are representation
  /// extents, not a measured set of accessed cache lines or served load tiers.
  virtual std::vector<MemoryRegion> memory_regions() const { return {}; }
};
std::vector<Row> initial_rows(std::size_t count, std::uint64_t seed);
Trace make_trace(std::string_view name, std::size_t records,
                 std::size_t operations, std::uint64_t seed);
std::vector<std::string> workload_names();
/// Malformed kind/scan extent/write value rejects before begin. After begin,
/// any unexpected backend exception makes this trial abort-only: discard the
/// Store. No rollback is promised for Packed, and end must not commit partial
/// LMDB writes during unwinding. A successful request calls end exactly once.
Outcome execute(Store&, const Operation&);
inline bool lookup_batch_eligible(Kind kind) { return kind == Kind::read || kind == Kind::chain; }
inline bool mutates(Kind kind) {
  return kind == Kind::update || kind == Kind::insert || kind == Kind::erase ||
         kind == Kind::rmw || kind == Kind::order;
}
namespace detail {
inline void mix(std::uint64_t& hash, std::uint64_t value) {
  // A semantic checksum; capture/result receipts supply SHA-256 separately.
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
}
}
inline void consume_row(Outcome& result, Key key, const Record& record) {
  detail::mix(result.digest, key); detail::mix(result.digest, record.next);
  detail::mix(result.digest, record.value); detail::mix(result.digest, record.tag);
  ++result.rows;
}
inline void consume_absent(Outcome& result, Key key) {
  detail::mix(result.digest, key); detail::mix(result.digest, 0);
}
std::unique_ptr<Store> reference_store();
std::unique_ptr<Store> lmdb_store(const std::string& directory, std::size_t map_bytes,
                                bool read_batch64 = false);
enum class Access { owner_ready, fresh_hold, retained_hold };
std::unique_ptr<Store> packed_store(std::uint64_t byte_limit,
                                  Access access = Access::owner_ready, bool check_effects = false);
std::string lmdb_version();
std::uint64_t fingerprint(std::span<const Row>);
} // namespace orbital_bench
