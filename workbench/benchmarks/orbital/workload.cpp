#include "workload.h"

#include <algorithm>
#include <array>
#include <bit>
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

void Store::prepare_read_batch(ReadExecution execution, std::size_t active_width) {
  if (!active_width || active_width > 64)
    throw std::invalid_argument("active read width must be in [1,64]");
  if (execution != ReadExecution::serial)
    throw std::invalid_argument("backend has no requested staged read execution");
}
void Store::read_batch(std::span<const Operation> operations, std::span<Outcome> outcomes,
                       ReadExecution execution) {
  if (execution != ReadExecution::serial)
    throw std::invalid_argument("backend has no requested staged read execution");
  if (operations.size() > 64 || outcomes.size() != operations.size() ||
      !std::all_of(operations.begin(), operations.end(), [](const Operation& op) { return lookup_batch_eligible(op.kind); }))
    throw std::invalid_argument("read batch requires matching bounded read-only inputs");
  for (std::size_t i = 0; i < operations.size(); ++i) outcomes[i] = execute(*this, operations[i]);
}

std::vector<std::string> workload_names() {
  return {"point-hit", "point-miss", "point-update", "point-rmw", "insert-random",
          "delete", "append", "scan-16", "dependent-16", "ycsb-a", "ycsb-b",
          "ycsb-c", "ycsb-d", "ycsb-e", "ycsb-f", "order-entry",
          "order-status", "stock-range", "insert-scan"};
}

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
  auto names = workload_names();
  if (std::find(names.begin(), names.end(), name) == names.end() || !records || !operations)
    throw std::invalid_argument("unknown workload or empty trace");
  Trace trace;
  // Measure successful deletes rather than letting a long trace become mostly
  // absent-key lookups after the initial data has already been removed.
  if (name == "delete") operations = std::min(operations, records);
  trace.operations.reserve(operations);
  Random random{seed};
  auto choose = [&] {
    // An explicit 80/20 hot-set mix for inspired workloads, not YCSB's Zipfian
    // generator. Every backend consumes these exact same operation bytes.
    auto limit = name.starts_with("ycsb-") && random.next() % 100 < 80
                     ? std::max<std::size_t>(1, records / 10) : records;
    return key_at(random.next() % limit);
  };
  std::size_t appended = 0;
  std::uint64_t erase_ordinal = 0;
  const auto erase_domain = std::bit_ceil(std::uint64_t(records));
  for (std::size_t i = 0; i < operations; ++i) {
    Operation op{Kind::read, choose(), {choose(),
                  std::uint32_t(random.next()) & value_mask,
                  std::uint32_t(random.next())}, 1};
    auto percent = random.next() % 100;
    if (name == "point-miss") ++op.key;
    else if (name == "point-update") op.kind = Kind::update;
    else if (name == "point-rmw") op.kind = Kind::rmw;
    else if (name == "delete") {
      // A bijection over the enclosing power-of-two domain, rejecting excess
      // ranks, removes distinct rows across the whole population. Partial
      // traces expose sparse retained pages rather than emptying a prefix.
      std::uint64_t rank;
      do { rank = (erase_ordinal++ * 0x9e3779b97f4a7c15ull + seed) & (erase_domain - 1); }
      while (rank >= records);
      op.kind = Kind::erase; op.key = key_at(rank);
    }
    else if (name == "insert-random") {
      op.kind = Kind::insert;
      // Unique sparse keys arrive in nonmonotonic order. A bijective modular
      // permutation over a power-of-two domain keeps outcomes reproducible.
      auto domain = std::bit_ceil(std::uint64_t(records));
      op.key = key_at(((i * 0x9e3779b97f4a7c15ull) & (domain - 1)) % records) +
               1 + 2 * (i / records);
      // Uniqueness is not assumed: both backends expose duplicate rejection.
    } else if (name == "append") { op.kind = Kind::insert; op.key = key_at(records + appended++); }
    else if (name == "insert-scan") {
      // A distinct 50/50 nonmonotonic insertion/ordered-scan mix. Scans see
      // physical fragmentation caused by real earlier insertions in this trace.
      if (percent < 50) {
        op.kind = Kind::insert;
        const auto domain = std::bit_ceil(std::uint64_t(records));
        op.key = key_at(((appended * 0x9e3779b97f4a7c15ull) & (domain - 1)) % records) +
                 1 + 2 * (appended / records);
        ++appended;
      } else { op.kind = Kind::scan; op.count = 16; }
    }
    else if (name == "scan-16") { op.kind = Kind::scan; op.count = 16; }
    else if (name == "dependent-16") { op.kind = Kind::chain; op.count = 16; }
    else if (name == "ycsb-a") op.kind = percent < 50 ? Kind::read : Kind::update;
    else if (name == "ycsb-b") op.kind = percent < 95 ? Kind::read : Kind::update;
    else if (name == "ycsb-d") {
      if (percent < 5) { op.kind = Kind::insert; op.key = key_at(records + appended++); }
      else {
        const auto total = records + appended;
        const auto recent = std::max<std::size_t>(1, total / 10);
        op.key = key_at(total - 1 - random.next() % recent);
      }
    } else if (name == "ycsb-e") {
      if (percent < 5) { op.kind = Kind::insert; op.key = key_at(records + appended++); }
      else { op.kind = Kind::scan; op.count = 1 + random.next() % 32; }
    } else if (name == "ycsb-f") op.kind = percent < 50 ? Kind::read : Kind::rmw;
    else if (name == "order-entry") {
      op.kind = Kind::order; op.count = 8; op.record.next = key_at(records + appended++);
    }
    else if (name == "order-status") { op.kind = Kind::status; op.count = 16; }
    else if (name == "stock-range") { op.kind = Kind::stock; op.count = 32; }
    trace.operations.push_back(op);
    mix(trace.fingerprint, std::uint64_t(op.kind)); mix(trace.fingerprint, op.key);
    mix(trace.fingerprint, op.record.next); mix(trace.fingerprint, op.record.value);
    mix(trace.fingerprint, op.record.tag); mix(trace.fingerprint, op.count);
  }
  return trace;
}

