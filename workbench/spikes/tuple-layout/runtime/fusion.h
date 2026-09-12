#pragma once
#include "composition.h"
namespace tuple_composition_probe {
enum class execution { separate, generic, constants, algebraic, normalized };
// select() names controlled fixture endpoints only. Ordinary binding must
// derive applicability from the actual schema/recipe via bind_operation().
operation select(execution, bool reordered, bool partial);
operation bind_operation(const recipe&);
const char* name(execution);
void check_routes();
void register_fusion_benchmarks();
}
