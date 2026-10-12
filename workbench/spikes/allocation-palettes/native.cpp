#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "../../../orbital/src/allocation/internal.h"

namespace {

constexpr std::size_t grids = 16, slots = 16, classes = 15;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

std::size_t number(std::string_view text) {
  std::size_t value{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
          "invalid integer in geometry TSV");
  return value;
}

std::vector<std::string_view> fields(std::string_view line) {
  std::vector<std::string_view> result;
  for (;;) {
    const auto tab = line.find('\t');
    result.push_back(line.substr(0, tab));
    if (tab == std::string_view::npos) return result;
    line.remove_prefix(tab + 1);
  }
}

struct Class {
  std::size_t capacity{}, offset{}, stride{};
};

struct Geometry {
  std::size_t payload{}, class_count{}, slot_count{}, class_bits{};
  std::vector<Class> table;
  std::vector<bool> present;
};

using Key = std::pair<std::string, std::size_t>;

std::map<Key, Geometry> read(std::istream& input) {
  std::string line;
  require(static_cast<bool>(std::getline(input, line)), "empty geometry TSV");
  if (!line.empty() && line.back() == '\r') line.pop_back();
  require(line == "layout\tpage\tpayload_bytes\tgrid_stride\tcls\tcapacity\toffset\tclasses\tslots\tclass_bits",
          "unexpected geometry TSV header");
  std::map<Key, Geometry> result;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const auto row = fields(line);
    require(row.size() == 10 && !row[0].empty(), "invalid geometry TSV row");
    require(row[0].find_first_of("\"\\") == std::string_view::npos,
            "layout name needs JSON escaping");
    const auto page = number(row[1]);
    const auto payload = number(row[2]);
    const auto cls = number(row[4]);
    const auto class_count = number(row[7]);
    const auto slot_count = number(row[8]);
    const auto class_bits = number(row[9]);
    require(class_bits >= 4 && class_bits <= 8 && class_count &&
            class_count <= (std::size_t{1} << class_bits) &&
            slot_count == (std::size_t{1} << (8 - class_bits)) && cls < class_count,
            "class/slot partition does not fit one-byte binding");
    auto& geometry = result[{std::string(row[0]), page}];
    if (geometry.table.empty()) {
      geometry.class_count = class_count;
      geometry.slot_count = slot_count;
      geometry.class_bits = class_bits;
      geometry.table.resize(class_count);
      geometry.present.resize(class_count);
    }
    require(geometry.class_count == class_count && geometry.slot_count == slot_count &&
            geometry.class_bits == class_bits, "inconsistent class/slot partition in TSV");
    require(!geometry.present[cls], "duplicate class in geometry TSV");
    require(!geometry.payload || geometry.payload == payload,
            "inconsistent mapping extent in geometry TSV");
    geometry.payload = payload;
    geometry.table[cls] = {number(row[5]), number(row[6]), number(row[3])};
    geometry.present[cls] = true;
  }
  require(!result.empty(), "geometry TSV has no rows");
  return result;
}

