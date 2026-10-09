#include "shape.h"

namespace cs = compact_shape;
orbital::Result<cs::View> shape_part(cs::Selection selection, std::size_t index) noexcept {
  return selection.part(index);
}
cs::Control shape_control(cs::Selection selection) noexcept { return selection.control(); }
const std::byte* shape_address(cs::View view) noexcept { return view.address(); }
