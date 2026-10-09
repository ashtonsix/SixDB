#include "lmdb.h"

namespace orbital_versioned_performance {
namespace {
Key decode(const MDB_val& value) {
  if (value.mv_size != 8) throw std::runtime_error("LMDB key extent mismatch");
  Key result{};
  for (unsigned i=0; i<8; ++i) result=(result<<8)|static_cast<const unsigned char*>(value.mv_data)[i];
  return result;
}
struct Cursor {
  MDB_cursor* value{};
  ~Cursor() { if (value) mdb_cursor_close(value); }
};
}
Lmdb::Lmdb(const std::filesystem::path& directory, std::size_t byte_limit) {
  if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory))
    throw std::runtime_error("LMDB directory must be new or empty");
  std::filesystem::create_directories(directory);
  lmdb_check(mdb_env_create(&env_));
  try {
    lmdb_check(mdb_env_set_mapsize(env_, byte_limit));
    // Embedded comparison: ordinary LMDB locking/COW/publication; flushes off.
    lmdb_check(mdb_env_open(env_, directory.c_str(), MDB_NOSYNC|MDB_NOMETASYNC|MDB_NOTLS, 0600));
    auto write=edit(); lmdb_check(mdb_dbi_open(write.txn_, nullptr, 0, &db_)); write.publish();
  } catch (...) { mdb_env_close(env_); env_=nullptr; throw; }
}
Lmdb::~Lmdb() { if (env_) mdb_env_close(env_); }
Lmdb::Read Lmdb::read() const {
  MDB_txn* txn{}; lmdb_check(mdb_txn_begin(env_,nullptr,MDB_RDONLY,&txn)); return Read(txn,db_);
}
Lmdb::Write Lmdb::edit() {
  MDB_txn* txn{}; lmdb_check(mdb_txn_begin(env_,nullptr,0,&txn)); return Write(txn,db_);
}
void Lmdb::load(std::span<const Row> rows) {
  auto write=edit(); for (const auto& row:rows) write.put(row.key,row.record,true); write.publish();
}
void Lmdb::Write::put(Key key, Record record, bool insert) {
  auto encoded=lmdb_key(key); MDB_val id{encoded.size(),encoded.data()}, value{sizeof(record),&record};
  lmdb_check(mdb_put(txn_,db_,&id,&value,insert?MDB_NOOVERWRITE:0));
}
void Lmdb::Write::put_part(Address address,orbital::Bytes body) {
  auto encoded=lmdb_address(address); MDB_val id{encoded.size(),encoded.data()},value{body.size(),const_cast<std::byte*>(body.data())};
  lmdb_check(mdb_put(txn_,db_,&id,&value,0));
}
orbital::MutableBytes Lmdb::Write::reserve_part(Address address,std::size_t bytes) {
  auto encoded=lmdb_address(address); MDB_val id{encoded.size(),encoded.data()},value{bytes,nullptr};
  lmdb_check(mdb_put(txn_,db_,&id,&value,MDB_RESERVE));
  return {static_cast<std::byte*>(value.mv_data),value.mv_size};
}
void Lmdb::Write::publish() {
  auto* txn=std::exchange(txn_,nullptr); lmdb_check(mdb_txn_commit(txn));
}
std::size_t Lmdb::Read::scan(Key first, std::span<Row> output) const {
  if (output.empty()) return 0;
  Cursor cursor; lmdb_check(mdb_cursor_open(txn_,db_,&cursor.value));
  auto encoded=lmdb_key(first); MDB_val key{encoded.size(),encoded.data()}, value{};
  auto status=mdb_cursor_get(cursor.value,&key,&value,MDB_SET_RANGE); std::size_t count{};
  while (!status && count<output.size()) {
    output[count++]={decode(key),lmdb_record(value)};
    if (count<output.size()) status=mdb_cursor_get(cursor.value,&key,&value,MDB_NEXT);
  }
  if (status!=MDB_NOTFOUND) lmdb_check(status);
  return count;
}
Lmdb::Footprint Lmdb::footprint() const {
  MDB_envinfo info{}; MDB_stat stat{};
  lmdb_check(mdb_env_info(env_,&info)); lmdb_check(mdb_env_stat(env_,&stat));
  mdb_filehandle_t fd{}; lmdb_check(mdb_env_get_fd(env_,&fd));
  const auto file_bytes=std::filesystem::file_size(std::filesystem::path("/proc/self/fd")/std::to_string(fd));
  return {info.me_mapsize,(info.me_last_pgno+1)*stat.ms_psize,
          file_bytes,
          stat.ms_branch_pages+stat.ms_leaf_pages+stat.ms_overflow_pages,stat.ms_depth};
}
} // namespace orbital_versioned_performance
