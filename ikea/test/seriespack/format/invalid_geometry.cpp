// Compile-fail witness. Twelve bits support either real geometry, so accepting
// this type would specifically admit an unknown tag into the public byte law.
#include <ikea/seriespack/layout.h>
using invalid = ikea::seriespack::layout<12, static_cast<ikea::seriespack::geometry>(9)>;
constexpr auto force_layout_instantiation = invalid::tile_bytes;
