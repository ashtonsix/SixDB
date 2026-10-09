#pragma once
#include "workload.h"
#include <lmdb.h>
#include <orbital/types.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <utility>

namespace orbital_versioned_performance {
using orbital_bench::Key;
using orbital_bench::Record;
using orbital_bench::Row;
struct Address {
  orbital::ObjectId object{};
  orbital::PartId part{};
  auto operator<=>(const Address&) const = default;
};

inline void lmdb_check(int code) {
  if (code) throw std::runtime_error(std::string("LMDB: ") + mdb_strerror(code));
}
inline std::array<unsigned char, 8> lmdb_key(Key key) noexcept {
  std::array<unsigned char, 8> bytes;
  for (unsigned i = 0; i < 8; ++i) bytes[7-i] = key >> (8*i);
  return bytes;
}
inline Record lmdb_record(const MDB_val& value) {
  if (value.mv_size != sizeof(Record)) throw std::runtime_error("LMDB record extent mismatch");
  Record result; std::memcpy(&result, value.mv_data, sizeof(result)); return result;
}
inline std::array<unsigned char,32> lmdb_address(Address address) noexcept {
  const std::array<std::uint64_t,4> words{address.object.high,address.object.low,address.part.high,address.part.low};
  std::array<unsigned char,32> bytes;
  for (unsigned word=0;word<4;++word) for (unsigned i=0;i<8;++i) bytes[word*8+7-i]=words[word]>>(8*i);
  return bytes;
}

// Local native control. Owning store outlives every transaction, and each
// transaction stays on its assigned caller. NOTLS permits an old read snapshot
// alongside a new one on that same caller; it adds no concurrency to this panel.
class Lmdb {
  MDB_env* env_{};
  MDB_dbi db_{};
public:
  class Read {
    MDB_txn* txn_{};
    MDB_dbi db_{};
    Read(MDB_txn* txn, MDB_dbi db) : txn_(txn), db_(db) {}
    friend class Lmdb;
  public:
    Read(Read&& source) noexcept : txn_(std::exchange(source.txn_, nullptr)), db_(source.db_) {}
    Read& operator=(Read&&) = delete;
    Read(const Read&) = delete;
    ~Read() { if (txn_) mdb_txn_abort(txn_); }
    // Visible direct mdb_get path, not a virtual Store or outlined fake seam.
    std::optional<Record> get(Key key) const {
      auto encoded = lmdb_key(key);
      MDB_val id{encoded.size(), encoded.data()}, value{};
      const auto status = mdb_get(txn_, db_, &id, &value);
      if (status == MDB_NOTFOUND) return std::nullopt;
      lmdb_check(status); return lmdb_record(value);
    }
    // Borrow the actual mapped value. No staging/copy to satisfy a native body.
    // The calling body must permit alignment 1 and no readable overrun/padding.
    std::optional<orbital::Bytes> part(Address address) const {
      auto encoded=lmdb_address(address); MDB_val id{encoded.size(),encoded.data()},value{};
      const auto status=mdb_get(txn_,db_,&id,&value);
      if (status==MDB_NOTFOUND) return std::nullopt;
      lmdb_check(status);
      return orbital::Bytes{static_cast<const std::byte*>(value.mv_data),value.mv_size};
    }
    std::size_t scan(Key first, std::span<Row>) const;
    std::uint64_t version() const noexcept { return mdb_txn_id(txn_); }
  };
  class Write {
    MDB_txn* txn_{};
    MDB_dbi db_{};
    Write(MDB_txn* txn, MDB_dbi db) : txn_(txn), db_(db) {}
    friend class Lmdb;
  public:
    Write(Write&& source) noexcept : txn_(std::exchange(source.txn_, nullptr)), db_(source.db_) {}
    Write& operator=(Write&&) = delete;
    Write(const Write&) = delete;
    ~Write() { if (txn_) mdb_txn_abort(txn_); }
    std::optional<Record> get(Key key) const {
      auto encoded = lmdb_key(key); MDB_val id{encoded.size(), encoded.data()}, value{};
      const auto status = mdb_get(txn_, db_, &id, &value);
      if (status == MDB_NOTFOUND) return std::nullopt;
      lmdb_check(status); return lmdb_record(value);
    }
    void put(Key, Record, bool insert = false);
    void put_part(Address, orbital::Bytes);
    orbital::MutableBytes reserve_part(Address, std::size_t bytes);
    void publish(); // End-to-end interval includes the actual transaction commit.
  };
  struct Footprint { std::uint64_t map_bytes{}, file_used_bytes{}, file_bytes{}, data_pages{}, tree_depth{}; };
  Lmdb(const std::filesystem::path&, std::size_t byte_limit);
  ~Lmdb();
  Lmdb(const Lmdb&) = delete;
  void load(std::span<const Row>);
  Read read() const;
  Write edit();
  Footprint footprint() const;
};
} // namespace orbital_versioned_performance
