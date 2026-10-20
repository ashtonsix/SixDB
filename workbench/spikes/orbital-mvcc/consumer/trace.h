#pragma once
// Private Object/codec integration-check input; not an Engine workload interface.
#include <cstdint>
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
struct Trace {
  std::vector<Operation> operations;
  std::uint64_t fingerprint{};
};
std::vector<Row> initial_rows(std::size_t count, std::uint64_t seed);
Trace make_trace(std::string_view, std::size_t records, std::size_t operations, std::uint64_t seed);
std::uint64_t fingerprint(std::span<const Row>);
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
} // namespace orbital_bench
