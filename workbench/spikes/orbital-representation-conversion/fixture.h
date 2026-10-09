#pragma once
#include <orbital/allocation.h>
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

// Disposable representation exercise, not an Engine schema or container API.
namespace orbital_conversion_fixture {
constexpr std::size_t max_fields = 8;
using Record = std::array<std::uint64_t, max_fields>;
struct Schema {
  // Supported fixture widths: 1, 7, 8, 9, 12, 20, 31, 32, 63, 64.
  std::vector<unsigned> widths;
  bool operator==(const Schema&) const = default;
};
struct LogicalIdentity {
  orbital::ObjectId object{};
  std::uint64_t version{};
  bool operator==(const LogicalIdentity&) const = default;
};
enum class Kind { columns, rows };
enum class Codec { seriespack, tuplepack };
enum class Error { schema, value, allocation, format, injected, incomplete };
struct Costs {
  std::uint64_t source_values{}, target_values{}, decode_bindings{}, construction_bindings{};
  std::uint64_t allocations{}, allocated_body_bytes{}, padding_bytes{};
  std::uint64_t issued_bytes{}, issued_spans{}, completed_units{};
  std::uint64_t scratch_bytes{}, descriptor_bytes{};
  std::uint64_t construction_calls{}, guard_initialization_bytes{};
};
struct BuildOptions {
  // Columns count completed columns; rows count completed row units.
  std::uint64_t fail_after_units{UINT64_MAX};
  std::uint64_t fail_after_allocations{UINT64_MAX};
  bool reject_completion{};
};
struct Part {
  orbital::PartId id{}; // Only meaningful under this descriptor; conversion may change it.
  Codec codec{};
  std::size_t records{};
  unsigned width{}; // Series column width; zero for a Tuple row Part.
  orbital::Bytes body;
  std::size_t readable_padding{}, alignment{};
  orbital::Bytes interpretation; // Owned serialized Ikea physical description.
};
struct Description {
  LogicalIdentity logical;
  Kind kind{};
  std::size_t records{};
  std::span<const unsigned> field_widths;
  std::span<const Part> parts;
};

class Representation;
using Completed = std::shared_ptr<const Representation>;
using Result = std::expected<Completed, Error>;

class Representation {
public:
  struct Impl;
  ~Representation();
  Representation(const Representation&) = delete;
  Representation& operator=(const Representation&) = delete;
  /// Borrows this immutable closure. Retain Completed through access/waits.
  Description describe() const noexcept;
private:
  explicit Representation(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
  friend Result construct(orbital::allocation::Pool&, LogicalIdentity, const Schema&,
                          std::span<const Record>, Kind, Costs&, BuildOptions);
  friend std::expected<std::vector<Record>, Error> read_records(const Representation&, Costs*);
};

/// Returns only completed immutable targets; failures discard private backing.
/// Pool/Lane outlive all descriptors and actual physical settlement.
Result construct(orbital::allocation::Pool&, LogicalIdentity, const Schema&,
                 std::span<const Record>, Kind, Costs&, BuildOptions = {});
Result convert(orbital::allocation::Pool&, const Representation&, Kind, Costs&,
               BuildOptions = {});
std::expected<std::vector<Record>, Error> read_records(const Representation&, Costs* = nullptr);

/// Standalone selected-Part adapter: interpretation is retained by the caller.
/// One Part per Series column; one Tuple Part for all active fields/records.
std::expected<std::vector<Record>, Error> read_parts(const Description&, Costs* = nullptr);
} // namespace orbital_conversion_fixture
