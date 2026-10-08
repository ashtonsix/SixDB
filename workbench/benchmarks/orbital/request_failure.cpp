#include "workload.h"
#include <lmdb.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace b = orbital_bench;
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void preflight() {
  struct Probe final : b::Store {
    unsigned calls{};
    void load(std::span<const b::Row>) override { ++calls; }
    void begin(bool) override { ++calls; }
    void end() override { ++calls; }
    std::optional<b::Record> get(b::Key) override { ++calls; return b::Record{}; }
    bool update(b::Key, b::Record) override { ++calls; return true; }
    bool insert(b::Key, b::Record) override { ++calls; return true; }
    bool erase(b::Key) override { ++calls; return true; }
    std::size_t scan(b::Key, std::span<b::Row>) override { ++calls; return 0; }
  } probe;
  const std::array invalid{
    b::Operation{b::Kind::scan, 17, {}, 65}, b::Operation{b::Kind::status, 17, {}, 65},
    b::Operation{b::Kind::stock, 17, {}, 65}, b::Operation{b::Kind::order, 17, {}, 65},
    b::Operation{b::Kind::insert, 17, {0, b::value_mask + 1, 0}},
    b::Operation{b::Kind::update, 17, {0, b::value_mask + 1, 0}},
    b::Operation{b::Kind::order, 17, {49, b::value_mask + 1, 0}},
    b::Operation{static_cast<b::Kind>(99), 17, {}, 1}
  };
  for (const auto& operation : invalid) {
    bool rejected = false;
    try { (void)b::execute(probe, operation); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && probe.calls == 0, "malformed operation touched Store before rejection");
  }
  require(b::execute(probe, {b::Kind::read, 17, {}, 1}).rows == 1 && probe.calls == 3,
          "healthy request after preflight refusals differs");
}

// Throw after a real LMDB write. An unwind-time end() would commit that partial
// order request; destroying the trial must abort it instead.
struct FailingOrder final : b::Store {
  std::unique_ptr<b::Store> storage;
  unsigned& ends;
  bool wrote{};
  FailingOrder(std::unique_ptr<b::Store> value, unsigned& end_calls)
      : storage(std::move(value)), ends(end_calls) {}
  void load(std::span<const b::Row> rows) override { storage->load(rows); }
  void begin(bool write) override { storage->begin(write); }
  void end() override { ++ends; storage->end(); }
  std::optional<b::Record> get(b::Key key) override { return storage->get(key); }
  bool update(b::Key key, b::Record record) override {
    require(storage->update(key, record), "injected update did not write");
    wrote = true;
    throw std::runtime_error("injected failure after actual LMDB update");
  }
  bool insert(b::Key key, b::Record record) override { return storage->insert(key, record); }
  bool erase(b::Key key) override { return storage->erase(key); }
  std::size_t scan(b::Key key, std::span<b::Row> output) override { return storage->scan(key, output); }
};
void lmdb_abort(const std::filesystem::path& directory, bool batch64) {
  const std::array<b::Row, 2> initial{{{17, {49, 7, 9}}, {49, {17, 11, 13}}}};
  unsigned ends = 0;
  {
    FailingOrder store{b::lmdb_store(directory.string(), 64u << 20, batch64), ends};
    store.load(initial);
    bool failed = false;
    try { (void)b::execute(store, {b::Kind::order, 17, {81, 3, 5}, 2}); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed && store.wrote && ends == 0, "partial request was committed or injected write not reached");
  }
  // Independent reopen: inspect the committed DB rather than reuse the fixture
  // or infer rollback from a method-call counter.
  MDB_env* environment{};
  require(mdb_env_create(&environment) == 0, "LMDB verification environment failed");
  struct Close { MDB_env* environment; ~Close() { mdb_env_close(environment); } } close{environment};
  require(mdb_env_open(environment, directory.c_str(), MDB_RDONLY, 0600) == 0,
          "LMDB independent reopen failed");
  MDB_txn* transaction{};
  require(mdb_txn_begin(environment, nullptr, MDB_RDONLY, &transaction) == 0, "LMDB verification transaction failed");
  struct Abort { MDB_txn* transaction; ~Abort() { mdb_txn_abort(transaction); } } abort{transaction};
  MDB_dbi database{};
  require(mdb_dbi_open(transaction, nullptr, 0, &database) == 0, "LMDB verification database failed");
  MDB_stat statistics{};
  require(mdb_stat(transaction, database, &statistics) == 0 && statistics.ms_entries == initial.size(),
          "failed order changed committed row inventory");
  for (const auto& row : initial) {
    std::array<unsigned char, 8> encoded{};
    encoded.back() = static_cast<unsigned char>(row.key); // Hand-picked one-byte keys.
    MDB_val key{encoded.size(), encoded.data()}, value{};
    require(mdb_get(transaction, database, &key, &value) == 0 && value.mv_size == sizeof(b::Record),
            "committed initial row missing");
    b::Record record;
    std::memcpy(&record, value.mv_data, sizeof(record));
    require(record == row.record, "unwinding committed an actual partial LMDB mutation");
  }
}
} // namespace
int main(int argc, char** argv) try {
  if (argc != 2 && argc != 3) throw std::invalid_argument("usage: orbital_request_failure_check NEW-DIRECTORY [abort-only]");
  const std::filesystem::path root = argv[1];
  if (argc == 2) preflight();
  else require(std::string_view(argv[2]) == "abort-only", "unknown check selection");
  lmdb_abort(root / "lmdb", false);
  lmdb_abort(root / "lmdb64", true);
  std::cout << "request preflight and actual partial-write LMDB abort checks passed\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
