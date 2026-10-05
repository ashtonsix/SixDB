#include "grouped.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace descriptors;
constexpr std::array<std::size_t, 15> oracle_sizes{
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072};
constexpr std::array layouts{
    grouped_layout::grid_major, grouped_layout::class_major,
    grouped_layout::class_major_reordered, grouped_layout::class_aligned};
struct interval {
  std::size_t grid;
  unsigned cls;
  unsigned slot;
  std::size_t begin;
};
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::size_t round_multiple(std::size_t bytes, std::size_t unit) {
  return ((bytes + unit - 1) / unit) * unit;
}

/// Enumerate complete runs rather than reconstructing the production LUT
/// formula.
std::vector<interval> oracle(grouped_layout kind, std::size_t grids,
                             std::size_t granule, std::size_t &span) {
  std::vector<interval> result;
  std::size_t cursor = 0;
  const auto emit = [&](std::size_t grid, unsigned cls) {
    for (unsigned slot = 0; slot < 16; ++slot) {
      result.push_back({grid, cls, slot, cursor});
      cursor += oracle_sizes[cls];
    }
  };
  if (kind == grouped_layout::grid_major) {
    for (std::size_t grid = 0; grid < grids; ++grid) {
      for (unsigned cls = 0; cls < 5; ++cls)
        emit(grid, cls);
      cursor = round_multiple(cursor, granule);
    }
  } else {
    std::array<unsigned, 5> order{0, 1, 2, 3, 4};
    if (kind == grouped_layout::class_major_reordered)
      order = {3, 0, 2, 1, 4};
    for (const auto cls : order) {
      if (kind == grouped_layout::class_aligned)
        cursor = round_multiple(cursor, granule);
      for (std::size_t grid = 0; grid < grids; ++grid)
        emit(grid, cls);
    }
    cursor = round_multiple(cursor, granule);
  }
  for (std::size_t grid = 0; grid < grids; ++grid) {
    for (unsigned cls = 5; cls < 15; ++cls) {
      cursor = round_multiple(cursor, granule);
      emit(grid, cls);
    }
    cursor = round_multiple(cursor, granule);
  }
  span = cursor;
  auto sorted = result;
  std::sort(sorted.begin(), sorted.end(),
            [](auto a, auto b) { return a.begin < b.begin; });
  for (std::size_t i = 1; i < sorted.size(); ++i)
    check(sorted[i - 1].begin + oracle_sizes[sorted[i - 1].cls] <=
              sorted[i].begin,
          "oracle home intervals overlap");
  return result;
}

std::size_t address_checks(grouped_layout kind, std::size_t width,
                           std::size_t native_page, std::size_t granule) {
  grouped_arena arena(kind, width, 2, granule, 41);
  auto first = arena.allocate_in_grid(0, 16);
  check(first != 0, "first identity unavailable");
  const auto base = native(first);
  arena.release(first);
  std::size_t span;
  const auto intervals = oracle(kind, width / 16, granule, span);
  const auto &geometry = arena.layout();
  check(geometry.native_page == native_page && geometry.payload_bytes == span,
        "oracle payload span disagrees with geometry");
  for (std::size_t group = 0; group < 2; ++group)
    for (const auto item : intervals) {
      auto *cell = reinterpret_cast<std::uint8_t *>(
          base + group * geometry.group_bytes + item.grid * 16);
      *cell = std::uint8_t((item.slot << 4) | item.cls);
      const Head head = reinterpret_cast<Head>(cell) | (Head{41} << 57);
      const auto expected =
          base + group * geometry.group_bytes + granule + item.begin;
      check(reinterpret_cast<Head>(grouped_resolve(head, geometry)) == expected,
            "native resolver disagrees with enumerated address oracle");
    }
  return intervals.size() * 2;
}

