#pragma once
#include <orbital/types.h>
namespace orbital {
// Catalog-issued identity snapshot; physical ownership alone grants no labels.
struct ObjectBinding { ObjectId object{}; std::uint64_t version{}; };
}
