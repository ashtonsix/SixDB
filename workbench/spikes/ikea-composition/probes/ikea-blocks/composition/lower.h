#pragma once
#include "graph.h"

namespace ikea::composition {
struct Program;
// Lower one explicitly linear tile body. Register-carrier reuse or branching
// needs a richer allocator/protocol and is rejected, rather than materialized.
bool lower_linear(const Graph&,Program&,std::string& error);
// The separately compiled inline implementations cover this selected recipe.
// They do not execute an arbitrary runtime graph or consult its stage table.
bool mapped_inline_recipe(const Program&);
}