std::size_t lifecycle_checks(grouped_layout kind, std::size_t width,
                             std::size_t granule) {
  grouped_arena arena(kind, width, 1, granule, 17);
  std::size_t checks = 0;
  for (std::size_t bytes = 1; bytes <= 3072; ++bytes) {
    const auto expected = *std::lower_bound(oracle_sizes.begin(),
                                          oracle_sizes.end(), bytes);
    const auto head = arena.allocate_in_grid(0, bytes);
    check(head && arena.size(head) == bytes && arena.capacity(head) == expected,
          "finite class lookup disagrees with independent capacity search");
    arena.release(head);
    ++checks;
  }
  bool rejected_zero = false;
  try {
    arena.allocate(0);
  } catch (const std::invalid_argument &) {
    rejected_zero = true;
  }
  check(rejected_zero, "zero-byte allocation silently admitted");
  std::size_t lower = 0;
  for (const auto capacity : oracle_sizes) {
    const auto head = arena.allocate_in_grid(0, lower + 1);
    check(head && arena.size(head) == lower + 1 &&
              arena.capacity(head) == capacity,
          "reservation class boundary mismatch");
    arena.release(head);
    lower = capacity;
    ++checks;
  }
  bool rejected = false;
  try {
    arena.allocate_in_grid(0, 3073);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "unassigned virtual class silently admitted");
  arena.trim();
  for (const auto bytes : oracle_sizes) {
    std::vector<Head> heads;
    for (unsigned identity = 0; identity < 16; ++identity) {
      const auto head = arena.allocate_in_grid(0, bytes);
      check(head && arena.size(head) == bytes && arena.capacity(head) == bytes,
            "reservation length/capacity mismatch");
      const std::uint64_t value = bytes * 100 + identity;
      std::memcpy(grouped_resolve(head, arena.layout()), &value, sizeof(value));
      heads.push_back(head);
    }
    check(!arena.allocate_in_grid(0, bytes),
          "seventeenth grid identity admitted");
    const auto old_head = heads[5];
    const auto old_payload = grouped_resolve(old_head, arena.layout());
    const auto maps = arena.counters().map_calls;
    arena.release(old_head);
    heads[5] = arena.allocate_in_grid(0, bytes);
    check(heads[5] == old_head &&
              grouped_resolve(heads[5], arena.layout()) == old_payload,
          "quiescent identity/home was not reusable");
    check(arena.counters().map_calls == maps,
          "prepared reuse remapped backing");
    const std::uint64_t replacement = 0xfedcba9876543210ULL;
    std::memcpy(grouped_resolve(heads[5], arena.layout()), &replacement,
                sizeof(replacement));
    for (unsigned identity = 0; identity < 16; ++identity) {
      std::uint64_t observed;
      std::memcpy(&observed, grouped_resolve(heads[identity], arena.layout()),
                  sizeof(observed));
      check(observed == (identity == 5 ? replacement : bytes * 100 + identity),
            "reuse corrupted another live home");
      arena.release(heads[identity]);
    }
    check(arena.counters().live_allocations == 0,
          "live owner remains after release");
    check(arena.counters().cached_empty_granules ==
              arena.counters().payload_granules,
          "empty reusable backing count disagrees");
    check(arena.trim() > 0 && arena.counters().payload_granules == 0,
          "empty payload backing was not actually discarded");
    checks += 16;
  }
  // Verify that one grid cannot discard a granule still occupied by a neighbor.
  std::vector<Head> cohort;
  for (std::size_t grid = 0; grid < arena.grids(); ++grid)
    for (unsigned identity = 0; identity < 16; ++identity) {
      const auto head = arena.allocate_in_grid(grid, 64);
      const std::uint64_t value = 1000 + cohort.size();
      std::memcpy(grouped_resolve(head, arena.layout()), &value, sizeof(value));
      cohort.push_back(head);
    }
  for (unsigned identity = 0; identity < 16; ++identity)
    arena.release(cohort[identity]);
  arena.trim();
  for (std::size_t index = 16; index < cohort.size(); ++index) {
    std::uint64_t observed;
    std::memcpy(&observed, grouped_resolve(cohort[index], arena.layout()),
                sizeof(observed));
    check(observed == 1000 + index,
          "trim discarded another grid's live payload");
    arena.release(cohort[index]);
    ++checks;
  }
  arena.trim();
  check(arena.counters().live_allocations == 0 &&
            arena.counters().payload_granules == 0,
        "cohort backing remains after complete release/trim");
  return checks;
}

std::set<std::uintptr_t> covered_pages(const std::byte *payload,
                                      std::size_t bytes,
                                      std::size_t granule) {
  std::set<std::uintptr_t> pages;
  const auto begin = reinterpret_cast<std::uintptr_t>(payload);
  const auto first = (begin / granule) * granule;
  const auto end = ((begin + bytes - 1) / granule) * granule;
  for (auto page = first; page <= end; page += granule)
    pages.insert(page);
  return pages;
}

