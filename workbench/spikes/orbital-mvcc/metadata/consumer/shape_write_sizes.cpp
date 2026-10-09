// Repository-qualified includes let the copied probe bind only archived source
// roots. Private Shape records are authoritative definitions, not mirrors.
#include "workbench/spikes/orbital-mvcc/metadata/consumer/shape_write.h"
#include "workbench/spikes/orbital-mvcc/metadata/orbital/shape/shape_private.h"
#include <cstdio>
#include <type_traits>

namespace sw = orbital_mvcc::metadata_consumer::shape_write;
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
  packet<sw::TupleProof>("TupleProof");
  packet<sw::SeriesProof>("SeriesProof");
  packet<orbital::Result<sw::SeriesProof>>("ResultSeriesProof");
  packet<sw::TupleLaw>("TupleLaw");
  packet<std::optional<sw::TupleLaw>>("OptionalTupleLaw");
  packet<std::optional<sw::SeriesProof>>("OptionalSeriesProof");
  packet<v::tp::layout>("TupleLayout");
  packet<v::tp::writer<8>>("TupleWriter");
  packet<v::tp::view>("TupleView");
  packet<v::tp::mutation_operation<8>>("TupleMutation");
  packet<v::sp::view<v::Column<64>, std::uint8_t>>("SeriesView64");
  packet<decltype(v::sp::bind_mutation(std::declval<const v::sp::view<v::Column<64>, std::uint8_t>&>()))::value_type>("SeriesMutation64");
  packet<std::array<std::size_t, v::write_batch_limit>>("BatchRows");
  packet<std::array<ikea::owner_write, 4>>("EffectJournal");
  packet<cs::Selection>("Selection");
  packet<cs::Shape>("Shape");
  packet<cs::Geometry>("Geometry");
  packet<law::ReadLaw>("ReadLaw");
  packet<v::Schema>("TransientSchema");
  packet<orbital_bench::Record>("Record");
  packet<v::Update>("Update");
  packet<v::WritePart>("WritePart");
  packet<v::KernelCosts>("KernelCosts");
  packet<sw::ProbeCosts>("ProbeCosts");
  packet<orbital::Result<void>>("ResultVoid", false);
  std::printf("]\n");
}
