#pragma once

#include "finite.h"

namespace orbital::allocation::detail {

// Test-only larger-page geometry. Group page state and charges use this granule;
// the host still owns physical faults, protection and discard. This does not
// emulate native-page performance. Zero selects the host page as usual.
struct FiniteQualification {
  [[nodiscard]] static Result<std::unique_ptr<FiniteGroup>> create(
      FiniteConfig config, std::size_t page_granule) noexcept {
    return FiniteGroup::create_geometry(config, page_granule);
  }
};

} // namespace orbital::allocation::detail