std::size_t sparse_checks(grouped_layout kind, std::size_t width,
                         std::size_t granule) {
  grouped_arena arena(kind, width, 2, granule, 33);
  struct allocation {
    Head head;
    std::byte *payload;
    std::size_t capacity;
    unsigned char value;
  };
  std::vector<allocation> live;
  std::set<std::uintptr_t> mapped;
  std::uint64_t entropy = 0x123456789abcdefULL;
  const auto random = [&] {
    entropy ^= entropy << 13;
    entropy ^= entropy >> 7;
    entropy ^= entropy << 17;
    return entropy;
  };
  std::size_t checks = 0;
  const auto verify_trim = [&] {
    std::set<std::uintptr_t> used;
    for (const auto &item : live) {
      const auto pages = covered_pages(item.payload, item.capacity, granule);
      used.insert(pages.begin(), pages.end());
      check(grouped_resolve(item.head, arena.layout()) == item.payload,
            "mixed reuse changed a live payload address");
      check(std::all_of(item.payload, item.payload + item.capacity,
                        [&](std::byte byte) {
                          return byte == std::byte{item.value};
                        }),
            "mixed reuse/trim changed live payload bytes");
    }
    const auto counters = arena.counters();
    check(counters.live_allocations == live.size() &&
              counters.payload_granules == mapped.size() &&
              counters.cached_empty_granules == mapped.size() - used.size(),
          "sparse page counters disagree with byte-interval oracle");
    check(arena.trim() == mapped.size() - used.size(),
          "sparse trim count disagrees with byte-interval oracle");
    mapped = std::move(used);
    check(arena.counters().payload_granules == mapped.size(),
          "sparse trim left incorrect mapped count");
    ++checks;
  };
  // Fully occupied mixed grids, then holes throughout both control groups.
  for (std::size_t grid = 0; grid < arena.grids(); ++grid)
    for (unsigned identity = 0; identity < 16; ++identity) {
      const auto capacity = oracle_sizes[(grid + identity) % 15];
      const auto head = arena.allocate_in_grid(grid, capacity - 1);
      check(head != 0, "mixed initial grid could not fill all identities");
      auto *payload = grouped_resolve(head, arena.layout());
      const auto pages = covered_pages(payload, capacity, granule);
      mapped.insert(pages.begin(), pages.end());
      const auto value = static_cast<unsigned char>(1 + (grid + identity) % 253);
      std::memset(payload, value, capacity);
      live.push_back({head, payload, capacity, value});
    }
  check(!arena.allocate(64), "full mixed arena admitted another identity");
  verify_trim();
  for (auto index = live.size(); index-- > 0;)
    if (index % 3 == 0) {
      arena.release(live[index].head);
      live[index] = live.back();
      live.pop_back();
    }
  verify_trim();
  for (unsigned step = 0; step < 768; ++step) {
    const auto decision = random();
    if (!live.empty() && decision % 5 < 2) {
      const auto index = random() % live.size();
      arena.release(live[index].head);
      live[index] = live.back();
      live.pop_back();
    } else {
      const auto capacity = oracle_sizes[random() % oracle_sizes.size()];
      const auto bytes = 1 + random() % capacity;
      const auto expected = *std::lower_bound(oracle_sizes.begin(),
                                            oracle_sizes.end(), bytes);
      const auto head = decision & 8
                            ? arena.allocate(bytes)
                            : arena.allocate_in_grid(random() % arena.grids(),
                                                     bytes);
      if (head) {
        auto *payload = grouped_resolve(head, arena.layout());
        const auto pages = covered_pages(payload, expected, granule);
        mapped.insert(pages.begin(), pages.end());
        const auto value = static_cast<unsigned char>(1 + step % 253);
        std::memset(payload, value, expected);
        live.push_back({head, payload, expected, value});
      }
    }
    if (step % 19 == 0)
      verify_trim();
  }
  while (live.size() > 3) {
    arena.release(live.back().head);
    live.pop_back();
  }
  verify_trim();
  for (const auto &item : live)
    arena.release(item.head);
  check(arena.trim() == mapped.size() &&
            !arena.counters().payload_granules,
        "mixed history failed final backing reclamation");
  return checks + 1;
}

