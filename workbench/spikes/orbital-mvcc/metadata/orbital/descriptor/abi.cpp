#include "descriptor.h"
#include <utility>

namespace cd = compact_descriptor;

cd::Result<cd::View> descriptor_part(cd::Selection selection, std::size_t index) noexcept {
  return selection.part(index);
}

cd::Bytes descriptor_body(cd::View value) noexcept { return value.body(); }

cd::Bytes descriptor_interpretation(cd::View value) noexcept { return value.interpretation(); }

const cd::FrozenDescription& descriptor_description(cd::View value) noexcept { return value.description(); }

cd::Result<cd::Selection> descriptor_bind(cd::Descriptor* value) noexcept {
  if (!value) return std::unexpected(cd::Error::absent);
  return cd::Selection{value};
}

#if defined(COPY_FROZEN_DESCRIPTION)
cd::Bytes accidental_description_copy(cd::View value) noexcept {
  auto description = value.description();
  return description.interpretation();
}
#endif

#if defined(MOVE_FROZEN_DESCRIPTION)
cd::Bytes accidental_description_move(cd::FrozenDescription& value) noexcept {
  auto description = std::move(value);
  return description.interpretation();
}
#endif
