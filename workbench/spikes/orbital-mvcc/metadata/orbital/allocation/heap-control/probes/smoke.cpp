#include "native_fixture.h"
#include "internal.h"
#include <cstdio>
#include <cstdlib>
namespace oa = orbital::allocation;
namespace d = oa::detail;
void measure(unsigned bytes) {
  compact_shape::probe::NativeFixture fixture;
  if (!fixture.open()) std::abort();
  auto allocated = oa::allocate(*fixture.pool,
      {.bytes = bytes, .alignment = 1, .readable_padding = 16, .initialization = oa::Initialization::zero});
  if (!allocated) std::abort();
  for (auto byte : oa::bytes(*allocated)) if (byte != std::byte{}) std::abort();
  auto held = oa::hold(*allocated, {0, bytes + 16});
  if (!held) std::abort();
  auto& group = *static_cast<d::Group*>(d::route(*allocated->storage)->context);
  const auto counts = fixture.statistics();
  std::printf("{\"palette\":\"%s\",\"native_page\":%zu,\"Group\":%zu,\"FiniteGroup\":%zu,\"control_heap_requested\":%zu,\"metadata_charge\":%llu,\"public_metadata\":%llu,\"payload_reserved\":%llu}\n",
      bytes <= 64 ? "A" : "B", group.geometry->native_page(), sizeof(group), sizeof(d::FiniteGroup),
      group.geometry->control_bytes(), static_cast<unsigned long long>(group.metadata_charge),
      static_cast<unsigned long long>(counts.metadata_bytes), static_cast<unsigned long long>(counts.reserved_bytes));
  oa::release(*allocated);
  for (std::size_t i = 0; i < bytes + 16; ++i) if (held->address[i] != std::byte{}) std::abort();
  oa::release(*held);
  if (!fixture.close()) std::abort();
}
int main() { measure(64); measure(1536); }