class Mapping {
public:
  Mapping(std::size_t extent, std::size_t alignment) : extent_(extent) {
    require(extent <= std::numeric_limits<std::size_t>::max() - alignment,
            "mapping extent overflow");
    const auto total = extent + alignment;
    auto* reservation = ::mmap(nullptr, total, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(reservation != MAP_FAILED, "mmap failed");
    const auto start = reinterpret_cast<std::uintptr_t>(reservation);
    const auto aligned = (start + alignment - 1) & ~(alignment - 1);
    const auto prefix = aligned - start;
    const auto suffix = total - prefix - extent;
    // Alignment is real; a larger policy granule is not a larger OS base page.
    if (prefix && ::munmap(reservation, prefix) != 0) {
      (void)::munmap(reservation, total);
      throw std::runtime_error("prefix munmap failed");
    }
    auto* tail = reinterpret_cast<void*>(aligned + extent);
    if (suffix && ::munmap(tail, suffix) != 0) {
      (void)::munmap(reinterpret_cast<void*>(aligned), extent + suffix);
      throw std::runtime_error("suffix munmap failed");
    }
    data_ = reinterpret_cast<std::byte*>(aligned);
  }

  ~Mapping() { if (data_) (void)::munmap(data_, extent_); }
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  std::byte* data() const { return data_; }

private:
  std::byte* data_{};
  std::size_t extent_{};
};

struct Home {
  std::size_t offset{}, capacity{}, grid{}, cls{}, slot{};
  bool retained{};
};

std::byte pattern(const Home& home, std::size_t index) {
  return std::byte{static_cast<std::uint8_t>(
      1 + (home.cls * 37 + home.grid * 19 + home.slot * 11 + index * 3) % 251)};
}

std::uint16_t alignment_mask(const Class& cls, std::size_t grid,
                             std::size_t slot_count, std::size_t alignment) {
  const auto offset = cls.offset + grid * cls.stride;
  std::uint16_t result = 0;
  for (std::size_t slot = 0; slot < slot_count; ++slot)
    if ((offset + slot * cls.capacity) % alignment == 0)
      result |= static_cast<std::uint16_t>(1u << slot);
  return result;
}

void qualify(const Key& key, const Geometry& geometry, std::size_t host_page) {
  const auto& [layout, page] = key;
  require(std::has_single_bit(page) && page >= host_page && page % host_page == 0,
          "policy page must be a power-of-two multiple of the host base page");
  require(geometry.payload && geometry.payload % page == 0,
          "mapping extent must be policy-page aligned");
  const auto classes = geometry.class_count, slots = geometry.slot_count;
  const auto grids = std::size_t{256} / slots;
  std::vector<Home> homes;
  std::vector<std::size_t> offsets(classes * grids * slots);
  const auto home_index = [slots, grids](std::size_t cls, std::size_t grid, std::size_t slot) {
    return (cls * grids + grid) * slots + slot;
  };
  std::size_t count = 0, legal64 = 0, checked = 0;
  for (std::size_t cls = 0; cls < classes; ++cls) {
    if (!geometry.present[cls]) continue;
    ++count;
    const auto& c = geometry.table[cls];
    require(c.offset <= geometry.payload && c.capacity && c.capacity <= geometry.payload / slots &&
            c.stride <= geometry.payload / grids,
            "class capacity or stride exceeds mapping extent");
    for (std::size_t grid = 0; grid < grids; ++grid) {
      require(grid * c.stride <= geometry.payload - c.offset,
              "class/grid offset exceeds mapping extent");
      auto offset = c.offset + grid * c.stride;
      for (std::size_t slot = 0; slot < slots; ++slot) {
        require(c.capacity <= geometry.payload - offset,
                "home exceeds mapping extent");
        offsets[home_index(cls, grid, slot)] = offset;
        const auto coordinate = (slot << geometry.class_bits) | cls;
        if (coordinate != 0xff)
          homes.push_back({offset, c.capacity, grid, cls, slot,
                           (cls % 3 == 0 && grid == 0 && slot == 0) ||
                           (cls == classes - 1 && grid == grids - 1 &&
                            slot + 1 >= slots - 1)});
        offset += c.capacity;
      }
    }
  }
  require(count == classes, "layout must contain every configured class");
  std::sort(homes.begin(), homes.end(), [](const auto& left, const auto& right) {
    return left.offset < right.offset;
  });
  for (std::size_t index = 1; index < homes.size(); ++index)
    require(homes[index - 1].offset + homes[index - 1].capacity <= homes[index].offset,
            "homes overlap");

  Mapping mapping(geometry.payload, page);
  for (const auto& home : homes) {
    const auto coordinate = static_cast<std::uint8_t>((home.slot << geometry.class_bits) | home.cls);
    require(coordinate != 0xff, "home collides with unbound byte");
    const auto class_mask = (std::size_t{1} << geometry.class_bits) - 1;
    const auto& c = geometry.table[coordinate & class_mask];
    const auto decoded = c.offset + home.grid * c.stride +
                         (coordinate >> geometry.class_bits) * c.capacity;
    require(decoded == offsets[home_index(home.cls, home.grid, home.slot)],
            "one-byte coordinate decode differs from enumerated home");
    auto* address = mapping.data() + decoded;
    require(reinterpret_cast<std::uintptr_t>(address) % 16 == 0,
            "home does not support 16-byte alignment");
    for (const auto alignment : {std::size_t{16}, std::size_t{64}}) {
      const auto mask = alignment_mask(c, home.grid, slots, alignment);
      const auto expected = reinterpret_cast<std::uintptr_t>(address) % alignment == 0;
      require(static_cast<bool>(mask & (1u << home.slot)) == expected,
              "alignment mask differs from actual home address");
      if (alignment == 64) legal64 += expected;
    }
    for (std::size_t index = 0; index < home.capacity; ++index)
      address[index] = pattern(home, index);
  }

  const auto verify = [&](bool retained_only) {
    for (const auto& home : homes) {
      if (retained_only && !home.retained) continue;
      for (std::size_t index = 0; index < home.capacity; ++index) {
        require(mapping.data()[home.offset + index] == pattern(home, index),
                "home bytes changed");
        ++checked;
      }
    }
  };
  verify(false);
  std::vector<bool> live(geometry.payload / page);
  std::size_t retained_homes = 0;
  for (const auto& home : homes) {
    if (!home.retained) continue;
    ++retained_homes;
    const auto first = home.offset / page;
    const auto last = (home.offset + home.capacity - 1) / page;
    for (auto index = first; index <= last; ++index) live[index] = true;
  }
  std::size_t discarded = 0;
  for (std::size_t granule = 0; granule < live.size(); ++granule) {
    if (live[granule]) continue;
    auto* address = mapping.data() + granule * page;
    require(::madvise(address, page, MADV_DONTNEED) == 0, "madvise failed");
    require(::mprotect(address, page, PROT_NONE) == 0, "mprotect failed");
    ++discarded;
  }
  verify(true);
  std::cout << "{\"layout\":\"" << layout << "\",\"host_page\":" << host_page
            << ",\"policy_granule\":" << page
            << ",\"mode\":\"emulated_policy_native_syscalls\",\"classes\":" << count
            << ",\"slots\":" << slots << ",\"class_bits\":" << geometry.class_bits
            << ",\"grids\":" << grids
            << ",\"homes\":" << homes.size() << ",\"legal_64b_homes\":" << legal64
            << ",\"checked_bytes\":" << checked << ",\"retained_homes\":" << retained_homes
            << ",\"discarded_granules\":" << discarded << ",\"payload_bytes\":"
            << geometry.payload << ",\"qualification\":\"pass\"}\n";
}

template<class T>
T take(orbital::Result<T> result, std::string_view message) {
  require(result.has_value(), message);
  return std::move(*result);
}

void okay(orbital::Result<void> result, std::string_view message) {
  require(result.has_value(), message);
}

void qualify_production(const std::map<Key, Geometry>& geometries, std::size_t host_page) {
  namespace allocation = orbital::allocation::detail;
  for (const auto palette : {allocation::Palette::a, allocation::Palette::b}) {
    const std::string layout = palette == allocation::Palette::a ? "A/class-aligned"
        : host_page >= 16384 ? "B-large/class-aligned" : "B/class-aligned";
    const auto expected = geometries.find({layout, host_page});
    require(expected != geometries.end(), "missing native production geometry in TSV");
    const auto& geometry = expected->second;
    require(geometry.class_count == classes && geometry.slot_count == slots && geometry.class_bits == 4 &&
            std::all_of(geometry.present.begin(), geometry.present.end(), [](bool p) { return p; }),
            "production geometry must contain all 15 classes");
    auto group = take(allocation::FiniteGroup::create({palette, 256}),
                      "production FiniteGroup create failed");
    okay(group->prepare(), "production FiniteGroup prepare failed");
    require(group->native_page() == host_page && group->identity_count() == grids * slots,
            "production native page or identity count differs");
    require(group->reserved_bytes() == geometry.payload &&
            group->payload_pages() == geometry.payload / host_page,
            "production payload extent differs from model");
    const auto control_prefix = 256 + 16 * (2 + 2 * classes * 2) + group->payload_pages();
    const auto expected_control = (control_prefix + 7) / 8 * 8 + 24 * group->live_words();
    require(group->control_bytes() == expected_control,
            "production geometry control extent differs from modeled inventory");
    auto* base = group->page_address(0);
    require(::mprotect(base, group->reserved_bytes(), PROT_READ | PROT_WRITE) == 0,
            "production payload mprotect failed");
    okay(group->mark_mapped({0, group->payload_pages()}),
         "production mapping inventory failed");
    std::array<allocation::Identity, grids * slots> identities{};
    for (std::size_t index = 0; index < identities.size(); ++index) {
      identities[index] = take(group->claim_identity(), "production identity claim failed");
      require(identities[index].index == index, "production identity census differs");
      require(group->resolve(identities[index]) == nullptr,
              "fresh production identity is already bound");
    }
    require(!group->claim_identity(), "production full identity census admits another identity");
    std::size_t homes_checked = 0;
    for (std::size_t grid = 0; grid < grids; ++grid) {
      for (std::size_t cls = 0; cls < classes; ++cls) {
        const auto& c = geometry.table[cls];
        std::array<allocation::Home, slots> homes{};
        std::uint16_t seen = 0;
        for (auto& home : homes) {
          home = take(group->claim_home(identities[grid * slots], c.capacity, 16),
                      "production home claim failed");
          require(home.grid == grid && home.cls == cls && home.slot < slots &&
                  !(seen & (1u << home.slot)), "production coordinate census differs");
          seen |= static_cast<std::uint16_t>(1u << home.slot);
          const auto offset = c.offset + grid * c.stride + home.slot * c.capacity;
          require(offset <= geometry.payload && c.capacity <= geometry.payload - offset,
                  "production expected home exceeds mapping extent");
          require(home.capacity == c.capacity && home.address == base + offset,
                  "production home address or capacity differs from model");
          const auto first = offset / host_page;
          const auto last = (offset + c.capacity - 1) / host_page;
          require(home.pages.first == first && home.pages.count == last - first + 1,
                  "production home page span differs from independent byte bounds");
          const auto identity = identities[grid * slots + home.slot];
          okay(group->bind(identity, home), "production bind failed");
          const auto coordinate = static_cast<std::uint8_t>((home.slot << 4) | cls);
          require(*group->descriptor(identity) == coordinate &&
                  group->resolve(identity) == base + offset &&
                  group->bound_capacity(identity) == c.capacity,
                  "production one-byte binding decode differs from model");
          const auto bound = take(group->bound_home(identity), "production bound_home failed");
          require(bound.grid == home.grid && bound.cls == home.cls && bound.slot == home.slot &&
                  bound.address == home.address && bound.capacity == home.capacity &&
                  bound.pages.first == first && bound.pages.count == last - first + 1,
                  "production bound Home differs from claimed Home");
          const auto value = std::byte{static_cast<std::uint8_t>(1 + cls + grid + home.slot)};
          home.address[0] = home.address[home.capacity - 1] = value;
          require(group->resolve(identity)[0] == value &&
                  group->resolve(identity)[home.capacity - 1] == value,
                  "production resolved pointer lost actual payload bytes");
          ++homes_checked;
        }
        require(seen == 0xffff &&
                !group->claim_home(identities[grid * slots], c.capacity, 16),
                "production selected-class home pressure differs");
        for (const auto& home : homes) {
          auto& identity = identities[grid * slots + home.slot];
          const auto index = identity.index;
          okay(group->release_identity(identity), "production bound identity release failed");
          require(group->resolve(identity) == nullptr && *group->descriptor(identity) == 0xff,
                  "production released identity retained binding");
          okay(group->release_home(home), "production unbound home release failed");
          identity = take(group->claim_identity(), "production identity reclaim failed");
          require(identity.index == index, "production identity reclaim changed dense identity");
        }
      }
    }
    for (const auto identity : identities)
      okay(group->release_identity(identity), "production final identity release failed");
    std::cout << "{\"layout\":\"" << layout << "\",\"host_page\":" << host_page
              << ",\"mode\":\"current_production_differential\",\"homes\":" << homes_checked
              << ",\"payload_bytes\":" << group->reserved_bytes()
              << ",\"payload_pages\":" << group->payload_pages()
              << ",\"geometry_control_bytes\":" << group->control_bytes()
              << ",\"qualification\":\"pass\"}\n";
    okay(group->retire(), "production group retirement failed");
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--sizes") {
      std::cout << "{\"Group\":" << sizeof(orbital::allocation::detail::Group)
                << ",\"FiniteGroup\":" << sizeof(orbital::allocation::detail::FiniteGroup)
                << "}\n";
      return 0;
    }
    const bool production = argc == 3 && std::string_view(argv[1]) == "--production";
    require(argc == 2 || production,
            "usage: allocation_palettes_native [--production] geometry.tsv");
    std::ifstream input(argv[production ? 2 : 1]);
    require(static_cast<bool>(input), "cannot open geometry TSV");
    const auto native = ::sysconf(_SC_PAGESIZE);
    require(native > 0 && std::has_single_bit(static_cast<std::size_t>(native)),
            "invalid host base page size");
    const auto geometries = read(input);
    if (production) {
      qualify_production(geometries, static_cast<std::size_t>(native));
      return 0;
    }
    for (const auto& [key, geometry] : geometries)
      qualify(key, geometry, static_cast<std::size_t>(native));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "allocation palette qualification: " << error.what() << '\n';
    return 1;
  }
}
