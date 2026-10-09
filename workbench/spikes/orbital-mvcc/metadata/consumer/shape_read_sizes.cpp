// Repository-qualified includes let the copied probe bind only archived source
// roots. Private Shape records are authoritative definitions, not mirrors.
#include "workbench/spikes/orbital-mvcc/metadata/consumer/shape_read.h"
#include "workbench/spikes/orbital-mvcc/metadata/orbital/shape/shape_private.h"
#include <cstdio>
#include <type_traits>

namespace sr = orbital_mvcc::metadata_consumer::shape_read;
namespace law = orbital_mvcc::metadata_consumer::compact_law;
namespace cs = compact_shape;
namespace v = orbital_bench::versioned;

template<class T> void packet(const char* name, bool comma = true) {
  std::printf("{\"type\":\"%s\",\"bytes\":%zu,\"alignment\":%zu,\"trivially_copyable\":%s,"
      "\"trivial_destructor\":%s,\"trivial_copy\":%s,\"trivial_move\":%s,\"standard_layout\":%s}%s",
      name, sizeof(T), alignof(T), std::is_trivially_copyable_v<T> ? "true" : "false",
      std::is_trivially_destructible_v<T> ? "true" : "false",
      std::is_trivially_copy_constructible_v<T> ? "true" : "false",
      std::is_trivially_move_constructible_v<T> ? "true" : "false",
      std::is_standard_layout_v<T> ? "true" : "false", comma ? "," : "");
}

int main() {
  std::printf("[");
  packet<sr::TupleProof>("TupleProof");
  packet<orbital::Result<sr::TupleProof>>("ResultTupleProof");
  packet<cs::Selection>("Selection");
  packet<cs::View>("View");
  packet<orbital::Result<cs::View>>("ResultView");
  packet<cs::Control>("ShapeControl");
  packet<law::ReadLaw>("ReadLaw");
  packet<law::Control>("CompactReadControl");
  packet<law::Cursor>("Cursor");
  packet<law::Part>("Part");
  packet<cs::Shape>("Shape");
  packet<cs::Realization>("Realization");
  packet<cs::Geometry>("Geometry");
  packet<cs::SharedBody>("SharedBody");
  packet<cs::AllocatedBody>("AllocatedBody");
  packet<std::shared_ptr<const void>>("SharedBodyOwner");
  packet<orbital::allocation::Storage*>("AllocationBodyOwnerPointer");
  packet<cs::Specification>("Specification");
  packet<cs::BodySource>("BodySource");
  packet<v::Schema>("TransientSchema");
  packet<orbital_bench::Outcome>("Outcome");
  packet<orbital::Result<void>>("ResultVoid", false);
  std::printf("]\n");
}
