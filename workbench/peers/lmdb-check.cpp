#include <lmdb.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
void check(int status) {
  if (status) throw std::runtime_error(mdb_strerror(status));
}
void require(bool value) {
  if (!value) throw std::runtime_error("LMDB native consequence check failed");
}
using Transaction = std::unique_ptr<MDB_txn, decltype(&mdb_txn_abort)>;
Transaction begin(MDB_env* env, unsigned flags) {
  MDB_txn* txn{};
  check(mdb_txn_begin(env, nullptr, flags, &txn));
  return {txn, mdb_txn_abort};
}
MDB_val bytes(std::string_view value) {
  return {value.size(), const_cast<char*>(value.data())};
}
void put(MDB_txn* txn, MDB_dbi db, std::string_view key, std::string_view value) {
  auto k = bytes(key), v = bytes(value);
  check(mdb_put(txn, db, &k, &v, 0));
}
std::string_view get(MDB_txn* txn, MDB_dbi db, std::string_view key) {
  auto k = bytes(key);
  MDB_val value{};
  check(mdb_get(txn, db, &k, &value));
  // Borrowed directly from LMDB; consume while the caller's transaction lives.
  return {static_cast<const char*>(value.mv_data), value.mv_size};
}
}
int main(int argc, char** argv) try {
  require(argc == 2 && std::filesystem::create_directory(argv[1]));
  MDB_env* raw{};
  check(mdb_env_create(&raw));
  std::unique_ptr<MDB_env, decltype(&mdb_env_close)> env(raw, mdb_env_close);
  check(mdb_env_set_mapsize(env.get(), 16u << 20));
  // This test deliberately permits two reader handles on one caller. These
  // flags are explicit test choices, not defaults supplied by the peer target.
  const unsigned flags = MDB_NOSYNC | MDB_NOMETASYNC | MDB_NOTLS;
  check(mdb_env_open(env.get(), argv[1], flags, 0600));
  MDB_dbi db{};
  {
    auto write = begin(env.get(), 0);
    check(mdb_dbi_open(write.get(), nullptr, 0, &db));
    put(write.get(), db, "key", "before");
    check(mdb_txn_commit(write.release()));
  }
  auto retained = begin(env.get(), MDB_RDONLY);
  require(get(retained.get(), db, "key") == "before");
  auto reusable = begin(env.get(), MDB_RDONLY);
  require(get(reusable.get(), db, "key") == "before");
  mdb_txn_reset(reusable.get());
  {
    auto write = begin(env.get(), 0);
    put(write.get(), db, "key", "after");
    auto key = bytes("reserved");
    MDB_val value{3, nullptr};
    check(mdb_put(write.get(), db, &key, &value, MDB_RESERVE));
    std::memcpy(value.mv_data, "abc", 3);
    check(mdb_txn_commit(write.release()));
  }
  require(get(retained.get(), db, "key") == "before");
  check(mdb_txn_renew(reusable.get()));
  require(get(reusable.get(), db, "key") == "after");
  {
    auto fresh = begin(env.get(), MDB_RDONLY);
    require(get(fresh.get(), db, "key") == "after");
    require(get(fresh.get(), db, "reserved") == "abc");
    MDB_cursor* cursor{};
    check(mdb_cursor_open(fresh.get(), db, &cursor));
    std::unique_ptr<MDB_cursor, decltype(&mdb_cursor_close)> owned(cursor, mdb_cursor_close);
    auto key = bytes("q");
    MDB_val value{};
    check(mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE));
    require(std::string_view(static_cast<char*>(key.mv_data), key.mv_size) == "reserved");
    require(std::string_view(static_cast<char*>(value.mv_data), value.mv_size) == "abc");
    require(mdb_cursor_get(cursor, &key, &value, MDB_NEXT) == MDB_NOTFOUND);
  }
  try {
    auto write = begin(env.get(), 0);
    put(write.get(), db, "key", "must abort");
    throw 1;
  } catch (int) {}
  auto fresh = begin(env.get(), MDB_RDONLY);
  require(get(fresh.get(), db, "key") == "after");
  unsigned actual_flags{};
  check(mdb_env_get_flags(env.get(), &actual_flags));
  require((actual_flags & flags) == flags);
  std::cout << mdb_version(nullptr, nullptr, nullptr)
            << " flags=" << actual_flags << " snapshot/abort/reserve/cursor checks passed\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
