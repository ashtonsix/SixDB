// Frozen 7f73c81 headers: sizes are storage facts, not ABI classifications.
#include "orbital/src/object/internal.h"
#include <cstdio>
#include <type_traits>

template<class T> void packet(const char* name) {
  std::printf("{\"type\":\"%s\",\"size\":%zu,\"alignment\":%zu,"
      "\"trivial_copy\":%s,\"trivial_move\":%s,\"trivial_destructor\":%s,"
      "\"standard_layout\":%s}\n", name, sizeof(T), alignof(T),
      std::is_trivially_copyable_v<T> ? "true" : "false",
      std::is_trivially_move_constructible_v<T> ? "true" : "false",
      std::is_trivially_destructible_v<T> ? "true" : "false",
      std::is_standard_layout_v<T> ? "true" : "false");
}

int main() {
  using namespace orbital;
  using namespace orbital::object;
  using namespace orbital::object::detail;
#define PACKET(T) packet<T>(#T)
  PACKET(Description); PACKET(RepresentationSpec); PACKET(PartSource); PACKET(PartPlan);
  PACKET(View); PACKET(Representation); PACKET(Selection); PACKET(RetainedView);
  PACKET(RetainedSelection); PACKET(BorrowedContext); PACKET(ReadContext);
  PACKET(WriteView); PACKET(Builder); PACKET(Draft); PACKET(Prepared); PACKET(Store);
  PACKET(Disposition); PACKET(object::Statistics);
  PACKET(Result<View>); PACKET(Result<Selection>); PACKET(Result<Representation>);
  PACKET(Result<ReadContext>); PACKET(Result<WriteView>); PACKET(Result<Builder>);
  PACKET(Result<Draft>); PACKET(Result<Prepared>); PACKET(Result<Store>);
  PACKET(Result<Bytes>); PACKET(Result<MutableBytes>); PACKET(Result<void>);
  PACKET(Base); PACKET(Domain); PACKET(Info); PACKET(Part); PACKET(Rep); PACKET(Leaf);
  PACKET(Access); PACKET(Build); PACKET(Version); PACKET(Entry); PACKET(Catalog);
  PACKET(Snapshot); PACKET(Change); PACKET(Edit); PACKET(Pending); PACKET(Acceptance);
  PACKET(Ref<Version>); PACKET(Ref<Rep>); PACKET(Ref<Part>);
  PACKET(std::shared_ptr<const void>); PACKET(std::pmr::vector<PartId>);
  PACKET(std::pmr::vector<bool>);
#undef PACKET
}
