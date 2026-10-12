#include "backend/finite.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <numeric>
#include <set>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace orbital;
using namespace orbital::allocation::palette_experiment;

namespace {
bool observe_allocations{};
std::array<std::size_t, 4> observed_allocations{};
std::size_t observed_count{}, checked_homes{};

void check(bool value, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (value) return;
  std::fprintf(stderr, "palettes:%u: %s\n", where.line(), reason);
  std::abort();
}

template<class T> T take(Result<T> result, const char* reason) {
  check(bool(result), reason);
  return std::move(*result);
}

void done(Result<void> result, const char* reason) { check(bool(result), reason); }

std::size_t rounded(std::size_t value, std::size_t unit) {
  return ((value + unit - 1) / unit) * unit;
}

std::vector<std::size_t> table(unsigned classes, std::size_t maximum) {
  std::vector<std::size_t> result;
  for (unsigned i = 1; i <= classes; ++i)
    result.push_back(rounded(i * maximum / classes, 16));
  result.back() = maximum;
  return result;
}

std::byte pattern(const Home& home) {
  return std::byte((home.grid * 47 + home.cls * 19 + home.slot * 7 + 1) % 255 + 1);
}

std::set<std::size_t> byte_pages(const FiniteGroup& group, std::span<const Home> homes) {
  const auto base = reinterpret_cast<std::uintptr_t>(group.page_address(0));
  std::set<std::size_t> result;
  for (const auto& home : homes) {
    const auto first = reinterpret_cast<std::uintptr_t>(home.address) - base;
    const auto last = first + home.capacity - 1;
    for (auto page = first / group.native_page(); page <= last / group.native_page(); ++page)
      result.insert(page);
  }
  return result;
}

void check_live(const FiniteGroup& group, std::span<const Home> homes) {
  const auto expected = byte_pages(group, homes);
  std::vector<std::uint64_t> actual(group.live_words(), ~std::uint64_t{0});
  done(group.live_pages(actual), "enumerate live pages");
  for (std::size_t page = 0; page < actual.size() * 64; ++page)
    check(bool(actual[page / 64] & (std::uint64_t{1} << (page % 64))) == expected.contains(page),
        "page inventory disagrees with independent byte intervals");
}

void map(FiniteGroup& group, const Home& home) {
  for (auto page = home.pages.first; page < home.pages.first + home.pages.count; ++page) {
    if (group.mapped(page)) continue;
    check(::mprotect(group.page_address(page), group.native_page(), PROT_READ | PROT_WRITE) == 0,
        "make Home page accessible");
    done(group.mark_mapped({page, 1}), "publish accessible page");
  }
}

void discard_idle(FiniteGroup& group, std::span<const Home> retained) {
  const auto live = byte_pages(group, retained);
  for (std::size_t page = 0; page < group.payload_pages(); ++page) {
    if (!group.mapped(page)) continue;
    if (live.contains(page)) {
      const auto refused = group.begin_trim({page, 1});
      check(!refused && refused.error() == Error::conflict, "retained Home admitted to trim");
      continue;
    }
    done(group.begin_trim({page, 1}), "reserve idle page");
    check(::madvise(group.page_address(page), group.native_page(), MADV_DONTNEED) == 0 &&
        ::mprotect(group.page_address(page), group.native_page(), PROT_NONE) == 0,
        "discard idle page");
    done(group.finish_trim({page, 1}, true), "complete physical discard");
  }
  check(group.mapped_pages() == live.size(), "trim retained/refunded the wrong pages");
}

void geometry(std::size_t page, unsigned bits, unsigned classes, std::uint16_t width, bool packed = false) {
  const auto maximum = page / 4 * 3;
  const auto capacities = packed ? std::vector<std::size_t>{
      16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072}
      : table(classes, maximum);
  auto supplied = capacities;
  observe_allocations = true;
  observed_count = 0;
  auto group = take(FiniteGroup::create({Palette::a, width, supplied,
      static_cast<std::uint8_t>(bits), page, packed}), "create custom palette");
  std::fill(supplied.begin(), supplied.end(), 0);
  done(group->prepare(), "prepare custom palette");
  observe_allocations = false;
  const auto slots = 1u << (8 - bits);
  check(group->slots_per_grid() == slots && group->class_count() == classes &&
      group->native_page() == page && group->host_page() == std::size_t(::sysconf(_SC_PAGESIZE)),
      "configured geometry or actual host page was lost");
  const auto grid_bytes = (width / slots) * (1 + classes * 2) * (slots / 8);
  const auto heap_bytes = rounded(width + grid_bytes +
      group->payload_pages(), 8) + 3 * 8 * ((group->payload_pages() + 63) / 64);
  if (group->control_bytes() != heap_bytes || observed_count != 2 ||
      observed_allocations[0] != sizeof(FiniteGroup) || observed_allocations[1] != heap_bytes)
    std::fprintf(stderr, "control expected=%zu actual=%zu count=%zu observed=%zu,%zu group=%zu\n",
        heap_bytes, group->control_bytes(), observed_count, observed_allocations[0], observed_allocations[1],
        sizeof(FiniteGroup));
  check(group->control_bytes() == heap_bytes && observed_count == 2 &&
      observed_allocations[0] == sizeof(FiniteGroup) && observed_allocations[1] == heap_bytes,
      "reported control charge differs from actual requested heap blocks");
  check(group->reserved_bytes() == group->payload_pages() * page +
      (page > group->host_page() ? page : 0), "reservation omits policy alignment headroom");
  check(reinterpret_cast<std::uintptr_t>(group->page_address(0)) % page == 0,
      "payload base is not policy-page aligned");

  std::vector<Identity> identities;
  for (unsigned i = 0; i < width; ++i) {
    identities.push_back(take(group->claim_identity(), "claim dense identity"));
    check(identities.back().index == i && group->grid_of(identities.back()) == i / slots,
        "identity grid arithmetic is wrong");
    check(*group->descriptor(identities.back()) == 0xff && !group->resolve(identities.back()),
        "fresh identity has a live binding");
  }
  check(!group->has_identity() && !group->claim_identity(), "full identity inventory is available");

  std::vector<Home> homes;
  const auto base = reinterpret_cast<std::uintptr_t>(group->page_address(0));
  for (unsigned grid = 0; grid < width / slots; ++grid) {
    for (unsigned cls = 0; cls < classes; ++cls) {
      check(group->capacity(cls) == capacities[cls], "geometry borrowed the caller's mutable table");
      const auto legal = slots - (classes == (1u << bits) && cls == classes - 1);
      unsigned seen = 0;
      for (unsigned i = 0; i < legal; ++i) {
        const auto alignment = std::gcd(capacities[cls], std::size_t{64});
        const auto home = take(group->claim_home(identities[grid * slots], capacities[cls], alignment),
            "claim each legal physical coordinate");
        check(home.grid == grid && home.cls == cls && home.slot < slots &&
            !(seen & (1u << home.slot)) && ((home.slot << bits) | home.cls) != 0xff,
            "coordinate duplicated, escaped grid or used unbound sentinel");
        seen |= 1u << home.slot;
        const auto start = reinterpret_cast<std::uintptr_t>(home.address);
        check(start % alignment == 0 && home.capacity == capacities[cls], "Home alignment/capacity wrong");
        check(home.pages.first == (start - base) / page && home.pages.count ==
            (start + home.capacity - 1 - base) / page - (start - base) / page + 1,
            "Home page coverage differs from byte addresses");
        map(*group, home);
        std::fill_n(home.address, home.capacity, pattern(home));
        homes.push_back(home);
        ++checked_homes;
      }
      check(seen == ((1u << legal) - 1), "some legal coordinate was lost");
      const auto exhausted = group->claim_home(identities[grid * slots], capacities[cls], 16);
      check(!exhausted && exhausted.error() == Error::capacity, "full physical class is available");
    }
  }
  auto ordered = homes;
  if (packed) {
    std::vector<Home> tiny;
    for (const auto& home : homes) if (home.capacity <= 96) tiny.push_back(home);
    check(byte_pages(*group, tiny).size() == rounded(width * 256, page) / page,
        "packed tiny-class aperture grew unnecessary policy pages");
  }
  std::sort(ordered.begin(), ordered.end(), [](const Home& a, const Home& b) { return a.address < b.address; });
  for (std::size_t i = 1; i < ordered.size(); ++i)
    check(ordered[i - 1].address + ordered[i - 1].capacity <= ordered[i].address,
        "physical Homes overlap");
  for (const auto& home : homes) {
    check(std::all_of(home.address, home.address + home.capacity,
        [&](auto byte) { return byte == pattern(home); }), "another Home corrupted this Home's bytes");
    const auto identity = identities[home.grid * slots];
    done(group->bind(identity, home), "publish physical coordinate");
    check(*group->descriptor(identity) == ((home.slot << bits) | home.cls) &&
        group->resolve(identity) == home.address && group->bound_capacity(identity) == home.capacity &&
        take(group->bound_home(identity), "read bound Home").slot == home.slot,
        "one-byte binding decode disagrees with claimed Home");
    check(!group->bind(identities[home.grid * slots + 1], home), "second identity acquired the same Home");
    check(!group->release_home(home), "bound physical Home was released");
  }
  for (const auto identity : identities) {
    done(group->release_identity(identity), "release all identities before retained Homes");
    check(!group->resolve(identity) && group->bound_capacity(identity) == 0,
        "released identity still resolves");
  }
  check_live(*group, homes);
  std::vector<Home> retained;
  for (std::size_t i = 0; i < homes.size(); ++i) {
    if (i == 0 || i == homes.size() / 2 || i + 1 == homes.size()) retained.push_back(homes[i]);
    else done(group->release_home(homes[i]), "release unretained Home");
  }
  check_live(*group, retained);
  discard_idle(*group, retained);
  for (const auto& home : retained) {
    check(std::all_of(home.address, home.address + home.capacity,
        [&](auto byte) { return byte == pattern(home); }),
        "trim damaged bytes whose identity had already retired");
    done(group->release_home(home), "release retained Home");
  }
  check_live(*group, {});
  done(group->retire(), "retire geometry");
  check(group->reserved_bytes() == 0 && group->mapped_pages() == 0, "retirement left charged mappings");
}

void alignment_retry(std::size_t page, unsigned bits) {
  const std::array<std::size_t, 2> capacities{320, page / 4 * 3};
  auto group = take(FiniteGroup::create({Palette::b, 256, capacities,
      static_cast<std::uint8_t>(bits), page}), "create alignment pressure geometry");
  done(group->prepare(), "prepare alignment pressure geometry");
  const auto slots = unsigned(group->slots_per_grid());
  std::vector<Identity> identities;
  std::vector<Home> homes;
  for (unsigned i = 0; i < slots / 2; ++i) {
    identities.push_back(take(group->claim_identity(), "claim first-grid aligned identity"));
    homes.push_back(take(group->claim_home(identities.back(), 320, 128), "claim aligned Home"));
    check(reinterpret_cast<std::uintptr_t>(homes.back().address) % 128 == 0,
        "explicit alignment128 violated");
  }
  auto blocked = take(group->claim_identity(), "grid retains an identity under Home pressure");
  const auto unavailable = group->claim_home(blocked, 320, 128);
  check(!unavailable && unavailable.error() == Error::capacity, "unaligned Home admitted");
  done(group->release_identity(blocked), "undo blocked candidate identity");
  const auto next = take(group->claim_identity(std::uint64_t{1} << group->grid_of(blocked)),
      "retry another grid");
  check(next.index == slots, "retry skipped or returned the wrong physical grid");
  const auto home = take(group->claim_home(next, 320, 128), "later grid has legal aligned Home");
  check(home.grid == 1 && reinterpret_cast<std::uintptr_t>(home.address) % 128 == 0,
      "later-grid phase destroyed alignment");
  done(group->release_home(home), "release later-grid Home");
  done(group->release_identity(next), "release later-grid identity");
  for (const auto identity : identities) done(group->release_identity(identity), "release first-grid identity");
  for (const auto& value : homes) done(group->release_home(value), "release aligned Home");
  for (const auto grid : {0u, 1u, 7u, 256u / slots - 1}) {
    const auto identity = take(group->claim_identity(~(std::uint64_t{1} << grid)), "select alignment grid");
    for (const auto capacity : capacities) {
      std::vector<Home> choices;
      for (unsigned slot = 0; slot < slots; ++slot)
        choices.push_back(take(group->claim_home(identity, capacity, 16), "enumerate native choices"));
      for (const auto& choice : choices) done(group->release_home(choice), "return enumerated choice");
      for (const auto alignment : {std::size_t{64}, std::size_t{128}, std::size_t{2048}, page}) {
        unsigned expected = 0, actual = 0;
        for (const auto& choice : choices)
          if (reinterpret_cast<std::uintptr_t>(choice.address) % alignment == 0)
            expected |= 1u << choice.slot;
        std::vector<Home> claimed;
        for (;;) {
          const auto choice = group->claim_home(identity, capacity, alignment);
          if (!choice) {
            check(choice.error() == Error::capacity, "legal alignment was rejected as malformed");
            break;
          }
          check(!(actual & (1u << choice->slot)), "aligned coordinate was returned twice");
          actual |= 1u << choice->slot;
          claimed.push_back(*choice);
        }
        check(actual == expected, "alignment mask differs from actual address residues");
        for (const auto& choice : claimed) done(group->release_home(choice), "return aligned choice");
      }
    }
    done(group->release_identity(identity), "release selected alignment grid");
  }
}

void pending(std::size_t page, unsigned bits) {
  const std::array<std::size_t, 1> capacities{page / 4 * 3};
  auto group = take(FiniteGroup::create({Palette::b, 64, capacities,
      static_cast<std::uint8_t>(bits), page}), "create pending geometry");
  done(group->prepare(), "prepare pending geometry");
  const auto identity = take(group->claim_identity(), "pending identity");
  const auto home = take(group->claim_home(identity, capacities[0], 64), "pending Home");
  std::array<PageSpan, 2> spans;
  const auto count = take(group->begin_prepare(home, spans), "reserve asynchronous preparation");
  check(count == 1 && group->pending(home) && group->preparing_pages() == home.pages.count,
      "cold preparation did not retain exact Home/page inventory");
  check(!group->bind(identity, home) && !group->release_home(home), "pending Home was bound or reused");
  const auto other = take(group->claim_home(identity, capacities[0], 64), "find disjoint pending neighbor");
  check(other.address + other.capacity <= home.address || home.address + home.capacity <= other.address,
      "unfinished preparation reused physical bytes");
  done(group->release_home(other), "release pending neighbor");
  done(group->finish_prepare(home, std::span(spans).first(count), false), "retain failed preparation");
  check(group->pending(home) && group->preparing_pages() == home.pages.count && group->mapped_pages() == 0,
      "failed helper completion published/refunded unconfirmed pages");
  for (const auto span : std::span(spans).first(count))
    check(::mprotect(group->page_address(span.first), span.count * page, PROT_READ | PROT_WRITE) == 0,
        "helper makes promised spans accessible");
  done(group->finish_prepare(home, std::span(spans).first(count), true), "publish completed spans");
  done(group->bind(identity, home), "bind completed Home");
  home.address[0] = home.address[home.capacity - 1] = std::byte{0x6d};
  done(group->release_identity(identity), "identity retires while Home remains retained");
  check_live(*group, std::span(&home, 1));
  check(!group->begin_trim(home.pages), "retired identity hid retained physical Home");
  done(group->release_home(home), "retire prepared Home");
  discard_idle(*group, {});
}

void invalid(std::size_t native) {
  const auto rejected = [](FiniteConfig config) {
    const auto value = FiniteGroup::create(config);
    if (value || value.error() != Error::invalid_argument)
      std::fprintf(stderr, "invalid config: palette=%u IDs=%u bits=%u page=%zu classes=%zu first=%zu\n",
          unsigned(config.palette), config.identities, config.class_bits, config.page_granule,
          config.capacities.size(), config.capacities.empty() ? 0 : config.capacities.front());
    check(!value && value.error() == Error::invalid_argument, "invalid palette geometry accepted");
  };
  const std::array<std::size_t, 2> duplicate{32, 32}, descending{64, 32}, unaligned{32, 47};
  const std::array<std::size_t, 1> zero{0}, excessive{native + 16};
  for (const auto capacities : {std::span(duplicate), std::span(descending), std::span(unaligned)})
    rejected({Palette::a, 256, capacities});
  rejected({Palette::a, 256, zero});
  rejected({Palette::a, 256, excessive});
  rejected({Palette::a, 48});
  rejected({Palette::a, 1024, {}, 5});
  rejected({Palette::a, 256, {}, 3});
  rejected({Palette::a, 256, {}, 4, native + 1});
  if (native > 1) rejected({Palette::a, 256, {}, 4, native / 2});
  rejected({static_cast<Palette>(42), 256});
  const auto too_many = table(17, native / 4 * 3);
  rejected({Palette::b, 256, too_many, 4});
  const auto unpackable = table(15, native / 4 * 3);
  rejected({Palette::a, 256, unpackable, 4, native, true});
  const std::array<std::size_t, 1> overflow{65536};
  rejected({Palette::a, 256, overflow, 5, std::max(native, std::size_t{65536})});
  const std::array<std::size_t, 5> wraps_offset{16, 32, 48, 64, 80};
  check(!FiniteGroup::create({Palette::b, 256, wraps_offset, 4, std::size_t{1} << 30}),
      "page-aligned class offset can wrap uint32 and alias a preceding Home range");
}

} // namespace