std::size_t decode_checks(grouped_layout kind, std::size_t width,
                         std::size_t granule) {
  // All group starts exercise the reciprocal's correction at exact span
  // multiples; last rows and identities exercise the far control boundary.
  grouped_arena arena(kind, width, 17, granule, 34);
  std::vector<Head> heads;
  for (std::size_t group = 0; group < arena.groups(); ++group) {
    const auto first = group * (width / 16);
    const auto last = first + width / 16 - 1;
    const auto edges = first == last ? 1u : 2u;
    for (unsigned edge = 0; edge < edges; ++edge) {
      const auto grid = edge ? last : first;
      for (unsigned identity = 0; identity < 16; ++identity) {
        const auto bytes = oracle_sizes[(group + identity) % 15];
        const auto head = arena.allocate_in_grid(grid, bytes);
        check(head && arena.size(head) == bytes &&
                  arena.capacity(head) == bytes,
              "nonzero group/last-row head decoding failed");
        heads.push_back(head);
      }
    }
  }
  const auto first = heads.front();
  const auto start = native(first);
  const auto invalid = [&](Head head) {
    bool rejected = false;
    try {
      arena.release(head);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    check(rejected, "invalid grouped head was admitted");
  };
  invalid(start | (Head{35} << 57));
  invalid((start + width) | (Head{34} << 57));
  invalid((start + granule) | (Head{34} << 57));
  invalid((start + arena.reserved_bytes()) | (Head{34} << 57));
  for (const auto head : heads) {
    arena.release(head);
    invalid(head);
  }
  check(!arena.counters().live_allocations, "decode history left live rows");
  const auto released = arena.trim();
  check(released && !arena.counters().payload_granules,
        "decode history backing was not reclaimed");
  const auto repopulated = arena.allocate_in_grid(arena.grids() - 1, 3072);
  check(repopulated && arena.size(repopulated) == 3072,
        "last grid could not repopulate after trim");
  std::memset(grouped_resolve(repopulated, arena.layout()), 0x67, 3072);
  arena.release(repopulated);
  check(arena.trim() && !arena.counters().payload_granules,
        "repopulated last-grid backing was not reclaimed");
  return heads.size() + 6;
}

void straddling_trim_check() {
  grouped_arena arena(grouped_layout::class_major, 64, 2, 4096, 35);
  std::vector<Head> heads;
  for (unsigned slot = 0; slot < 6; ++slot)
    heads.push_back(arena.allocate_in_grid(1, 96));
  auto *payload = grouped_resolve(heads.back(), arena.layout());
  const auto start = native(heads.front()) & ~Head{4095};
  check(reinterpret_cast<Head>(payload) - start - 4096 == 12256,
        "96-byte straddle witness changed geometry");
  std::memset(payload, 0xab, 96);
  for (unsigned slot = 0; slot < 5; ++slot)
    arena.release(heads[slot]);
  check(arena.counters().payload_granules == 2 &&
            !arena.counters().cached_empty_granules && !arena.trim(),
        "straddling survivor did not retain both payload granules");
  check(std::all_of(payload, payload + 96,
                    [](std::byte byte) { return byte == std::byte{0xab}; }),
        "trim discarded bytes of the straddling survivor");
  arena.release(heads.back());
  check(arena.trim() == 2 && !arena.counters().payload_granules,
        "straddling final release did not reclaim both granules");
}
} // namespace

int main() {
  try {
    const auto native_page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    std::vector<std::size_t> granules{native_page};
    if (native_page == 4096)
      granules.push_back(16384);
    std::size_t addresses = 0, lifecycles = 0, sparse = 0, decodes = 0;
    for (const auto granule : granules)
      for (const auto kind : layouts)
        for (const auto width : {16u, 64u, 256u, 1024u}) {
          addresses += address_checks(kind, width, native_page, granule);
          lifecycles += lifecycle_checks(kind, width, granule);
          sparse += sparse_checks(kind, width, granule);
          decodes += decode_checks(kind, width, granule);
        }
    if (native_page == 4096)
      straddling_trim_check();
    std::cout << "grouped native checks passed: address_coordinates="
              << addresses << " free_reuse_checks=" << lifecycles
              << " sparse_trim_checks=" << sparse
              << " group_decode_checks=" << decodes
              << " native_page=" << native_page
              << " modeled_16384_mapping_granule=" << (native_page == 4096)
              << '\n';
  } catch (const std::exception &failure) {
    std::cerr << "grouped native probe: " << failure.what() << '\n';
    return 1;
  }
}
