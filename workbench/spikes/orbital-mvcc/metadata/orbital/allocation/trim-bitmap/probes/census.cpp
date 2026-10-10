// Actual requested accounting of the selected runtime and bitmap projection.
#include "fixture.h"
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
#if TRIM_BITMAP
  const std::size_t trim = 0, discard = 0;
  const auto membership = group.geometry->trim_membership().size_bytes();
  const auto success = group.geometry->trim_success().size_bytes();
#else
  const auto trim = group.trimming.capacity() * sizeof(d::TrimSpan);
  const auto discard = (group.discarded.capacity() + 7) / 8;
  const std::size_t membership = 0, success = 0;
#endif
  const auto sum = sizeof(group) + sizeof(d::FiniteGroup) + control + live + trim + discard;
  const auto total = fixture.statistics().metadata_bytes;
  if (group.metadata_charge != sum || total - empty != sum) std::abort();
  std::printf("{\"palette\":\"%s\",\"Group\":%zu,\"FiniteGroup\":%zu,"
      "\"control_mapping\":%zu,\"live_capacity\":%zu,\"trim_capacity\":%zu,\"discard_capacity\":%zu,"
      "\"mapped_membership_bytes\":%zu,\"mapped_success_bytes\":%zu,\"payload_pages\":%zu,"
      "\"group_metadata_charge\":%zu,\"empty_pool\":%llu,\"populated_pool\":%llu,"
      "\"excluded_group_vector_capacity_bytes\":%zu,\"native_page_bytes\":%zu}\n",
      bytes <= 64 ? "A" : "B", sizeof(group), sizeof(d::FiniteGroup), control, live, trim, discard,
      membership, success, group.geometry->payload_pages(), sum,
      static_cast<unsigned long long>(empty), static_cast<unsigned long long>(total),
      fixture.pool->internal->groups.capacity() * sizeof(void*), d::page_size());
}
int main() { measure(64); measure(1536); }
