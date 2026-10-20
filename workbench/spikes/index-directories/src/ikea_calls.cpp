#include "native_abi.h"
#include <ikea/boundlist.h>
#include <ikea/boundlist/author/search.h>

extern "C" unsigned idx_ikea_lower_u32(const std::uint32_t* p,unsigned n,
    std::uint32_t q) noexcept {
#if INDEX_DIRECTORY_LEAF_BOUND
  // Actual node admission proves a maximum count, not count==capacity and not
  // permission to read any byte beyond the source's assigned n endpoints.
  __builtin_assume(n<=INDEX_DIRECTORY_LEAF_BOUND);
#endif
#if INDEX_DIRECTORY_SCAN
  const ikea::boundlist::author::scan_source<std::uint32_t> source{p,n};
#else
  const ikea::boundlist::direct_source<std::uint32_t> source{p,n};
#endif
  return ikea::boundlist::lower_bound(source,q);
}
extern "C" unsigned idx_ikea_upper_u32(const std::uint32_t* p,unsigned n,
    std::uint32_t q) noexcept {
#if INDEX_DIRECTORY_INTERNAL_BOUND
  __builtin_assume(n<=INDEX_DIRECTORY_INTERNAL_BOUND);
#endif
#if INDEX_DIRECTORY_SCAN
  const ikea::boundlist::author::scan_source<std::uint32_t> source{p,n};
#else
  const ikea::boundlist::direct_source<std::uint32_t> source{p,n};
#endif
  return ikea::boundlist::upper_bound(source,q);
}
