#include "workload.h"
#include <array>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>

namespace b = orbital_bench;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::unique_ptr<b::Store> create(const std::string& backend, const std::filesystem::path& directory) {
  if (backend.starts_with("lmdb")) return b::lmdb_store(directory.string(), 64u << 20, backend == "lmdb-read-batch64");
  return b::packed_store(64u << 20, backend == "packed-retained" ? b::Access::retained_hold
                                  : backend == "packed-owner" ? b::Access::owner_ready
                                  : b::Access::fresh_hold, true);
}
void compare(b::Store& actual, b::Store& oracle) {
  b::Key key = 0;
  for (;;) {
    std::array<b::Row, 37> left, right;
    actual.begin(false); auto n = actual.scan(key, left); actual.end();
    oracle.begin(false); auto m = oracle.scan(key, right); oracle.end();
    require(n == m && std::equal(left.begin(), left.begin() + n, right.begin()),
            "final ordered rows differ");
    if (!n || left[n - 1].key == std::numeric_limits<b::Key>::max()) break;
    key = left[n - 1].key + 1;
  }
}
void full_key_domain(const std::string& backend, const std::filesystem::path& root) {
  auto rows = b::initial_rows(513, 61);
  const auto maximum = std::numeric_limits<b::Key>::max();
  rows.front().key = 0; rows.back().key = maximum;
  rows.front().record.next = maximum; rows.back().record.next = rows[256].key;
  rows[256].record.next = maximum - 1;
  const auto directory = root / "full-key-domain";
  auto actual = create(backend, directory); actual->load(rows);
  auto oracle = b::reference_store(); oracle->load(rows);
  auto check_state = [&] {
    // Compare actual ordered rows to the independent hand-maintained vector,
    // including a terminal maximum key that cannot advance by +1.
    std::array<b::Row, 37> output;
    b::Key next = 0;
    std::size_t position = 0;
    for (;;) {
      actual->begin(false); const auto n = actual->scan(next, output); actual->end();
      require(position + n <= rows.size() &&
              std::equal(output.begin(), output.begin() + n, rows.begin() + position),
              "full-key-domain ordered state differs");
      position += n;
      if (!n || output[n - 1].key == maximum) break;
      next = output[n - 1].key + 1;
    }
    require(position == rows.size(), "full-key-domain rows missing");
    actual->begin(false); const auto n = actual->scan(maximum, output); actual->end();
    require(n == 1 && output[0] == rows.back(), "maximum-key inclusive tail scan differs");
    for (const auto& row : rows) {
      actual->begin(false); const auto got = actual->get(row.key); actual->end();
      require(got && *got == row.record, "full-key-domain point record differs");
    }
    const std::array<b::Operation, 6> pattern{{
      {b::Kind::read, 0, {}, 1}, {b::Kind::read, maximum, {}, 1},
      {b::Kind::read, maximum - 1, {}, 1}, {b::Kind::chain, 0, {}, 16},
      {b::Kind::chain, maximum, {}, 2}, {b::Kind::chain, maximum, {}, 0}}};
    std::vector<b::Operation> input(131);
    std::vector<b::Outcome> wanted;
    for (std::size_t i = 0; i < input.size(); ++i) {
      input[i] = pattern[i % pattern.size()];
      wanted.push_back(b::execute(*oracle, input[i]));
    }
    require(wanted[0].rows == 1 && wanted[1].rows == 1 && wanted[2].rows == 0 &&
            wanted[3].rows == 3 && wanted[4].rows == 2 && wanted[5] == b::Outcome{},
            "hand-expected edge-key row counts differ");
    for (auto execution : {b::ReadExecution::serial, b::ReadExecution::native_direct,
                           b::ReadExecution::manual, b::ReadExecution::ring}) {
      if (backend != "packed-owner" && execution != b::ReadExecution::serial) continue;
      for (auto active : {std::size_t(1), std::size_t(16), std::size_t(64)}) {
        if (execution == b::ReadExecution::serial && active != 1) continue;
        actual->prepare_read_batch(execution, active);
        for (std::size_t first = 0; first < input.size(); first += 64) {
          const auto count = std::min(std::size_t(64), input.size() - first);
          const b::Outcome poison{17, 23, 29};
          std::array<b::Outcome, 66> output;
          output.fill(poison);
          actual->read_batch(std::span(input).subspan(first, count),
                             std::span(output).subspan(1, count), execution);
          require(output.front() == poison && output[count + 1] == poison,
                  "edge batch overwrote output guards");
          require(std::equal(output.begin() + 1, output.begin() + 1 + count, wanted.begin() + first),
                  "edge batch outcome/index differs");
        }
      }
    }
    actual->drain(); actual->audit(); compare(*actual, *oracle);
  };
  check_state();
  auto change = [&](const b::Operation& operation) {
    const auto result = b::execute(*actual, operation);
    require(result == b::execute(*oracle, operation), "edge mutation outcome differs");
    return result;
  };
  const b::Record replacement{rows[256].key, b::value_mask, 0xffffffffu};
  require(change({b::Kind::erase, maximum, {}}).changes == 1, "maximum erase failed");
  require(change({b::Kind::insert, maximum, replacement}).changes == 1, "maximum reinsert failed");
  require(change({b::Kind::insert, maximum, replacement}).changes == 0, "maximum duplicate accepted");
  rows.back().record = replacement;
  require(change({b::Kind::erase, 0, {}}).changes == 1, "zero erase failed");
  require(change({b::Kind::insert, 0, rows.front().record}).changes == 1, "zero reinsert failed");
  const b::Row gap{1, {maximum, 7, 9}};
  require(change({b::Kind::insert, gap.key, gap.record}).changes == 1, "zero-page fence-gap insert failed");
  rows.insert(rows.begin() + 1, gap);
  check_state();
  actual.reset();
  if (backend.starts_with("lmdb")) std::filesystem::remove_all(directory);
}
void transaction_intent() {
  struct Probe final : b::Store {
    bool writer{};
    unsigned begins{}, ends{}, mutations{};
    void load(std::span<const b::Row>) override {}
    void begin(bool value) override { writer = value; ++begins; }
    void end() override { ++ends; }
    std::optional<b::Record> get(b::Key) override { return b::Record{17, 3, 5}; }
    bool update(b::Key, b::Record) override { ++mutations; return true; }
    bool insert(b::Key, b::Record) override { ++mutations; return true; }
    bool erase(b::Key) override { ++mutations; return true; }
    std::size_t scan(b::Key, std::span<b::Row> out) override {
      if (out.empty()) return 0;
      out[0] = {17, {17, 3, 5}}; return 1;
    }
  } probe;
  const std::array reads{b::Kind::read, b::Kind::chain, b::Kind::scan, b::Kind::status, b::Kind::stock};
  const std::array writes{b::Kind::update, b::Kind::insert, b::Kind::erase, b::Kind::rmw, b::Kind::order};
  for (auto kind : reads) {
    probe.begins = probe.ends = probe.mutations = 0;
    const auto result = b::execute(probe, {kind, 17, {49, 7, 9}, 2});
    require(probe.begins == 1 && probe.ends == 1 && !probe.writer && !probe.mutations && !result.changes,
            "read-only operation opened a writer transaction or mutated rows");
  }
  for (auto kind : writes) {
    probe.begins = probe.ends = probe.mutations = 0;
    b::execute(probe, {kind, 17, {49, 7, 9}, 2});
    require(probe.begins == 1 && probe.ends == 1 && probe.writer && probe.mutations,
            "mutating operation omitted a writer transaction or write");
  }
}
void insertion_distribution() {
  constexpr std::size_t records = 4194304;
  for (const auto& name : {"insert-random", "insert-scan"}) {
    const auto trace = b::make_trace(name, records, 100000, 20261008);
    std::array<unsigned, 8> regions{};
    std::vector<bool> inserted_region(records / 256);
    unsigned inserted = 0, scans = 0, scans_in_touched_region = 0;
    for (const auto& op : trace.operations) {
      const auto rank = (op.key - 17) / 32;
      require(rank < records, "insertion trace escaped initial record domain");
      if (op.kind == b::Kind::insert) {
        ++regions[rank / (records / regions.size())]; ++inserted;
        inserted_region[rank / 256] = true;
      } else if (op.kind == b::Kind::scan) {
        ++scans; scans_in_touched_region += inserted_region[rank / 256];
      }
    }
    for (auto count : regions)
      require(count > inserted / 16 && count < inserted / 4,
              "insertion trace is confined to a prefix or badly imbalanced region");
    if (scans) require(scans_in_touched_region > scans / 2,
                       "insertion/scan trace mostly scans regions without prior insertions");
  }
}
void batch_rejections() {
  const auto erased = b::make_trace("delete", 4194304, 100000, 20261008);
  std::vector<bool> seen(4194304);
  std::array<unsigned, 8> regions{};
  for (const auto& op : erased.operations) {
    const auto rank = (op.key - 17) / 32;
    require(op.kind == b::Kind::erase && rank < seen.size() && !seen[rank], "delete trace repeats or escapes population");
    seen[rank] = true; ++regions[rank / (seen.size() / regions.size())];
  }
  for (auto count : regions)
    require(count > erased.operations.size() / 16 && count < erased.operations.size() / 4,
            "partial delete trace is confined to a prefix");
  const auto nonpower = b::make_trace("delete", 513, 1024, 20261008);
  require(nonpower.operations.size() == 513, "successful delete trace did not cap at population");
  std::array<bool, 513> small{};
  for (const auto& op : nonpower.operations) {
    const auto rank = (op.key - 17) / 32;
    require(rank < small.size() && !small[rank], "non-power-of-two deletes duplicate or escape rank");
    small[rank] = true;
  }
  // Prove rejection happens before even the first valid prefix request runs.
  struct Probe final : b::Store {
    unsigned accesses{};
    void load(std::span<const b::Row>) override { ++accesses; }
    void begin(bool) override { ++accesses; }
    void end() override { ++accesses; }
    std::optional<b::Record> get(b::Key) override { ++accesses; return {}; }
    bool update(b::Key, b::Record) override { ++accesses; return true; }
    bool insert(b::Key, b::Record) override { ++accesses; return true; }
    bool erase(b::Key) override { ++accesses; return true; }
    std::size_t scan(b::Key, std::span<b::Row>) override { ++accesses; return 0; }
  } probe;
  const b::Outcome poison{17, 23, 29};
  std::array<b::Operation, 65> input;
  input.fill({b::Kind::read, 17, {}, 1});
  std::array<b::Outcome, 65> output;
  auto rejects = [&](std::span<const b::Operation> ops, std::span<b::Outcome> out,
                     b::ReadExecution execution = b::ReadExecution::serial) {
    output.fill(poison);
    bool rejected = false;
    try { probe.read_batch(ops, out, execution); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && probe.accesses == 0, "invalid batch accessed store");
    require(std::all_of(output.begin(), output.end(), [&](auto value) { return value == poison; }),
            "invalid batch changed output");
  };
  probe.read_batch({}, {}, b::ReadExecution::serial);
  require(probe.accesses == 0, "empty batch accessed store");
  rejects(input, output);
  rejects(std::span(input).first(2), std::span(output).first(1));
  input[1].kind = b::Kind::update;
  rejects(std::span(input).first(2), std::span(output).first(2));
  input[1].kind = b::Kind::scan;
  rejects(std::span(input).first(2), std::span(output).first(2));
  for (auto execution : {b::ReadExecution::native_direct, b::ReadExecution::manual, b::ReadExecution::ring})
    rejects(std::span(input).first(1), std::span(output).first(1), execution);
}
void batch_history(const std::string& backend, const std::filesystem::path& root, std::size_t width,
                   b::ReadExecution execution = b::ReadExecution::serial, std::size_t active_width = 16) {
  auto rows = b::initial_rows(513, 123);
  // Hand-built cross-page chain terminates at a miss; zero-length chains and
  // isolated misses must retire without reading a row or inheriting state.
  rows[0].record.next = rows[255].key;
  rows[255].record.next = rows[256].key;
  rows[256].record.next = rows[512].key;
  rows[512].record.next = 0;
  const std::array<b::Operation, 6> pattern{{
    {b::Kind::read, rows[256].key, {}, 1}, {b::Kind::read, 0, {}, 1},
    {b::Kind::chain, rows[0].key, {}, 16}, {b::Kind::chain, rows[0].key, {}, 0},
    {b::Kind::chain, rows[512].key, {}, 1}, {b::Kind::chain, rows[255].key, {}, 2}
  }};
  std::vector<b::Operation> input(131);
  for (std::size_t i = 0; i < input.size(); ++i) input[i] = pattern[i % pattern.size()];
  auto oracle = b::reference_store(); oracle->load(rows);
  std::vector<b::Outcome> wanted;
  for (const auto& op : input) wanted.push_back(b::execute(*oracle, op));
  require(wanted[0].rows == 1 && wanted[1].rows == 0 && wanted[2].rows == 4 &&
          wanted[3] == b::Outcome{} && wanted[4].rows == 1 && wanted[5].rows == 2,
          "hand-expected batch row counts differ");
  const auto directory = root / ("batch-" + std::to_string(int(execution)) + "-" + std::to_string(width) +
                                  "-active" + std::to_string(active_width));
  auto actual = create(backend, directory); actual->load(rows);
  for (auto invalid_width : {std::size_t(0), std::size_t(65)}) {
    bool rejected = false;
    try { actual->prepare_read_batch(execution, invalid_width); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid active width accepted");
  }
  actual->prepare_read_batch(execution, active_width);
  actual->read_batch({}, {}, execution);
  const b::Outcome poison{17, 23, 29};
  std::array<b::Outcome, 66> output;
  auto replay = [&] {
    for (std::size_t first = 0; first < input.size(); first += width) {
      output.fill(poison);
      const auto count = std::min(width, input.size() - first);
      actual->read_batch(std::span(input).subspan(first, count),
                         std::span(output).subspan(1, count), execution);
      require(output.front() == poison && output[count + 1] == poison, "batch overwrote output bounds");
      for (std::size_t i = 0; i < count; ++i)
        require(output[i + 1] == wanted[first + i], "batch request outcome/index differs");
    }
  };
  replay();
  // Reuse freed physical slots in reverse key order, creating an actual
  // fragmented rank-to-slot map. Probe every old and inserted key afterward.
  auto change = [&](const b::Operation& op) {
    require(b::execute(*actual, op) == b::execute(*oracle, op), "batch setup mutation differs");
  };
  for (std::size_t i = 0; i < rows.size(); i += 7)
    change({b::Kind::erase, rows[i].key, {}, 1});
  for (std::size_t i = (rows.size() - 1) / 7 * 7;; i -= 7) {
    change({b::Kind::insert, rows[i].key + 1, rows[i].record});
    if (!i) break;
  }
  input.clear(); wanted.clear();
  for (const auto& row : rows) {
    input.push_back({b::Kind::read, row.key, {}, 1});
    input.push_back({b::Kind::read, row.key + 1, {}, 1});
    input.push_back({b::Kind::chain, row.key, {}, 16});
  }
  input.push_back({b::Kind::chain, 0, {}, 0});
  for (const auto& op : input) wanted.push_back(b::execute(*oracle, op));
  replay();
  actual->drain(); actual->audit(); compare(*actual, *oracle);
  actual.reset();
  if (backend.starts_with("lmdb")) std::filesystem::remove_all(directory);
}
void scattered_delete_refill(const std::string& backend, const std::filesystem::path& root) {
  const auto rows = b::initial_rows(513, 42);
  auto oracle = b::reference_store(); oracle->load(rows);
  const auto directory = root / "scattered-delete-refill";
  auto actual = create(backend, directory); actual->load(rows);
  auto change = [&](const b::Operation& op) {
    require(b::execute(*actual, op) == b::execute(*oracle, op), "scattered delete/refill outcome differs");
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto rank = (i * 257) % rows.size();
    if (rank != 0 && rank != 256 && rank != 512) change({b::Kind::erase, rows[rank].key, {}, 1});
  }
  actual->drain(); actual->audit(); compare(*actual, *oracle);
  std::array<b::Row, 4> remaining;
  actual->begin(false); const auto count = actual->scan(0, remaining); actual->end();
  require(count == 3 && remaining[0] == rows[0] && remaining[1] == rows[256] && remaining[2] == rows[512],
          "scattered deletes failed independent retained-row expectations");
  if (!backend.starts_with("lmdb")) {
    const auto counts = actual->counters();
    require(counts.logical_live_rows == 3 && counts.logical_live_field_bytes == 72,
            "sparse logical row/field-byte accounting differs");
    std::cout << "Sparse fixture: " << counts.logical_live_field_bytes << " useful field bytes; "
              << counts.live_payload_bytes << " capacity-backed payload bytes\n";
  }
  for (std::size_t i = rows.size(); i-- > 0;)
    if (i != 0 && i != 256 && i != 512) change({b::Kind::insert, rows[i].key, rows[i].record});
  actual->drain(); actual->audit(); compare(*actual, *oracle);
  if (!backend.starts_with("lmdb")) {
    const auto counts = actual->counters();
    require(counts.logical_live_rows == 513 && counts.logical_live_field_bytes == 513 * 24,
            "refill logical row/field-byte accounting differs");
  }
  actual.reset();
  if (backend.starts_with("lmdb")) std::filesystem::remove_all(directory);
}
void retired_page_refill(const std::string& backend, const std::filesystem::path& root) {
  const auto rows = b::initial_rows(8 * 256, 61);
  auto oracle = b::reference_store(); oracle->load(rows);
  auto actual = create(backend, root / "retired-page-refill"); actual->load(rows);
  auto addresses = [](b::Store& store, std::string_view kind) {
    std::set<std::uintptr_t> result;
    for (const auto& region : store.memory_regions()) if (region.kind == kind) result.insert(region.address);
    return result;
  };
  const auto original_pages = addresses(*actual, "page");
  const auto original_bindings = addresses(*actual, "bindings");
  require(original_pages.size() == 8 && original_bindings.size() == 8, "initial owner extents differ");
  auto inventory = [&](std::size_t expected_pages) {
    std::size_t pages = 0, fences = 0, metadata = 0, payload = 0;
    std::vector<std::pair<std::uintptr_t,std::uintptr_t>> metadata_regions;
    for (const auto& region : actual->memory_regions()) {
      if (region.kind == "page") ++pages;
      if (region.kind == "directory_entries") fences += region.bytes;
      if (region.kind == "lookup_driver" || region.kind == "directory_capacity" ||
          region.kind == "page" || region.kind == "bindings" ||
          region.kind == "rank_index" || region.kind == "free_bitmap")
        metadata_regions.emplace_back(region.address, region.address+region.bytes);
      if (region.kind == "allocation") payload += region.bytes;
      if (region.kind == "rank_index" || region.kind == "free_bitmap") payload -= region.bytes;
    }
    std::sort(metadata_regions.begin(),metadata_regions.end());
    std::uintptr_t covered_end{};
    for (const auto& [first,last] : metadata_regions) {
      const auto uncovered=std::max(first,covered_end);
      if (last>uncovered) metadata+=last-uncovered;
      covered_end=std::max(covered_end,last);
    }
    const auto counts = actual->counters();
    require(pages == expected_pages &&
        fences == pages * (sizeof(b::Key) + sizeof(void*)), "retired pages remain in the page/fence inventory");
    require(counts.metadata_bytes == metadata && counts.live_payload_bytes == payload,
            "counter accounting omitted an owned metadata or physical payload extent");
    return metadata;
  };
  auto change = [&](const b::Operation& operation) {
    require(b::execute(*actual, operation) == b::execute(*oracle, operation), "page retirement/refill outcome differs");
  };
  inventory(8);
  // Retire middle, head and tail pages. Every surviving Page and its named
  // Bindings keep their original addresses while fences compact around them.
  std::size_t remaining_pages = 8;
  for (auto page : {std::size_t(2), std::size_t(0), std::size_t(7)}) {
    for (std::size_t row = page * 256; row < (page + 1) * 256; ++row)
      change({b::Kind::erase, rows[row].key, {}});
    actual->drain(); actual->audit(); compare(*actual, *oracle); inventory(--remaining_pages);
    for (auto address : addresses(*actual, "page"))
      require(original_pages.contains(address), "retiring an unrelated owner moved a live Page");
    for (auto address : addresses(*actual, "bindings"))
      require(original_bindings.contains(address), "retiring an unrelated owner moved named Bindings");
  }
  std::optional<std::size_t> empty_metadata, full_metadata;
  const auto initial_reuses = actual->counters().pool_reuses;
  for (unsigned round = 0; round < 6; ++round) {
    for (const auto& row : rows) change({b::Kind::erase, row.key, {}});
    actual->drain(); actual->audit(); compare(*actual, *oracle);
    const auto empty = inventory(0);
    if (empty_metadata) require(empty == *empty_metadata, "empty metadata grew with retirement history");
    else empty_metadata = empty;
    require(actual->counters().logical_live_rows == 0, "empty store still exposes rows");
    for (const auto& row : rows) change({b::Kind::insert, row.key, row.record});
    actual->drain(); actual->audit(); compare(*actual, *oracle);
    const auto full = inventory(8);
    if (full_metadata) require(full == *full_metadata, "equal-size refill accumulated abandoned metadata");
    else full_metadata = full;
  }
  require(actual->counters().pool_reuses >= initial_reuses + 6 * 8,
          "eager empty-page retirement did not expose backing for ordinary allocator reuse");
}
int main(int argc, char** argv) try {
  if (argc != 3) throw std::runtime_error("usage: orbital_workload_check packed|packed-owner|packed-retained|lmdb|lmdb-read-batch64 EMPTY-DIRECTORY");
  const std::string backend = argv[1];
  if (backend != "packed" && backend != "packed-owner" && backend != "packed-retained" &&
      backend != "lmdb" && backend != "lmdb-read-batch64")
    throw std::runtime_error("unknown backend");
  const std::filesystem::path root = argv[2];
  transaction_intent();
  full_key_domain(backend, root);
  insertion_distribution();
  scattered_delete_refill(backend, root);
  if (!backend.starts_with("lmdb")) retired_page_refill(backend, root);
  batch_rejections();
  for (auto width : {std::size_t(1), std::size_t(3), std::size_t(16), std::size_t(63), std::size_t(64)})
    batch_history(backend, root, width);
  if (backend == "packed-owner")
    for (auto execution : {b::ReadExecution::native_direct, b::ReadExecution::manual, b::ReadExecution::ring})
      for (auto active : {std::size_t(1), std::size_t(16), std::size_t(32), std::size_t(64)})
        for (auto width : {std::size_t(1), std::size_t(3), std::size_t(16), std::size_t(63), std::size_t(64)})
          batch_history(backend, root, width, execution, active);
  std::size_t checks = 0;
  for (auto count : {std::size_t(1), std::size_t(255), std::size_t(256), std::size_t(257), std::size_t(513)}) {
    for (const auto& name : b::workload_names()) {
      auto rows = b::initial_rows(count, 123);
      auto trace = b::make_trace(name, count, 1024, 456);
      auto oracle = b::reference_store(); oracle->load(rows);
      auto actual = create(backend, root / (std::to_string(count) + "-" + name));
      actual->load(rows);
      for (const auto& op : trace.operations)
        require(b::execute(*actual, op) == b::execute(*oracle, op), "request outcome differs");
      actual->drain(); actual->audit(); compare(*actual, *oracle); ++checks;
      actual.reset();
      if (backend.starts_with("lmdb")) std::filesystem::remove_all(root / (std::to_string(count) + "-" + name));
    }
  }
  // Independent hand-expected boundary history: insertion into a fence gap,
  // duplicate rejection, final-slot split, cross-page scan, deletion and RMW.
  auto rows = b::initial_rows(256, 42);
  auto actual = create(backend, root / "boundary");
  actual->load(rows);
  actual->begin(true);
  const b::Record inserted{rows[5].key, 7, 9};
  require(actual->insert(rows[100].key + 1, inserted), "gap insert refused");
  require(!actual->insert(rows[100].key + 1, inserted), "duplicate insert accepted");
  require(actual->get(rows[100].key + 1) == inserted, "inserted value lost");
  require(!actual->get(rows[100].key + 2), "fence-gap miss reads wrong row");
  require(actual->erase(rows[127].key), "existing erase refused");
  require(!actual->get(rows[127].key), "deleted key remains visible");
  actual->end();
  std::array<b::Row, 64> scan;
  actual->begin(false); auto found = actual->scan(rows[100].key, scan); actual->end();
  require(found == 64 && scan[0] == rows[100] && scan[1].key == rows[100].key + 1,
          "split scan ordering wrong");
  require(scan[28] == rows[128], "scan did not skip deleted row");
  auto outcome = b::execute(*actual, {b::Kind::rmw, rows[100].key + 1, {}, 1});
  require(outcome.rows == 1 && outcome.changes == 1, "RMW accounting wrong");
  actual->begin(false); auto after = actual->get(rows[100].key + 1); actual->end();
  require(after && after->value == 8 && after->tag == 10 && after->next == inserted.next,
          "RMW independent expected value wrong");
  actual->drain();
  actual->audit();
  if (!backend.starts_with("lmdb")) {
    // Rejection precedes any Hold, dirty byte, summary or accounting change.
    auto before = actual->counters();
    actual->begin(true);
    bool rejected = false;
    try { actual->update(rows[100].key + 1, {0, b::value_mask + 1, 0}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "over-width update accepted");
    rejected = false;
    try { actual->insert(0, {0, b::value_mask + 1, 0}); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "over-width insert accepted");
    actual->end();
    auto after_counts = actual->counters();
    require(before.lease_calls == after_counts.lease_calls &&
            before.issued_write_bytes == after_counts.issued_write_bytes &&
            before.splits == after_counts.splits, "rejected write changed accounting");
    actual->audit();
    actual->begin(false); auto saved = actual->get(rows[100].key + 1); actual->end();
    require(saved == after, "rejected write changed data");
  }
  std::cout << backend << ": " << checks << " trace histories, batch/tail/active-width histories, rejection and independent page-boundary checks passed\n";
  actual.reset();
  if (backend.starts_with("lmdb")) std::filesystem::remove_all(root / "boundary");
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
