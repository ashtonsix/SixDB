#include "grouped.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace descriptors {
namespace {
std::size_t align_up(std::size_t bytes, std::size_t alignment) {
  return (bytes + alignment - 1) & ~(alignment - 1);
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
unsigned select_class(std::size_t bytes) {
  if (!bytes || bytes > grouped_capacities[14])
    throw std::invalid_argument(
        "grouped candidate requires 1--3072 bytes; virtual homes unassigned");
  static constexpr auto classes = [] {
    std::array<std::uint8_t, grouped_capacities[14] / 16> result{};
    unsigned cls = 0;
    for (std::size_t index = 0; index < result.size(); ++index) {
      while ((index + 1) * 16 > grouped_capacities[cls])
        ++cls;
      result[index] = static_cast<std::uint8_t>(cls);
    }
    return result;
  }();
  return classes[(bytes - 1) >> 4];
}
struct grid_state {
  std::uint16_t identities{};
  std::array<std::uint16_t, 15> homes{};
  std::array<std::uint16_t, 16> lengths{};
};
static_assert(sizeof(grid_state) == 64);
} // namespace

const char *grouped_layout_name(grouped_layout kind) {
  switch (kind) {
  case grouped_layout::grid_major:
    return "grid_major";
  case grouped_layout::class_major:
    return "class_major";
  case grouped_layout::class_major_reordered:
    return "class_major_reordered";
  case grouped_layout::class_aligned:
    return "class_aligned";
  }
  throw std::invalid_argument("unknown grouped layout");
}

grouped_geometry make_grouped_geometry(grouped_layout kind, std::size_t width,
                                       std::size_t native_page,
                                       std::size_t mapping_granule) {
  if ((width != 16 && width != 64 && width != 256 && width != 1024) ||
      !std::has_single_bit(native_page) ||
      !std::has_single_bit(mapping_granule) || mapping_granule < native_page ||
      mapping_granule < width)
    throw std::invalid_argument("unsupported grouped geometry");
  grouped_geometry result;
  result.kind = kind;
  result.native_page = native_page;
  result.mapping_granule = mapping_granule;
  result.trampolines_per_group = width;
  result.grids_per_group = width / 16;
  result.stride = grouped_capacities;
  std::size_t small_end = 0;
  if (kind == grouped_layout::grid_major) {
    for (unsigned cls = 0; cls < 5; ++cls) {
      result.class_offset[cls] = small_end;
      result.grid_step[cls] = mapping_granule;
      small_end += 16 * result.stride[cls];
    }
    // The original small five-class aperture occupies one mapping granule/grid.
    if (small_end > mapping_granule)
      throw std::invalid_argument("small grid exceeds mapping granule");
    small_end = result.grids_per_group * mapping_granule;
  } else {
    constexpr std::array<unsigned, 5> ascending{0, 1, 2, 3, 4};
    constexpr std::array<unsigned, 5> reordered{3, 0, 2, 1, 4};
    const auto &order =
        kind == grouped_layout::class_major_reordered ? reordered : ascending;
    for (const auto cls : order) {
      if (kind == grouped_layout::class_aligned)
        small_end = align_up(small_end, mapping_granule);
      result.class_offset[cls] = small_end;
      result.grid_step[cls] = 16 * result.stride[cls];
      small_end += width * result.stride[cls];
    }
    small_end = align_up(small_end, mapping_granule);
  }
  std::size_t large_end = 0;
  for (unsigned cls = 5; cls < 15; ++cls) {
    large_end = align_up(large_end, mapping_granule);
    result.class_offset[cls] = small_end + large_end;
    large_end += 16 * result.stride[cls];
  }
  large_end = align_up(large_end, mapping_granule);
  for (unsigned cls = 5; cls < 15; ++cls)
    result.grid_step[cls] = large_end;
  result.payload_bytes = small_end + result.grids_per_group * large_end;
  result.group_bytes = mapping_granule + result.payload_bytes;
  return result;
}

struct grouped_owner {
  std::vector<grid_state> grids;
  std::vector<std::uint8_t> mapped;
};

namespace {
/// The occupancy masks are already authoritative. Reconstruct page liveness
/// for trimming/reporting rather than counting every page on the hot path.
void mark_live_pages(const grouped_owner &owner,
                     const grouped_geometry &geometry, unsigned granule_shift,
                     std::vector<std::uint8_t> &live) {
  std::fill(live.begin(), live.end(), 0);
  for (std::size_t row_index = 0; row_index < owner.grids.size(); ++row_index) {
    const auto &row = owner.grids[row_index];
    if (!row.identities)
      continue;
    for (unsigned cls = 0; cls < 15; ++cls) {
      auto occupied = row.homes[cls];
      if (!occupied)
        continue;
      const auto base = geometry.class_offset[cls] +
                        row_index * geometry.grid_step[cls];
      // All sixteen homes of a class form one contiguous byte interval in
      // every candidate layout. Dense rows need page work, not home work.
      if (occupied == 0xffff) {
        const auto first = base >> granule_shift;
        const auto last =
            (base + 16 * geometry.stride[cls] - 1) >> granule_shift;
        for (auto page = first; page <= last; ++page)
          live[page] = 1;
        continue;
      }
      while (occupied) {
        const auto slot = std::countr_zero(occupied);
        const auto begin = base + slot * geometry.stride[cls];
        const auto first = begin >> granule_shift;
        const auto last =
            (begin + geometry.stride[cls] - 1) >> granule_shift;
        for (auto page = first; page <= last; ++page)
          live[page] = 1;
        occupied &= static_cast<std::uint16_t>(occupied - 1);
      }
    }
  }
}
} // namespace

grouped_arena::grouped_arena(grouped_layout kind, std::size_t width,
                             std::size_t groups, std::size_t mapping_granule,
                             unsigned id)
    : id_(id) {
  const auto native_page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  if (!mapping_granule)
    mapping_granule = native_page;
  if (!groups || id >= 64 ||
      (mapping_granule != native_page &&
       !(native_page == 4096 && mapping_granule == 16384)))
    throw std::invalid_argument(
        "unsupported native/model mapping configuration");
  geometry_ = make_grouped_geometry(kind, width, native_page, mapping_granule);
  grid_shift_ = std::countr_zero(geometry_.grids_per_group);
  granule_shift_ = std::countr_zero(mapping_granule);
  group_reciprocal_ = static_cast<std::uint64_t>(
      (static_cast<unsigned __int128>(1) << 64) / geometry_.group_bytes);
  if (groups > (std::numeric_limits<std::size_t>::max() - mapping_granule) /
                   geometry_.group_bytes)
    throw std::invalid_argument("grouped reservation size overflow");
  reserved_ = groups * geometry_.group_bytes;
  // Over-reserve and trim to establish modeled-granule alignment on 4 KiB
  // hosts.
  void *mapping = mmap(nullptr, reserved_ + mapping_granule, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  require(mapping != MAP_FAILED, "grouped address reservation failed");
  const auto start = reinterpret_cast<std::uintptr_t>(mapping);
  const auto aligned = align_up(start, mapping_granule);
  if (aligned != start)
    require(munmap(mapping, aligned - start) == 0,
            "grouped alignment prefix trim failed");
  require(munmap(reinterpret_cast<void *>(aligned + reserved_),
                 mapping_granule - (aligned - start)) == 0,
          "grouped alignment suffix trim failed");
  base_ = reinterpret_cast<std::byte *>(aligned);
  try {
    require(!(aligned & tag_mask), "native addresses overlap arena metadata");
    groups_.reserve(groups);
    for (std::size_t group = 0; group < groups; ++group) {
      auto owner = std::make_unique<grouped_owner>();
      owner->grids.resize(geometry_.grids_per_group);
      owner->mapped.resize(geometry_.payload_bytes / mapping_granule);
      auto *control = base_ + group * geometry_.group_bytes;
      require(mprotect(control, mapping_granule, PROT_READ | PROT_WRITE) == 0,
              "grouped control mapping failed");
      // Fault every native page in the coarser mapping model; its TLB stays
      // native.
      for (std::size_t byte = 0; byte < mapping_granule; byte += native_page)
        *reinterpret_cast<volatile std::byte *>(control + byte) = std::byte{0};
      groups_.push_back(std::move(owner));
    }
  } catch (...) {
    munmap(base_, reserved_);
    base_ = nullptr;
    throw;
  }
}

grouped_arena::~grouped_arena() {
  if (base_)
    munmap(base_, reserved_);
}

Head grouped_arena::allocate(std::size_t bytes) {
  const auto cls = select_class(bytes);
  while (next_grid_ < grids()) {
    if (const auto head = allocate_selected(next_grid_, cls, bytes))
      return head;
    ++next_grid_;
  }
  return 0;
}

Head grouped_arena::allocate_in_grid(std::size_t grid, std::size_t bytes) {
  const auto cls = select_class(bytes);
  if (grid >= grids())
    throw std::out_of_range("grouped grid index");
  return allocate_selected(grid, cls, bytes);
}

Head grouped_arena::allocate_selected(std::size_t grid, unsigned cls,
                                     std::size_t bytes) {
  const auto group = grid >> grid_shift_;
  const auto row_index = grid & (geometry_.grids_per_group - 1);
  auto &owner = *groups_[group];
  auto &row = owner.grids[row_index];
  const auto available = static_cast<std::uint16_t>(~row.identities);
  const auto vacant = static_cast<std::uint16_t>(~row.homes[cls]);
  if (!available || !vacant)
    return 0;
  const auto identity = std::countr_zero(available);
  const auto slot = std::countr_zero(vacant);
  const auto coordinate = static_cast<std::uint8_t>((slot << 4) | cls);
  const auto begin = geometry_.class_offset[cls] +
                     row_index * geometry_.grid_step[cls] +
                     slot * geometry_.stride[cls];
  const auto first = begin >> granule_shift_;
  const auto last =
      (begin + geometry_.stride[cls] - 1) >> granule_shift_;
  if (!owner.mapped[first] || !owner.mapped[last]) [[unlikely]]
    map_payload(owner, group, first, last);
  row.identities |= std::uint16_t(1u << identity);
  row.homes[cls] |= std::uint16_t(1u << slot);
  row.lengths[identity] = static_cast<std::uint16_t>(bytes);
  auto *cell = reinterpret_cast<std::uint8_t *>(
      base_ + group * geometry_.group_bytes + row_index * 16 + identity);
  __atomic_store_n(cell, coordinate, __ATOMIC_RELEASE);
  ++counters_.live_allocations;
  ++counters_.allocations;
  return reinterpret_cast<Head>(cell) | (Head{id_} << 57);
}

[[gnu::noinline]] void grouped_arena::map_payload(grouped_owner &owner,
                                               std::size_t group,
                                               std::size_t first,
                                               std::size_t last) {
  auto *payload_base =
      base_ + group * geometry_.group_bytes + geometry_.mapping_granule;
  for (auto page = first; page <= last; ++page) {
    if (!owner.mapped[page]) {
      auto *address = payload_base + page * geometry_.mapping_granule;
      require(mprotect(address, geometry_.mapping_granule,
                       PROT_READ | PROT_WRITE) == 0,
              "grouped payload mapping failed");
      for (std::size_t byte = 0; byte < geometry_.mapping_granule;
           byte += geometry_.native_page)
        *reinterpret_cast<volatile std::byte *>(address + byte) = std::byte{0};
      owner.mapped[page] = 1;
      ++counters_.payload_granules;
      ++counters_.map_calls;
    }
  }
}

[[gnu::always_inline]] grouped_arena::head_location
grouped_arena::locate(Head head) const {
  if (arena_id(head) != id_)
    throw std::invalid_argument("grouped arena identifier");
  const auto address = native(head);
  const auto start = reinterpret_cast<Head>(base_);
  if (address < start || address >= start + reserved_)
    throw std::invalid_argument("grouped head address");
  const auto offset = address - start;
  // floor(2^64 / span) underestimates offset/span by at most one for
  // 64-bit offsets. Correcting the remainder avoids a runtime division.
  auto group = static_cast<std::size_t>(
      (static_cast<unsigned __int128>(offset) * group_reciprocal_) >> 64);
  auto control_offset = offset - group * geometry_.group_bytes;
  if (control_offset >= geometry_.group_bytes) {
    ++group;
    control_offset -= geometry_.group_bytes;
  }
  if (control_offset >= geometry_.trampolines_per_group)
    throw std::invalid_argument("grouped head outside trampoline zone");
  const auto row = control_offset / 16;
  const auto identity = control_offset % 16;
  auto *owner = groups_[group].get();
  if (!(owner->grids[row].identities & (1u << identity)))
    throw std::invalid_argument("grouped identity is not live");
  return {owner, row, (group << grid_shift_) + row,
          static_cast<unsigned>(identity)};
}

void grouped_arena::release(Head head) {
  const auto location = locate(head);
  auto &row = location.owner->grids[location.row];
  const auto identity = location.identity;
  const auto coordinate = *reinterpret_cast<const std::uint8_t *>(native(head));
  const auto cls = coordinate & 15;
  const auto slot = coordinate >> 4;
  assert(cls < 15 && (row.homes[cls] & (1u << slot)));
  row.identities &= std::uint16_t(~(1u << identity));
  row.homes[cls] &= std::uint16_t(~(1u << slot));
  row.lengths[identity] = 0;
  next_grid_ = std::min(next_grid_, location.grid);
  --counters_.live_allocations;
  ++counters_.releases;
}

std::size_t grouped_arena::trim() {
  std::size_t released = 0;
  std::vector<std::uint8_t> live(geometry_.payload_bytes >> granule_shift_);
  for (std::size_t group = 0; group < groups_.size(); ++group) {
    auto &owner = *groups_[group];
    mark_live_pages(owner, geometry_, granule_shift_, live);
    auto *payload =
        base_ + group * geometry_.group_bytes + geometry_.mapping_granule;
    for (std::size_t page = 0; page < owner.mapped.size(); ++page)
      if (owner.mapped[page] && !live[page]) {
        auto *address = payload + page * geometry_.mapping_granule;
        require(madvise(address, geometry_.mapping_granule, MADV_DONTNEED) == 0,
                "grouped backing discard failed");
        require(mprotect(address, geometry_.mapping_granule, PROT_NONE) == 0,
                "grouped mapping revoke failed");
        owner.mapped[page] = 0;
        --counters_.payload_granules;
        ++counters_.released_granules;
        ++released;
      }
  }
  return released;
}

std::size_t grouped_arena::size(Head head) const {
  const auto location = locate(head);
  return location.owner->grids[location.row].lengths[location.identity];
}
std::size_t grouped_arena::capacity(Head head) const {
  locate(head);
  return geometry_
      .stride[*reinterpret_cast<const std::uint8_t *>(native(head)) & 15];
}
grouped_counters grouped_arena::counters() const {
  auto result = counters_;
  std::vector<std::uint8_t> live(geometry_.payload_bytes >> granule_shift_);
  for (const auto &owner : groups_) {
    mark_live_pages(*owner, geometry_, granule_shift_, live);
    for (std::size_t page = 0; page < owner->mapped.size(); ++page)
      result.cached_empty_granules +=
          owner->mapped[page] && !live[page];
  }
  return result;
}
std::size_t grouped_arena::owner_record_bytes() const {
  std::size_t bytes = sizeof(*this) + groups_.capacity() * sizeof(groups_[0]);
  for (const auto &owner : groups_)
    bytes += sizeof(*owner) + owner->grids.capacity() * sizeof(grid_state) +
             owner->mapped.capacity();
  return bytes;
}
} // namespace descriptors
