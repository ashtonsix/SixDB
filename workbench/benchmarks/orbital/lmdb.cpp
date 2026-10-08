#include "workload.h"

#include <lmdb.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace orbital_bench {
namespace {
void check(int code) {
  if (code) throw std::runtime_error(std::string("LMDB: ") + mdb_strerror(code));
}
std::array<unsigned char, 8> encode(Key key) {
  std::array<unsigned char, 8> bytes;
  for (unsigned i = 0; i < 8; ++i) bytes[7 - i] = key >> (i * 8);
  return bytes;
}
Key decode(const MDB_val& val) {
  if (val.mv_size != 8) throw std::runtime_error("LMDB key extent mismatch");
  Key key = 0;
  for (unsigned i = 0; i < 8; ++i) key = (key << 8) | static_cast<unsigned char*>(val.mv_data)[i];
  return key;
}
Record record(const MDB_val& val) {
  if (val.mv_size != sizeof(Record)) throw std::runtime_error("LMDB record extent mismatch");
  Record result; std::memcpy(&result, val.mv_data, sizeof(result)); return result;
}
class Lmdb final : public Store {
  MDB_env* env_{};
  MDB_txn* txn_{};
  MDB_dbi db_{};
  bool writing_{};
  bool read_batch64_{}, request_active_{};
  unsigned reads_{};
public:
  Lmdb(const std::string& directory, std::size_t map_bytes, bool read_batch64) : read_batch64_(read_batch64) {
    // The runner supplies a new disposable directory. Refuse existing contents
    // rather than clearing arbitrary paths or reusing a previously mutated DB.
    if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory))
      throw std::runtime_error("LMDB directory must be empty");
    std::filesystem::create_directories(directory);
    check(mdb_env_create(&env_));
    check(mdb_env_set_mapsize(env_, map_bytes));
    // No durable-sync comparison is possible before Orbital's object/journal
    // stage. Keep ordinary LMDB locking and COW/txn semantics; do not WRITEMAP.
    check(mdb_env_open(env_, directory.c_str(), MDB_NOSYNC | MDB_NOMETASYNC, 0600));
    begin(true); check(mdb_dbi_open(txn_, nullptr, 0, &db_)); end();
  }
  ~Lmdb() override {
    if (txn_) mdb_txn_abort(txn_);
    if (env_) mdb_env_close(env_);
  }
  void load(std::span<const Row> rows) override {
    begin(true);
    for (const auto& row : rows) {
      auto encoded = encode(row.key);
      MDB_val key{encoded.size(), encoded.data()}, data{sizeof(row.record), const_cast<Record*>(&row.record)};
      check(mdb_put(txn_, db_, &key, &data, MDB_NOOVERWRITE));
    }
    end();
  }
  void begin(bool write) override {
    if (request_active_) throw std::runtime_error("nested benchmark transaction");
    request_active_ = true;
    if (txn_ && write) { mdb_txn_abort(txn_); txn_ = nullptr; }
    if (txn_) return; // Same read snapshot for at most 64 adjacent requests.
    reads_ = 0;
    writing_ = write; check(mdb_txn_begin(env_, nullptr, write ? 0 : MDB_RDONLY, &txn_));
  }
  void end() override {
    request_active_ = false;
    if (!writing_ && read_batch64_ && ++reads_ < 64) return;
    auto txn = txn_; txn_ = nullptr;
    if (writing_) check(mdb_txn_commit(txn)); else mdb_txn_abort(txn);
  }
  void drain() override {
    if (txn_) { mdb_txn_abort(txn_); txn_ = nullptr; }
  }
  Counters counters(bool include_storage = true) const override {
    if (!include_storage) return {};
    MDB_stat stat{}; MDB_envinfo info{};
    check(mdb_env_stat(env_, &stat)); check(mdb_env_info(env_, &info));
    Counters counts;
    counts.lmdb_file_used_bytes = (info.me_last_pgno + 1) * stat.ms_psize;
    return counts;
  }
  std::optional<Record> get(Key id) override {
    auto encoded = encode(id); MDB_val key{encoded.size(), encoded.data()}, data;
    auto result = mdb_get(txn_, db_, &key, &data);
    if (result == MDB_NOTFOUND) return std::nullopt;
    check(result); return record(data);
  }
  bool update(Key id, Record value) override {
    auto encoded = encode(id); MDB_val key{encoded.size(), encoded.data()}, data{sizeof(value), &value};
    check(mdb_put(txn_, db_, &key, &data, 0)); return true;
  }
  bool insert(Key id, Record value) override {
    auto encoded = encode(id); MDB_val key{encoded.size(), encoded.data()}, data{sizeof(value), &value};
    auto result = mdb_put(txn_, db_, &key, &data, MDB_NOOVERWRITE);
    if (result == MDB_KEYEXIST) return false;
    check(result); return true;
  }
  bool erase(Key id) override {
    auto encoded = encode(id); MDB_val key{encoded.size(), encoded.data()};
    auto result = mdb_del(txn_, db_, &key, nullptr);
    if (result == MDB_NOTFOUND) return false;
    check(result); return true;
  }
  std::size_t scan(Key first, std::span<Row> output) override {
    if (output.empty()) return 0;
    MDB_cursor* cursor{}; check(mdb_cursor_open(txn_, db_, &cursor));
    auto encoded = encode(first); MDB_val key{encoded.size(), encoded.data()}, data;
    std::size_t used = 0;
    auto status = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (!status && used < output.size()) {
      output[used++] = {decode(key), record(data)};
      if (used < output.size()) status = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }
    mdb_cursor_close(cursor);
    if (status != MDB_NOTFOUND) check(status);
    return used;
  }
};
} // namespace
std::unique_ptr<Store> lmdb_store(const std::string& directory, std::size_t map_bytes, bool read_batch64) {
  return std::make_unique<Lmdb>(directory, map_bytes, read_batch64);
}
std::string lmdb_version() { return mdb_version(nullptr, nullptr, nullptr); }
} // namespace orbital_bench
