#include "trace.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace orbital_bench {
namespace {
using detail::mix;
struct Random {
  std::uint64_t state;
  std::uint64_t next() {
    auto z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }
};
constexpr Key key_at(std::uint64_t i) { return i * 32 + 17; }
void add(Outcome& outcome, Key key, const Record& record) {
  consume_row(outcome, key, record);
}
} // namespace

std::vector<Row> initial_rows(std::size_t count, std::uint64_t seed) {
  if (!count || count > (std::uint64_t{1} << 40))
    throw std::invalid_argument("records must be in [1, 2^40]");
  Random random{seed};
  std::vector<std::size_t> permutation(count);
  for (std::size_t i = 0; i < count; ++i) permutation[i] = i;
  for (std::size_t i = count; i > 1; --i)
    std::swap(permutation[i - 1], permutation[random.next() % i]);
  std::vector<Row> rows(count);
  // One random cyclic linked list: each next lookup genuinely depends on bytes
  // returned by the previous lookup. It is not a pre-generated address trace.
  for (std::size_t i = 0; i < count; ++i) {
    auto row = permutation[i];
    rows[row] = {key_at(row), {key_at(permutation[(i + 1) % count]),
                    std::uint32_t(random.next()) & value_mask,
                    std::uint32_t(random.next())}};
  }
  return rows;
}

Trace make_trace(std::string_view name, std::size_t records,
                 std::size_t operations, std::uint64_t seed) {
  if ((name != "point-hit" && name != "point-miss" && name != "scan-16" && name != "dependent-16") ||
      !records || !operations)
    throw std::invalid_argument("unknown integration read trace or empty input");
  Trace trace;
  trace.operations.reserve(operations);
  Random random{seed};
  auto choose = [&] { return key_at(random.next() % records); };
  for (std::size_t i = 0; i < operations; ++i) {
    Operation op{Kind::read, choose(), {choose(),
                  std::uint32_t(random.next()) & value_mask,
                  std::uint32_t(random.next())}, 1};
    // Keep the original read traces byte-for-byte after removing the unused
    // mixed/write campaign generators, including their one discarded draw.
    (void)random.next();
    if (name == "point-miss") ++op.key;
    else if (name == "scan-16") { op.kind = Kind::scan; op.count = 16; }
    else if (name == "dependent-16") { op.kind = Kind::chain; op.count = 16; }
    trace.operations.push_back(op);
    mix(trace.fingerprint, std::uint64_t(op.kind)); mix(trace.fingerprint, op.key);
    mix(trace.fingerprint, op.record.next); mix(trace.fingerprint, op.record.value);
    mix(trace.fingerprint, op.record.tag); mix(trace.fingerprint, op.count);
  }
  return trace;
}

std::uint64_t fingerprint(std::span<const Row> rows) {
  Outcome result;
  for (const auto& row : rows) add(result, row.key, row.record);
  return result.digest;
}
} // namespace orbital_bench
