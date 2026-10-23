#pragma once
#include "shape_read.h"

// Private, fixed-law factory after actual Shape/capture or mapping admission.
// Kept outside the public adapter header; it grants no ownership or authority.
namespace orbital_mvcc::metadata_consumer::shape_read::detail {
struct CursorAccess {
  static compact_law::Cursor admitted(const std::byte* pointer) noexcept {
    return compact_law::Cursor{reinterpret_cast<const std::byte*>(pointer)};
  }
};
inline bool mapped_extent(std::uint64_t count, orbital::Bytes body) noexcept {
  return count <= SIZE_MAX / compact_law::v::tuple_bytes &&
      body.size() == count * compact_law::v::tuple_bytes && compact_law::detail::valid_span(body);
}
} // namespace orbital_mvcc::metadata_consumer::shape_read::detail
