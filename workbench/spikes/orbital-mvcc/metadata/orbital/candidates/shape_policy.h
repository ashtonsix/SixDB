#pragma once

#include "store.h"
#include "../shape/material.h"

namespace compact_history {

using ShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical>;
using ShapeStore32 = Store<Slab<Version32>, compact_retention::DistinctRetention, ShapePhysical>;
using ShapeStore8 = Store<Chunks8, compact_retention::DistinctRetention, ShapePhysical>;
using CompactShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using CompactShapeStore32 = Store<Slab<Version32>, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using CompactShapeStore8 = Store<Chunks8, compact_retention::DistinctRetention, ShapePhysical, CompactObjects>;
using InlineShapeStore16 = Store<Slab<Version16>, compact_retention::DistinctRetention, ShapePhysical, TipObjects>;

} // namespace compact_history
