// Size-only TU: including the real implementation makes its private records
// observable without mirroring them. Compile with function sections and discard
// unused sections at link time; main never constructs Schema or calls a kernel.
#include "write_batch.cpp"
#include <cstdio>
#include <type_traits>

namespace wb = orbital_mvcc::metadata_consumer::write_batch;
namespace v = orbital_bench::versioned;
namespace tp = ikea::tuplepack;
namespace sp = ikea::seriespack;
using TupleWriteResult = decltype(tp::bind_writer(std::declval<const tp::writer<8>&>(),
                                                std::declval<const tp::view&>()));
using TupleWrite = TupleWriteResult::value_type;
using TupleNativeWrite = decltype(tp::native_writer{std::declval<TupleWrite>()});
using Series64 = decltype(wb::Columns::next);
using Series20 = decltype(wb::Columns::value);
using Series32 = decltype(wb::Columns::tag);
template<class View> using SeriesWriteResult = decltype(sp::bind_mutation(std::declval<const View&>()));
template<class View> using SeriesWrite = typename SeriesWriteResult<View>::value_type;

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
  packet<v::Schema>("Schema");
  packet<v::Shape>("Shape");
  packet<v::WritePart>("WritePart");
  packet<v::Update>("Update");
  packet<v::KernelCosts>("KernelCosts");
  packet<wb::ProbeCosts>("ProbeCosts");
  packet<v::ReadData>("ReadData");
  packet<orbital::Result<v::ReadData>>("ResultReadData");
  packet<wb::Rows>("PrivateRows");
  packet<wb::Columns>("PrivateColumns");
  packet<wb::Range>("PrivateRange");
  packet<std::array<wb::Range, 11>>("ProtectedRanges11");
  packet<std::array<v::ReadPart, 4>>("ReadParts4");
  packet<tp::view>("TupleView");
  packet<tp::writer<8>>("TupleWriterPlan8");
  packet<TupleWrite>("TupleMutationOperation8x1");
  packet<TupleWriteResult>("ResultTupleMutationOperation8x1");
  packet<TupleNativeWrite>("TupleNativeWriter8x1");
  packet<Series64>("SeriesMutableView64");
  packet<Series20>("SeriesMutableView20");
  packet<Series32>("SeriesMutableView32");
  packet<SeriesWrite<Series64>>("SeriesPreparedMutation64");
  packet<SeriesWrite<Series20>>("SeriesPreparedMutation20");
  packet<SeriesWrite<Series32>>("SeriesPreparedMutation32");
  packet<SeriesWriteResult<Series64>>("ResultSeriesPreparedMutation64");
  packet<SeriesWriteResult<Series20>>("ResultSeriesPreparedMutation20");
  packet<SeriesWriteResult<Series32>>("ResultSeriesPreparedMutation32");
  packet<std::array<ikea::owner_write, 4>>("OwnerWriteJournal4");
  packet<ikea::source_write_journal>("SourceWriteJournal");
  packet<orbital::Result<void>>("ResultVoid", false);
  std::printf("]\n");
}
