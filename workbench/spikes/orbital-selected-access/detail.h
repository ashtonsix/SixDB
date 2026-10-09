#pragma once
#include "access.h"
#include <orbital/allocation.h>
#include <vector>

namespace selected::detail {
struct Part {
  Id id{};
  orbital::allocation::Allocation bytes;
  bool native{};
  Bytes external, interpretation;
  std::size_t external_alignment{1}, external_padding{};
  Bytes body() const noexcept { return bytes ? Bytes{bytes.address, bytes.size} : external; }
  std::size_t padding() const noexcept { return bytes ? bytes.readable_padding : external_padding; }
};

struct Builder {
  Key key;
  Id realization{}, format{};
  std::vector<std::byte> interpretation;
  std::vector<Part> parts;
  unsigned writers{};
  std::shared_ptr<const void> external_owner;
};

struct Object { Key key; std::vector<std::byte> logical; Rep rep; };
struct StoreIdentity {};
struct State { std::shared_ptr<const StoreIdentity> owner; Id epoch{}; std::vector<Object> objects; };
struct Store { Store(); std::shared_ptr<const StoreIdentity> identity; std::shared_ptr<const State> head; std::vector<Rep> alternatives; };
struct Draft { std::shared_ptr<const State> basis; std::vector<Object> changes; bool sealed{}; };
struct Prepared { std::shared_ptr<const State> basis, next; bool installed{}; };
const Object* find(const State&, Id) noexcept;
const Part* find(const Representation&, Id) noexcept;
Result<Bytes> qualify(const Representation&, Bytes, std::size_t padding, Requirements);

struct Buffer { orbital::allocation::Allocation bytes; };
struct Slot {
  std::uint64_t generation{};
  std::size_t outstanding{};
  Retained selection;
  std::shared_ptr<Buffer> buffer;
};

struct Transfer {
  std::weak_ptr<Slot> slot;
  std::uint64_t generation{};
  Retained selection;
  const Part* source{};
  std::shared_ptr<Buffer> target;
  bool completed{}, success{}, settled{}, retired{};
};
} // namespace selected::detail

namespace selected {
struct Representation {
  explicit Representation(std::shared_ptr<const detail::Builder> value) : data(std::move(value)) {}
  std::shared_ptr<const detail::Builder> data;
};
} // namespace selected
