#pragma once
#include <engine/directory.h>
#include <ikea/hashtable.h>

namespace engine::directory {
namespace detail {
struct HashStorage {
  struct context { loom::Worker* worker; };
  struct retention {};
  using trim_result=std::expected<void,std::errc>;
  struct buffer {
    orbital::allocation::Allocation allocation;
    std::uint8_t* data{};
    std::size_t size{};
    buffer()=default;
    buffer(std::size_t,context*,bool zero=true,std::size_t reserve=0);
    buffer(buffer&&) noexcept;
    buffer& operator=(buffer&&) noexcept;
    buffer(const buffer&)=delete;
    std::size_t capacity() const noexcept { return size; }
    std::size_t reserved_bytes() const noexcept;
    ikea::hashtable::storage_usage usage() const noexcept;
    void grow(std::size_t,std::size_t=0,bool=true);
    void retain(retention&) const noexcept {}
    trim_result relinquish_tail(std::size_t) noexcept;
    trim_result relinquish_surplus() noexcept { return {}; }
  };
};
using DirectoryTable=ikea::hashtable::table<4,0,64,4,ikea::hashtable::search_kernel::generic,HashStorage>;
}
/// Native frozen Ikea owner borrowed under the issuing Scope. Keys are the
/// original low four prefix bits; the selected group supplies the high bits.
class HashGroup {
  friend class HashRead;
  const detail::DirectoryTable* table_{};
  explicit HashGroup(const detail::DirectoryTable* t):table_(t) {}
public:
  HashGroup()=default;
  template<ikea::hashtable::lookup_hint Hint=ikea::hashtable::lookup_hint::predictable>
  [[gnu::always_inline]] auto get(ikea::hashtable::word suffix) const {
    return table_?table_->template get<Hint>(suffix):typename detail::DirectoryTable::lookup_value{};
  }
  void prefetch(ikea::hashtable::word suffix) const { if (table_) table_->prefetch(table_->prepare(suffix)); }
};
/// A caller-resident read context: cache entries (including absence) belong only
/// to this Scope. Core admission occurs once; group resolution uses that Scope.
class HashRead {
  friend class HashLayer;
  const material::Store::Scope* scope_{};
  std::uint64_t space_{};
  unsigned layer_{},next_{};
  bool empty_{true};
  struct Cached { std::uint32_t group{UINT32_MAX}; const detail::DirectoryTable* table{}; };
  std::array<Cached,8> cache_;
  orbital::Result<HashGroup> bind(std::uint32_t) const noexcept;
public:
  HashRead()=default;
  HashRead(const HashRead&)=delete;
  HashRead& operator=(const HashRead&)=delete;
  orbital::Result<HashGroup> group(std::uint32_t index) noexcept {
    if (!scope_ || index>(UINT32_MAX>>4) ||
        (layer_<4 && index>=(UINT32_C(1)<<(8*layer_-4))))
      return std::unexpected(orbital::Error::invalid_argument);
    for (auto c:cache_) if (c.group==index) return HashGroup(c.table);
    auto value=bind(index); if (!value) return std::unexpected(value.error());
    cache_[next_++&7]={index,value->table_}; return *value;
  }
};
/// Shared-prefix layer recipe. Directory metadata is kept in named groups of
/// sixteen physical prefixes, using actual inline-V64 Ikea bitmap tables. A
/// locator-only change copies one group and publishes no replacement header.
/// Initial construction and existing-member updates are implemented; group
/// birth/death and ordered prefix/mask iteration remain integration work.
class HashLayer {
  friend class Directory;
  struct State;
  std::unique_ptr<State> state_;
  explicit HashLayer(std::unique_ptr<State>);
  static orbital::Result<std::unique_ptr<HashLayer>> open(material::Store&,loom::Worker&,
      std::pmr::memory_resource&,std::uint64_t,KeyShape,unsigned);
public:
  ~HashLayer();
  HashLayer(const HashLayer&)=delete;
  HashLayer& operator=(const HashLayer&)=delete;
  orbital::ObjectId root_id() const noexcept;
  /// Fresh layer only. Caller joins ALL returned finished changes in one packet.
  orbital::Result<std::pmr::vector<material::Store::Change>> initialize(
      const material::Store::Scope&,std::span<const Entry>);
  orbital::Result<void> read(const material::Store::Scope&,HashRead&) const noexcept;
  orbital::Result<std::pmr::vector<material::Store::Change>> replace(
      const material::Store::Scope&,std::span<const LocatorChange>);
};
}
