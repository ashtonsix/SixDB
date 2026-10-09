// Actual requested accounting; excludes Arena/thread/libc setup and Object state.
#include "../../shape/native_fixture.h"
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
#if ROUTE_STORAGE == 16
  const auto* route = allocated->storage->domain;
#else
  const auto* route = d::route(*allocated->storage);
#endif
  auto& group = *static_cast<d::Group*>(route->context);
  const auto control = group.geometry->control_bytes();
  const auto live = group.live.capacity() * sizeof(std::uint64_t);
  const auto trim = group.trimming.capacity() * sizeof(d::TrimSpan);
  const auto discard = (group.discarded.capacity() + 7) / 8;
  const auto sum = sizeof(group) + sizeof(d::FiniteGroup) + control + live + trim + discard;
  const auto total = fixture.statistics().metadata_bytes;
  if (group.metadata_charge != sum || total - empty != sum) std::abort();
  std::printf("{\"storage\":%zu,\"palette\":\"%s\",\"Group\":%zu,\"FiniteGroup\":%zu,"
      "\"control_mapping\":%zu,\"live_capacity\":%zu,\"trim_capacity\":%zu,"
      "\"discard_capacity\":%zu,\"group_metadata_charge\":%llu,\"empty_pool\":%llu,"
      "\"populated_pool\":%llu,\"group_vector_capacity_bytes_excluded_from_public_pool\":%zu,"
      "\"native_page_bytes\":%zu}\n", sizeof(oa::Storage), bytes <= 64 ? "A" : "B",
      sizeof(group), sizeof(d::FiniteGroup), control, live, trim, discard,
      static_cast<unsigned long long>(sum), static_cast<unsigned long long>(empty),
      static_cast<unsigned long long>(total), fixture.pool->internal->groups.capacity() * sizeof(void*), d::page_size());
}

int main() { measure(64); measure(1536); }
