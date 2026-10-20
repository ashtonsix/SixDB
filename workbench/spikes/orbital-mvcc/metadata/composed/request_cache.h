#pragma once
#include <array>
#include <memory>
#include <type_traits>
#include <stdexcept>
#include <cstdint>
#include <bit>
#include <utility>

namespace orbital_versioned_performance {
// Bounded by offered work, never by catalog size. Unused slots have no live
// State and are not zeroed. Scalar callers instantiate one slot; batch callers
// instantiate64. One multiplicative direct-map probe is common fixture work;
// a collision legitimately rebinds the actual opaque identifier.
template<class State,std::size_t Capacity>
class RequestCache {
  static_assert(std::is_trivially_destructible_v<State>);
  static_assert(Capacity<=64 && std::has_single_bit(Capacity));
  union Slot {State state;Slot() {} ~Slot() {}};
  std::array<Slot,Capacity> slots_;
  std::array<std::size_t,Capacity> identities_;
  std::uint64_t valid_{};
public:
  RequestCache() = default;
  RequestCache(const RequestCache&) = delete;
  RequestCache(RequestCache&& other) noexcept :valid_(std::exchange(other.valid_,0)) {
    for (std::size_t i=0;i<Capacity;++i) if (valid_ & (std::uint64_t{1}<<i)) {
      identities_[i]=other.identities_[i];std::construct_at(&slots_[i].state,other.slots_[i].state);
    }
  }
  template<class Resolve> const State& get(std::size_t identity,Resolve&& resolve,std::uint64_t& bindings) {
    const auto slot=[&] {
      if constexpr (Capacity==1) return std::size_t{0};
      else return std::size_t((std::uint64_t(identity)*0x9e3779b97f4a7c15ULL)>>(64-std::countr_zero(Capacity)));
    }();
    const auto bit=std::uint64_t{1}<<slot;
    if ((valid_&bit) && identities_[slot]==identity) return slots_[slot].state;
    auto state=resolve();
    std::construct_at(&slots_[slot].state,state);identities_[slot]=identity;valid_|=bit;++bindings;
    return slots_[slot].state;
  }
};
} // namespace orbital_versioned_performance
