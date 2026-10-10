#pragma once
#include <orbital/object.h>
#include "../orbital/candidates/shape_policy.h"

namespace orbital_mvcc::composed {
using Catalog = orbital::ObjectStore<compact_history::ShapePhysical>;
using InlineReadFusion = orbital::object_detail::Access<compact_history::ShapePhysical>;
}
extern template class orbital::ObjectStore<compact_history::ShapePhysical>;
