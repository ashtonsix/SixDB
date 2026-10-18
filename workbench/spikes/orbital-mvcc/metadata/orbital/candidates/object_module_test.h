#pragma once
#include <orbital/object.h>
#include <orbital/instantiate/object.h>
#include "descriptor_policy.h"
#include "shape_policy.h"

// Reuse consequence histories against the selected production implementation.
// This test-only wrapper adds observation, never catalog or ownership behavior.
namespace orbital::object_detail {
template<class Physical> struct Access {
  static auto inspect(const ObjectStore<Physical>& store) noexcept { return store.statistics(); }
};
}
namespace object_module_test {
using orbital::ObjectId;
using orbital::Error;
using orbital::Result;
using Memory = compact_version_probe::Memory;
using Disposition = orbital::PublicationState;
struct DescriptorPhysical : compact_history::DescriptorPhysical {
  static auto export_part(Selection selected, std::size_t index, orbital::ObjectBinding,
      ExportPolicy) noexcept { return compact_descriptor::retain_part(selected, index); }
};
template<class Physical> class Store : public orbital::ObjectStore<Physical> {
  using Base = orbital::ObjectStore<Physical>;
  Memory* memory_{};
public:
  static constexpr bool exact_retention = true;
  Store() = default;
  explicit Store(Memory& memory) : Base(memory), memory_(&memory) {}
  Store(Store&&) = default;
  Store& operator=(Store&&) = default;
  using CatalogObservation = decltype(orbital::object_detail::Access<Physical>::inspect(
      std::declval<const Base&>()));
  struct Observation : CatalogObservation { std::size_t allocation_calls, requested_bytes; };
  Observation statistics() const noexcept {
    const auto observed = orbital::object_detail::Access<Physical>::inspect(*this);
    return {observed, observed.core_bytes ? memory_->calls : 0,
        observed.core_bytes ? memory_->live : 0};
  }
};
using Selected = Store<DescriptorPhysical>;
using DescriptorOwner = Selected::Owner;
using Change = Selected::Change;
}
