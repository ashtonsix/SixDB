#pragma once
#include <lmdb.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace index_spike::lmdb_peer {
using Key = std::array<std::byte,4>;
using Descriptor64 = std::array<std::byte,64>;
struct Packed128 {
  Descriptor64 descriptor;
  std::array<std::byte,64> material;
};
static_assert(sizeof(Packed128)==128 && offsetof(Packed128,material)==64);

// Setup owns only the native environment. End every transaction/cursor before
// closing it. The caller directly uses mdb_txn_begin/commit/abort/reset/renew;
// no request, batch, clock, consumer or snapshot policy is supplied here.
struct Environment {
  MDB_env* handle{};
  MDB_dbi db{};
  Environment() = default;
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;
  ~Environment();
  void close() noexcept;
};
struct Info {
  const char* version{};
  unsigned flags{};
  MDB_envinfo environment{};
};

// The directory already exists. Flags are explicit: NOSYNC/NOMETASYNC is a
// CPU-only control, not durability parity. NOTLS permits the fixture's multiple
// read transactions on one thread; it does not change snapshot semantics.
[[nodiscard]] int open(Environment&,const char* path,std::size_t map_bytes,
    unsigned flags) noexcept;
[[nodiscard]] int inspect(const Environment&,Info&) noexcept;

inline Key key(std::uint32_t prefix) noexcept {
  return {std::byte(prefix>>24),std::byte(prefix>>16),
      std::byte(prefix>>8),std::byte(prefix)};
}
inline MDB_val key_value(Key& encoded) noexcept {
  return {encoded.size(),encoded.data()};
}

// Native statuses and borrowed bytes stay visible. Consume a successful value
// before transaction end/reset or any subsequent write through that transaction.
[[nodiscard]] inline int get(MDB_txn* txn,MDB_dbi db,std::uint32_t prefix,
    MDB_val& value) noexcept {
  auto encoded=key(prefix);
  auto k=key_value(encoded);
  return mdb_get(txn,db,&k,&value);
}
[[nodiscard]] inline int put(MDB_txn* txn,MDB_dbi db,std::uint32_t prefix,
    const Descriptor64& descriptor,unsigned flags=0) noexcept {
  if (flags&MDB_RESERVE) return EINVAL; // reserve() exposes the writable bytes.
  auto encoded=key(prefix);
  auto k=key_value(encoded);
  MDB_val value{descriptor.size(),const_cast<std::byte*>(descriptor.data())};
  return mdb_put(txn,db,&k,&value,flags);
}
[[nodiscard]] inline int put(MDB_txn* txn,MDB_dbi db,std::uint32_t prefix,
    const Packed128& packed,unsigned flags=0) noexcept {
  if (flags&MDB_RESERVE) return EINVAL;
  auto encoded=key(prefix);
  auto k=key_value(encoded);
  MDB_val value{sizeof(packed),const_cast<Packed128*>(&packed)};
  return mdb_put(txn,db,&k,&value,flags);
}

// With MDB_RESERVE the caller must initialize all requested bytes before the
// next write or transaction end. Keep that construction inside its timed write.
[[nodiscard]] inline int reserve(MDB_txn* txn,MDB_dbi db,std::uint32_t prefix,
    std::size_t bytes,MDB_val& value) noexcept {
  auto encoded=key(prefix);
  auto k=key_value(encoded);
  value={bytes,nullptr};
  return mdb_put(txn,db,&k,&value,MDB_RESERVE);
}

// These views require a successful get of the stated fixture shape. The caller
// checks complete bytes/size in its untimed oracle. Canonical descriptors do not
// copy Engine's process-local address bits; the caller defines their semantics.
inline std::span<const std::byte,64> descriptor(const MDB_val& value) noexcept {
  return std::span<const std::byte,64>(
      static_cast<const std::byte*>(value.mv_data),64);
}
inline std::span<const std::byte,64> material(const MDB_val& packed) noexcept {
  return std::span<const std::byte,64>(
      static_cast<const std::byte*>(packed.mv_data)+64,64);
}
// ordinal is 0..7; memcpy also handles native LMDB values without u64 alignment.
inline std::uint64_t material_word(const MDB_val& packed,unsigned ordinal) noexcept {
  std::uint64_t word;
  std::memcpy(&word,material(packed).data()+ordinal*8,8);
  return word;
}
} // namespace index_spike::lmdb_peer
