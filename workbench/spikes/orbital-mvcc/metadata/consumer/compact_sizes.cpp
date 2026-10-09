#include "compact_descriptor_adapter.h"
#include "canonical_descriptor_adapter.h"
#include <cstdio>
#include <type_traits>
namespace c = orbital_mvcc::metadata_consumer;
namespace law = c::compact_law;
namespace fixed = c::canonical_law;
template<class T> void packet(const char* name, bool comma = true) {
  std::printf("{\"type\":\"%s\",\"bytes\":%zu,\"alignment\":%zu,\"trivially_copyable\":%s,"
      "\"trivial_destructor\":%s,\"trivial_move\":%s}%s", name, sizeof(T), alignof(T),
      std::is_trivially_copyable_v<T> ? "true" : "false",
      std::is_trivially_destructible_v<T> ? "true" : "false",
      std::is_trivially_move_constructible_v<T> ? "true" : "false", comma ? "," : "");
}
int main() {
  std::printf("[");
  packet<law::ReadLaw>("ReadLaw"); packet<law::Control>("CompactControl");
  packet<law::Cursor>("CompactCursor"); packet<law::Part>("CompactPart");
  packet<orbital::Result<law::Cursor>>("ResultCompactCursor");
  packet<orbital::Result<law::Part>>("ResultCompactPart");
  packet<fixed::Law>("MaterializedCanonicalTag"); packet<fixed::Control>("CanonicalControl");
  packet<fixed::Cursor>("CanonicalCursor"); packet<fixed::Part>("CanonicalPart");
  packet<orbital::Result<fixed::Cursor>>("ResultCanonicalCursor");
  packet<orbital::Result<fixed::Part>>("ResultCanonicalPart", false);
  std::printf("]\n");
}