Outcome execute(Store& store, const Operation& op) {
  // Reject malformed inputs before a transaction, Hold or partial mutation.
  if (unsigned(op.kind) > unsigned(Kind::stock))
    throw std::invalid_argument("unknown operation kind");
  if ((op.kind == Kind::scan || op.kind == Kind::status ||
       op.kind == Kind::stock || op.kind == Kind::order) && op.count > 64)
    throw std::invalid_argument("scan exceeds scratch capacity");
  if ((op.kind == Kind::insert || op.kind == Kind::update || op.kind == Kind::order) &&
      op.record.value > value_mask)
    throw std::invalid_argument("over-width request value");
  Outcome result;
  store.begin(mutates(op.kind));
  if (op.kind == Kind::insert) {
    result.changes = store.insert(op.key, op.record); mix(result.digest, result.changes);
  } else if (op.kind == Kind::erase) {
    result.changes = store.erase(op.key); mix(result.digest, result.changes);
  } else if (op.kind == Kind::update) {
    // Field update preserves the dependency link, matching a partial record
    // update rather than destroying the pointer-chain input on one backend.
    if (auto old = store.get(op.key)) {
      old->value = op.record.value; old->tag = op.record.tag;
      result.changes = store.update(op.key, *old);
    }
    mix(result.digest, result.changes);
  } else if (op.kind == Kind::scan || op.kind == Kind::status ||
             op.kind == Kind::stock || op.kind == Kind::order) {
    std::array<Row, 64> rows;
    auto count = store.scan(op.key, std::span(rows).first(op.count));
    for (std::size_t i = 0; i < count; ++i) {
      auto& row = rows[i];
      add(result, row.key, row.record);
      if (op.kind == Kind::order) {
        row.record.value = (row.record.value - 1) & value_mask;
        result.changes += store.update(row.key, row.record);
      } else if (op.kind == Kind::stock) {
        mix(result.digest, row.record.value < op.record.value);
      }
    }
    if (op.kind == Kind::order)
      result.changes += store.insert(op.record.next, {op.key, op.record.value, op.record.tag});
  } else {
    auto key = op.key;
    auto count = op.kind == Kind::chain ? op.count : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
      auto record = store.get(key);
      if (!record) { mix(result.digest, key); mix(result.digest, 0); break; }
      add(result, key, *record);
      if (op.kind == Kind::rmw) {
        record->value = (record->value + 1) & value_mask;
        ++record->tag;
        result.changes += store.update(key, *record);
      }
      key = record->next;
    }
  }
  store.end();
  return result;
}

std::uint64_t fingerprint(std::span<const Row> rows) {
  Outcome result;
  for (const auto& row : rows) add(result, row.key, row.record);
  return result.digest;
}
} // namespace orbital_bench