extern "C" void* __real__ZnwmRKSt9nothrow_t(std::size_t, const std::nothrow_t&) noexcept;
extern "C" void* __real__ZnwmSt11align_val_tRKSt9nothrow_t(
    std::size_t, std::align_val_t, const std::nothrow_t&) noexcept;
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(std::size_t bytes, const std::nothrow_t& tag) noexcept {
  if (observe_allocations) {
    check(observed_count < observed_allocations.size(), "unexpected nothrow metadata allocation");
    observed_allocations[observed_count++] = bytes;
  }
  return __real__ZnwmRKSt9nothrow_t(bytes, tag);
}

extern "C" void* __wrap__ZnwmSt11align_val_tRKSt9nothrow_t(
    std::size_t bytes, std::align_val_t alignment, const std::nothrow_t& tag) noexcept {
  if (observe_allocations) {
    check(observed_count < observed_allocations.size(), "unexpected aligned geometry allocation");
    observed_allocations[observed_count++] = bytes;
  }
  return __real__ZnwmSt11align_val_tRKSt9nothrow_t(bytes, alignment, tag);
}

int main() {
  const auto host = std::size_t(::sysconf(_SC_PAGESIZE));
  invalid(host);
  for (const auto page : std::set{host, std::size_t{16384}, std::size_t{65536}}) {
    if (page < host) continue;
    std::printf("palette experiment: %zu-byte %s granule (host %zu)\n", page,
        page == host ? "native" : "emulated policy", host);
    geometry(page, 4, 15, 256);
    geometry(page, 4, 16, 256);
    geometry(page, 5, 15, 256);
    geometry(page, 5, 31, 256);
    geometry(page, 5, 32, 256);
    geometry(page, 4, 15, 256, true);
    geometry(page, 5, 15, 256, true);
    for (const auto bits : {4u, 5u}) {
      alignment_retry(page, bits);
      pending(page, bits);
    }
  }
  geometry(host, 4, 15, 1024);
  std::printf("palette experiment: %zu legal Homes, bindings, retained/pending custody and alignment checks passed\n",
      checked_homes);
}
