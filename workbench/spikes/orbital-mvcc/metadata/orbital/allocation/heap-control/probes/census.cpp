// Requested ownership accounting from an actual native Group and its capacities.
#include "native_fixture.h"
#include "internal.h"
#include <cstdio>
#include <cstdlib>

namespace oa = orbital::allocation;
namespace d = oa::detail;
void measure(unsigned bytes) {
  compact_shape::probe::NativeFixture fixture;
  if (!fixture.open()) std::abort();
  const auto empty = fixture.statistics().metadata_bytes;
  auto allocated = oa::allocate(*fixture.pool, {.bytes = bytes, .alignment = 1});
  if (!allocated) std::abort();
  auto& group = *static_cast<d::Group*>(d::route(*allocated->storage)->context);
  const auto control = group.geometry->control_bytes();
  const auto live = group.live.capacity() * sizeof(std::uint64_t);
  const auto trim = group.trimming.capacity() * sizeof(d::TrimSpan);
  const auto discard = (group.discarded.capacity() + 7) / 8;
  const auto sum = sizeof(group) + sizeof(d::FiniteGroup) + control + live + trim + discard;
  const auto total = fixture.statistics().metadata_bytes;
  const auto reserved = fixture.statistics().reserved_bytes;
  if (group.metadata_charge != sum || total - empty != sum) std::abort();
  if (d::page_size() == 4096 &&
      (sum != (bytes <= 64 ? 5256U : 6792U) ||
       trim != (bytes <= 64 ? 1272U : 2192U) ||
       reserved != (bytes <= 64 ? 2605056U : 4489216U))) std::abort();
  std::printf("{\"storage\":%zu,\"palette\":\"%s\",\"Group\":%zu,\"FiniteGroup\":%zu,"
      "\"control_heap_requested\":%zu,\"live_capacity\":%zu,\"trim_capacity\":%zu,"
      "\"discard_capacity\":%zu,\"group_metadata_charge\":%llu,\"empty_pool\":%llu,"
      "\"populated_pool\":%llu,\"group_vector_capacity_bytes_excluded_from_public_pool\":%zu,"
      "\"native_page_bytes\":%zu,\"payload_reserved\":%llu}\n",
      sizeof(oa::Storage), bytes <= 64 ? "A" : "B", sizeof(group), sizeof(d::FiniteGroup),
      control, live, trim, discard, static_cast<unsigned long long>(sum),
      static_cast<unsigned long long>(empty), static_cast<unsigned long long>(total),
      fixture.pool->internal->groups.capacity() * sizeof(void*), d::page_size(),
      static_cast<unsigned long long>(reserved));
  oa::release(*allocated);
  if (!fixture.close()) std::abort();
}

int main() { measure(64); measure(1536); }
