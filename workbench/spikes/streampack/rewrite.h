#pragma once
#include "material.h"

namespace streampack_material {
struct Rewrite {
  Draft draft;
  std::vector<std::uint32_t> remap, sources; // explicit caller-owned publication outputs
};
Result<Rewrite> hard(oa::Pool&, Selection, bool traversal, bool dedup, bool shrink_blocks = false);
} // namespace streampack_material
