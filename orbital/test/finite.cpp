#include "finite.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <set>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using namespace orbital;
using namespace orbital::allocation::detail;

namespace {
void check(bool condition, const char* detail) {
  if (!condition) {
    std::fprintf(stderr, "finite: %s\n", detail);
    std::abort();
  }
}

template<class T> T take(Result<T> result, const char* detail) {
  check(bool(result), detail);
  return std::move(*result);
}

void okay(Result<void> result, const char* detail) { check(bool(result), detail); }

constexpr std::array<std::size_t, 15> capacities_a{
    16,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072};
constexpr std::array<std::size_t, 15> capacities_b{
    256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072};
constexpr std::array<std::size_t, 15> capacities_b_large{
    256,320,384,448,512,640,768,896,1088,1280,1536,1792,2176,2560,3072};

const auto& expected_b(std::size_t page) {
  return page >= 16384 ? capacities_b_large : capacities_b;
}

std::set<std::size_t> pages_of(const FiniteGroup& group, const std::vector<Home>& homes) {
  std::set<std::size_t> pages;
  const auto base = reinterpret_cast<std::uintptr_t>(group.page_address(0));
  const auto page_size = group.native_page();
  for (const auto& home : homes) {
    const auto begin = reinterpret_cast<std::uintptr_t>(home.address) - base;
    const auto end = begin + home.capacity;
    for (auto byte = begin; byte < end;) {
      pages.insert(byte / page_size);
      byte = (byte / page_size + 1) * page_size;
    }
  }
  return pages;
}

void check_live(const FiniteGroup& group, const std::vector<Home>& homes) {
  const auto expected = pages_of(group, homes);
  std::vector<std::uint64_t> live(group.live_words(), ~std::uint64_t{0});
  okay(group.live_pages(live), "live page union");
  for (std::size_t page = 0; page < group.payload_pages(); ++page)
    check(bool(live[page / 64] & (std::uint64_t{1} << (page % 64))) == expected.contains(page),
          "union disagrees with independent byte intervals");
  std::size_t count = 0;
  for (const auto word : live)
    count += std::popcount(word);
  check(count == expected.size(), "unused final-word bits are clear");
}

void prepare(FiniteGroup& group, const Home& home) {
  check(::mprotect(group.page_address(home.pages.first), home.pages.count * group.native_page(),
                   PROT_READ | PROT_WRITE) == 0, "prepare native mapping");
  okay(group.mark_mapped(home.pages), "publish prepared mapping");
}

void geometry(Palette palette, std::uint16_t width) {
  auto group = take(FiniteGroup::create({palette, width}), "create geometry");
  check(bool(group->prepare()), "prepare geometry");
  check(group->native_page() == static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)),
        "uses native page granule");
  check(group->control_bytes() >= width && group->control_bytes() % alignof(std::uint64_t) == 0,
        "heap control extent includes bindings and complete aligned scratch");
  check(group->reserved_bytes() == group->payload_pages() * group->native_page(),
        "payload reservation excludes separately charged control heap");
  const auto& capacities = palette == Palette::a ? capacities_a : expected_b(group->native_page());
  std::vector<Identity> identities;
  for (unsigned index = 0; index < width; ++index) {
    auto identity = take(group->claim_identity(), "claim all identities");
    check(identity.index == index, "dense identities");
    identities.push_back(identity);
    check(group->resolve(identity) == nullptr, "unbound identity has no native pointer");
    if (index)
      check(group->descriptor(identity) == group->descriptor(identities.front()) + index,
            "one-byte dense trampoline plane");
  }
  check(!group->claim_identity(), "full identity pressure");
  std::vector<Home> all;
  bool straddler = false;
  for (unsigned grid = 0; grid < width / 16; ++grid) {
    for (unsigned cls = 0; cls < 15; ++cls) {
      std::byte* first = nullptr;
      std::uint16_t seen = 0;
      for (unsigned slot = 0; slot < 16; ++slot) {
        const auto home = take(group->claim_home(identities[grid * 16], capacities[cls], 16),
                               "claim each possible home");
        check(home.grid == grid && home.cls == cls && home.slot < 16 && !(seen & (1u << home.slot)),
              "exact unique finite coordinate");
        seen |= static_cast<std::uint16_t>(1u << home.slot);
        check(home.capacity == capacities[cls], "palette capacity");
        check(reinterpret_cast<std::uintptr_t>(home.address) % std::gcd(capacities[cls], std::size_t{64}) == 0,
              "class reservation lost natural alignment");
        if (!slot)
          first = home.address - home.slot * capacities[cls];
        check(home.address == first + home.slot * capacities[cls], "same-class homes are contiguous");
        const auto raw = reinterpret_cast<std::uintptr_t>(home.address);
        const auto expected_count = ((raw % group->native_page()) + home.capacity +
                                     group->native_page() - 1) / group->native_page();
        check(home.pages.count == expected_count, "complete capacity coverage includes straddlers");
        straddler |= home.pages.count > 1;
        all.push_back(home);
      }
      check(seen == 0xffff, "every selected-class coordinate is available");
      check(!group->claim_home(identities[grid * 16], capacities[cls], 16), "full selected-class pressure");
    }
  }
  check(straddler, "actual palette contains native page straddlers");
  auto ordered = all;
  std::sort(ordered.begin(), ordered.end(), [](const Home& left, const Home& right) {
    return left.address < right.address;
  });
  for (std::size_t index = 1; index < ordered.size(); ++index)
    check(ordered[index - 1].address + ordered[index - 1].capacity <= ordered[index].address,
          "all possible homes are physically disjoint");
  check_live(*group, all);
  for (const auto identity : identities)
    okay(group->release_identity(identity), "release identities before homes");
  check_live(*group, all);
  for (const auto& home : all)
    okay(group->release_home(home), "release all homes");
  check_live(*group, {});
}

void check_bound(const FiniteGroup& group, Identity identity, const Home& expected) {
  const auto actual = take(group.bound_home(identity), "read current bound home");
  check(actual.grid == expected.grid && actual.cls == expected.cls && actual.slot == expected.slot &&
      actual.address == expected.address && actual.capacity == expected.capacity &&
      actual.pages.first == expected.pages.first && actual.pages.count == expected.pages.count,
      "binding lost a physical coordinate or complete backing extent");
  check(group.bound_capacity(identity) == expected.capacity, "bound class capacity");
}

void lifecycle() {
  auto group = take(FiniteGroup::create(), "create lifecycle");
  check(bool(group->prepare()), "prepare lifecycle");
  const auto identity = take(group->claim_identity(), "claim identity");
  const auto first = take(group->claim_home(identity, 35, 16), "claim old home");
  check(!group->bound_home(identity) && group->bound_capacity(identity) == 0,
        "pending home is not a current binding");
  check(!group->bind(identity, first), "cannot bind before root prepares mapping");
  prepare(*group, first);
  okay(group->bind(identity, first), "bind old home");
  check_bound(*group, identity, first);
  check(group->resolve(identity) == first.address, "arithmetic resolution of old home");
  const auto second_identity = take(group->claim_identity(), "second identity");
  check(!group->bind(second_identity, first), "one physical home cannot be bound to two identities");
  okay(group->release_identity(second_identity), "release second identity");
  std::fill_n(first.address, first.capacity, std::byte{0x6d});
  check(!group->release_home(first), "current home cannot become reusable");
  const auto replacement = take(group->claim_home(identity, 80, 16), "claim replacement");
  prepare(*group, replacement);
  check_bound(*group, identity, first);
  okay(group->bind(identity, replacement), "rebind same identity");
  check_bound(*group, identity, replacement);
  check(group->resolve(identity) == replacement.address, "new resolution uses new home");
  for (std::size_t byte = 0; byte < first.capacity; ++byte)
    check(first.address[byte] == std::byte{0x6d}, "rebind preserves captured old bytes");
  check_live(*group, {first, replacement});
  check(group->mapped_pages() == pages_of(*group, {first, replacement}).size(),
        "shared backing pages counted once");
  okay(group->release_identity(identity), "release identity while old homes remain");
  check(!group->bound_home(identity) && group->bound_capacity(identity) == 0,
        "cleared binding cannot report retained old physical homes");
  check(!group->bound_home({static_cast<std::uint16_t>(group->identity_count())}) &&
      group->bound_capacity({static_cast<std::uint16_t>(group->identity_count())}) == 0,
      "bound lookup rejects an identity outside its plane");
  const auto reused = take(group->claim_identity(), "reuse identity independently");
  check(reused == identity && group->resolve(reused) == nullptr, "identity reuse clears old binding");
  const auto third = take(group->claim_home(reused, 35, 16), "claim after identity reuse");
  check(third.address != first.address, "retained old home remains occupied");
  check_live(*group, {first, replacement, third});
  check(!group->begin_trim(first.pages), "retained and pending homes prevent discard");
  okay(group->release_home(first), "old-home physical completion");
  okay(group->release_home(replacement), "replacement physical completion");
  okay(group->release_home(third), "pending-home cancellation before use");
  check_live(*group, {});
  okay(group->release_identity(reused), "release never-bound identity");
}

void trim() {
  auto group = take(FiniteGroup::create({Palette::a, 64}), "create trim");
  check(bool(group->prepare()), "prepare trim");
  auto identity = take(group->claim_identity(), "trim identity");
  std::vector<Home> homes;
  for (unsigned slot = 0; slot < 16; ++slot)
    homes.push_back(take(group->claim_home(identity, 16, 16), "fill small class"));
  check(homes.front().pages.first == homes.back().pages.first, "small class shares one page");
  prepare(*group, homes.front());
  check(group->mapped_pages() == 1, "one backing charge for shared small homes");
  okay(group->release_identity(identity), "trim identity no longer bound");
  for (const auto& home : homes)
    okay(group->release_home(home), "retire small homes");
  const auto page = homes.front().pages;
  okay(group->begin_trim(page), "reserve idle page for trim");
  check(group->trimming(page.first) && group->mapped_pages() == 1,
        "pending trim has not returned backing charge");
  identity = take(group->claim_identity(), "new identity while trim pending");
  check(!group->claim_home(identity, 16, 16), "trim excludes all homes sharing page");
  check(!group->mark_mapped(page), "mapping cannot race pending discard");
  check(!group->finish_trim({page.first, page.count + 1}, true),
        "partial invalid trim completion leaves state unchanged");
  okay(group->finish_trim(page, false), "failed trim restores mapped capacity");
  check(group->mapped_pages() == 1 && !group->trimming(page.first), "failed trim retains charge");
  auto home = take(group->claim_home(identity, 16, 16), "failed trim enables reuse");
  okay(group->release_home(home), "release never-bound home");
  okay(group->begin_trim(page), "retry trim");
  check(::mprotect(group->page_address(page.first), group->native_page(), PROT_NONE) == 0,
        "root revokes mapping");
  check(::madvise(group->page_address(page.first), group->native_page(), MADV_DONTNEED) == 0,
        "root discards actual empty backing");
  okay(group->finish_trim(page, true), "successful discard publishes absence");
  check(group->mapped_pages() == 0, "backing charge returns after completion");
  home = take(group->claim_home(identity, 16, 16), "reclaim absent home");
  check(!group->bind(identity, home), "discarded page requires new preparation");
  prepare(*group, home);
  okay(group->bind(identity, home), "bind remapped home");
  check(group->resolve(identity) == home.address, "remapping preserves reservation geometry");
  check(home.address[0] == std::byte{0}, "discard/remap supplies anonymous zero backing");
  okay(group->release_identity(identity), "release remapped identity");
  okay(group->release_home(home), "release remapped home");
}

void grid_selection() {
  auto group = take(FiniteGroup::create({Palette::b, 64}), "create grid selection");
  check(bool(group->prepare()), "prepare grid selection");
  check(!group->claim_identity(~std::uint64_t{0}), "full exclusion mask rejects identity claim");
  auto next_grid = take(group->claim_identity(1), "exclude first grid");
  check(next_grid.index == 16, "exclusion picks next eligible grid");
  okay(group->release_identity(next_grid), "release excluded-grid selection");
  auto identity = take(group->claim_identity(), "ordinary selection after exclusion");
  check(identity.index == 0, "exclusion did not mutate skipped availability");
  okay(group->release_identity(identity), "release ordinary selection");

  std::vector<Identity> identities;
  std::vector<Home> homes;
  for (unsigned index = 0; index < 8; ++index) {
    identity = take(group->claim_identity(), "aligned first-grid identity");
    const auto home = take(group->claim_home(identity, 320, 128), "aligned first-grid home");
    check(identity.index / 16 == 0 && home.grid == 0, "first cohort uses first grid");
    check(reinterpret_cast<std::uintptr_t>(home.address) % 128 == 0, "320-byte aligned home");
    identities.push_back(identity);
    homes.push_back(home);
  }
  // 320 B strides alternate residues 0/64 modulo 128, so one grid has
  // exactly eight aligned homes even though eight identities remain free.
  const auto first_address = reinterpret_cast<std::uintptr_t>(homes.front().address);
  unsigned legal = 0;
  for (unsigned slot = 0; slot < 16; ++slot)
    legal += (first_address + slot * 320) % 128 == 0;
  check(legal == homes.size(), "independent native-address alignment capacity");
  const auto refused = take(group->claim_identity(), "identity available despite class pressure");
  check(!group->claim_home(refused, 320, 128), "selected grid lacks aligned physical home");
  okay(group->release_identity(refused), "rollback failed home claim");
  next_grid = take(group->claim_identity(std::uint64_t{1} << (refused.index / 16)),
                   "retry other grid after aligned-class pressure");
  const auto next_home = take(group->claim_home(next_grid, 320, 128), "reuse same group's idle grid");
  check(next_home.grid == 1 && reinterpret_cast<std::uintptr_t>(next_home.address) % 128 == 0,
        "alignment pressure does not require a fresh group");
  identities.push_back(next_grid);
  homes.push_back(next_home);
  check_live(*group, homes);
  for (const auto selected : identities)
    okay(group->release_identity(selected), "release grid-selection identity");
  for (const auto& home : homes)
    okay(group->release_home(home), "release grid-selection home");
}

void alignment_and_validation() {
  check(!FiniteGroup::create({Palette::a, 48}), "reject unsupported group width");
  check(!FiniteGroup::create({static_cast<Palette>(42), 256}), "reject unknown palette");
  auto group = take(FiniteGroup::create({Palette::b, 64}), "create B alignment");
  check(bool(group->prepare()), "prepare B alignment");
  auto identity = take(group->claim_identity(), "B identity");
  for (const auto capacity : expected_b(group->native_page())) {
    const auto home = take(group->claim_home(identity, capacity, 64), "B 64B alignment");
    check(home.capacity == capacity && reinterpret_cast<std::uintptr_t>(home.address) % 64 == 0,
        "every selected-profile B home has its expected capacity/alignment");
    okay(group->release_home(home), "release B home");
  }
  check(!group->claim_home(identity, 0, 16), "reject empty home");
  check(!group->claim_home(identity, 3073, 16), "virtual class belongs outside finite group");
  check(!group->claim_home(identity, 64, 3), "reject non-power-of-two alignment");
  check(!group->claim_home(identity, 64, std::numeric_limits<std::size_t>::max()), "reject overflow alignment");
  check(group->descriptor({64}) == nullptr && group->resolve({64}) == nullptr,
        "reject identity outside dense plane");
  auto original = take(group->claim_home(identity, 256, 64), "home for malformed handle");
  auto malformed = original;
  ++malformed.capacity;
  check(!group->release_home(malformed), "reject forged home capacity");
  malformed = original;
  ++malformed.pages.count;
  check(!group->release_home(malformed), "reject forged home coverage");
  okay(group->release_home(original), "valid home remains after rejected releases");
  check(!group->mark_mapped({group->payload_pages(), 1}), "reject out-of-range page");
  check(!group->mark_mapped({0, std::numeric_limits<std::size_t>::max()}), "reject overflow span");
  std::vector<std::uint64_t> short_live(group->live_words() - 1);
  check(!group->live_pages(short_live), "reject insufficient live-page scratch");
  okay(group->release_identity(identity), "release B identity");
  check(!group->release_identity(identity), "double identity release fails");
  okay(group->retire(), "explicit fallible whole-group retirement");
  okay(group->retire(), "retirement is idempotent after full success");
}

void prepared_home_selection() {
  auto group = take(FiniteGroup::create({Palette::b, 64}), "create prepared-home selection");
  okay(group->prepare(), "prepare selection geometry");
  auto identity = take(group->claim_identity(), "selection identity");
  std::vector<Home> homes;
  for (unsigned index = 0; index < 16; ++index)
    homes.push_back(take(group->claim_home(identity, 1024, 64), "enumerate physical choices"));
  std::sort(homes.begin(), homes.end(), [](const auto& left, const auto& right) {
    return left.address < right.address;
  });
  prepare(*group, homes.back());
  const auto page = group->native_page();
  const auto capacity = page >= 16384 ? 1088u : 1024u;
  check(homes.back().capacity == capacity, "selected profile has the wrong 1024-byte reservation class");
  const auto mapped_begin = reinterpret_cast<std::uintptr_t>(homes.back().address) / page * page;
  const auto mapped_end = (reinterpret_cast<std::uintptr_t>(homes.back().address) + capacity + page - 1) / page * page;
  for (const auto& home : homes) okay(group->release_home(home), "return enumerated home");
  const auto expected = std::find_if(homes.begin(), homes.end(), [&](const auto& home) {
    const auto address = reinterpret_cast<std::uintptr_t>(home.address);
    return address >= mapped_begin && address + capacity <= mapped_end;
  });
  check(expected != homes.end(), "independent prepared byte interval has a legal home");
  auto selected = take(group->claim_home(identity, 1024, 64), "reuse existing prepared pages");
  check(selected.address == expected->address, "unprepared earlier slot hid prepared same-grid capacity");
  selected.address[0] = selected.address[capacity-1] = std::byte{0x37};
  okay(group->release_home(selected), "return prepared selected home");
  okay(group->begin_trim(selected.pages), "exclude prepared pages during reclamation");
  auto alternative = group->claim_home(identity, 1024, 64);
  if (alternative) {
    const auto address = reinterpret_cast<std::uintptr_t>(alternative->address);
    check(address + capacity <= mapped_begin || address >= mapped_end,
          "prepared preference claimed an in-flight trim range");
    okay(group->release_home(*alternative), "return non-trimming alternative");
  }
  okay(group->finish_trim(selected.pages, false), "failed trim restores prepared selection");
  selected = take(group->claim_home(identity, 1024, 64), "reuse after failed trim");
  check(selected.address == expected->address && selected.address[0] == std::byte{0x37} &&
      selected.address[capacity-1] == std::byte{0x37}, "failed trim lost prepared selection or actual contents");
  okay(group->release_home(selected), "release restored home");
  okay(group->release_identity(identity), "release selection identity");
}

void bitmap_trim_admission() {
  for (const auto palette : {Palette::a, Palette::b}) {
    for (const auto width : {64, 256, 1024}) {
      auto group = take(FiniteGroup::create({palette, static_cast<std::uint16_t>(width)}),
                        "create bitmap trim geometry");
      okay(group->prepare(), "prepare bitmap trim geometry");
      auto membership = group->trim_membership();
      check(!group->has_trim(), "fresh trim membership is empty");
      const auto page = group->native_page();
      check(::mprotect(group->page_address(0), 3 * page, PROT_READ | PROT_WRITE) == 0,
            "prepare three actual idle pages");
      okay(group->mark_mapped({0, 3}), "publish bitmap trim pages");
      group->page_address(0)[0] = std::byte{0x71};
      group->page_address(2)[page - 1] = std::byte{0x93};
      const auto unchanged = [&] {
        check(group->mapped_pages() == 3, "rejected trim keeps all mapped charge");
        for (std::size_t p = 0; p < 3; ++p)
          check(group->mapped(p) && !group->trimming(p),
                "rejected bitmap partially quarantined earlier valid pages");
        check(group->page_address(0)[0] == std::byte{0x71} &&
            group->page_address(2)[page - 1] == std::byte{0x93},
            "rejected bitmap changed readable idle contents");
      };

      membership[0] = 0xdu; // Valid pages 0/2 followed by absent page 3.
      const auto absent = group->begin_trim();
      check(!absent && absent.error() == Error::conflict, "absent selected page refuses complete bitmap");
      unchanged();
      membership[0] = 0x5u;
      if (const auto tail = group->payload_pages() % 64; tail) {
        membership.back() |= std::uint64_t{1} << tail;
        const auto beyond = group->begin_trim();
        check(!beyond && beyond.error() == Error::invalid_argument,
              "unused bitmap tail cannot select a page outside geometry");
        unchanged();
        membership.back() &= ~(std::uint64_t{1} << tail);
      }
      const std::array<PageSpan, 3> overflow{{
          {0, 1}, {1, std::numeric_limits<std::size_t>::max()}, {2, 1}}};
      check(!group->begin_trim(overflow), "wide overflow count is not narrowed to a legal range");
      unchanged();
      const std::array<PageSpan, 3> invalid_first{{
          {0, 1}, {std::numeric_limits<std::size_t>::max(), 1}, {2, 1}}};
      check(!group->begin_trim(invalid_first), "wide first page outside geometry refuses complete selection");
      unchanged();

      membership[0] = 0x7u;
      okay(group->begin_trim(), "reserve complete bitmap selection");
      for (std::size_t p = 0; p < 3; ++p)
        check(group->trimming(p), "valid bitmap quarantines every selected page");
      okay(group->finish_trim({0, 1}, false), "failed first discard restores mapping");
      check(::madvise(group->page_address(1), page, MADV_DONTNEED) == 0 &&
          ::mprotect(group->page_address(1), page, PROT_NONE) == 0,
          "discard actual middle page");
      okay(group->finish_trim({1, 1}, true), "successful middle discard refunds only its range");
      okay(group->finish_trim({2, 1}, false), "failed last discard restores mapping");
      membership[0] = 0;
      check(group->mapped_pages() == 2 && !group->mapped(1) &&
          group->page_address(0)[0] == std::byte{0x71} &&
          group->page_address(2)[page - 1] == std::byte{0x93},
          "independent completions lost mapping charge or surviving bytes");

      const auto last = group->payload_pages() - 1;
      check(::mprotect(group->page_address(last), page, PROT_READ | PROT_WRITE) == 0,
            "prepare last legal payload page");
      okay(group->mark_mapped({last, 1}), "publish last legal payload page");
      group->page_address(last)[page - 1] = std::byte{0xb5};
      membership[last / 64] = std::uint64_t{1} << (last % 64);
      okay(group->begin_trim(), "bitmap accepts last legal payload page");
      check(group->trimming(last), "bitmap endpoint is quarantined");
      okay(group->finish_trim({last, 1}, false), "restore bitmap endpoint after discard refusal");
      membership[last / 64] = 0;
      check(group->mapped_pages() == 3 && group->mapped(last) &&
          group->page_address(last)[page - 1] == std::byte{0xb5},
          "bitmap endpoint preserves bytes and charge");
    }
  }
}

void bitmap_trim_cross_word() {
  auto group = take(FiniteGroup::create({Palette::a, 1024}), "create cross-word trim geometry");
  okay(group->prepare(), "prepare cross-word trim geometry");
  check(group->payload_pages() > 67, "geometry has both neighbours of the cross-word run");
  const auto page = group->native_page();
  check(::mprotect(group->page_address(61), 7 * page, PROT_READ | PROT_WRITE) == 0,
        "prepare cross-word run and both neighbour pages");
  okay(group->mark_mapped({61, 7}), "publish cross-word run and neighbours");
  group->page_address(61)[page - 1] = std::byte{0xa1};
  group->page_address(67)[0] = std::byte{0xb2};
  const auto identity = take(group->claim_identity(~(std::uint64_t{1} << 63)),
                             "select last-grid active identity");
  const auto active = take(group->claim_home(identity, 3072, 16), "retain late active home");
  check(active.pages.first > 67, "active home follows the cross-word selection");
  prepare(*group, active);
  active.address[0] = active.address[active.capacity - 1] = std::byte{0xc3};
  const auto mapped = 7 + active.pages.count;
  auto membership = group->trim_membership();
  membership[0] = 0xc000000000000000ull;
  membership[1] = 0x7u;
  const auto unchanged = [&] {
    check(group->mapped_pages() == mapped, "refused cross-word trim preserves full charge");
    for (std::size_t p = 61; p <= 67; ++p)
      check(group->mapped(p) && !group->trimming(p), "refusal partially admitted the earlier run");
    for (auto p = active.pages.first; p < active.pages.first + active.pages.count; ++p)
      check(group->mapped(p) && !group->trimming(p), "refusal quarantined an active page");
    check(group->page_address(61)[page - 1] == std::byte{0xa1} &&
        group->page_address(67)[0] == std::byte{0xb2} && active.address[0] == std::byte{0xc3} &&
        active.address[active.capacity - 1] == std::byte{0xc3}, "refusal changed protected contents");
  };
  for (auto p = active.pages.first; p < active.pages.first + active.pages.count; ++p)
    membership[p / 64] |= std::uint64_t{1} << (p % 64);
  const auto occupied = group->begin_trim();
  check(!occupied && occupied.error() == Error::conflict, "late active page refuses complete selection");
  unchanged();
  std::fill(membership.begin(), membership.end(), 0);
  membership[0] = 0xc000000000000000ull;
  membership[1] = 0x7u;
  if (const auto tail = group->payload_pages() % 64; tail) {
    membership.back() |= std::uint64_t{1} << tail;
    const auto beyond = group->begin_trim();
    check(!beyond && beyond.error() == Error::invalid_argument, "late tail bit refuses cross-word selection");
    unchanged();
    membership.back() &= ~(std::uint64_t{1} << tail);
  }
  std::size_t cursor = 0;
  PageSpan span;
  check(next_trim_span(membership, cursor, span) && span.first == 62 && span.count == 5,
        "literal bitmap coalesces exactly the five pages [62,67)");
  check(!next_trim_span(membership, cursor, span), "cross-word selection contains only one run");
  okay(group->begin_trim(), "admit complete cross-word run");
  for (std::size_t p = 62; p < 67; ++p)
    check(group->trimming(p), "cross-word admission missed a selected page");
  check(group->mapped(61) && group->mapped(67), "cross-word admission reserved a neighbour");
  check(::madvise(group->page_address(62), 5 * page, MADV_DONTNEED) == 0 &&
      ::mprotect(group->page_address(62), 5 * page, PROT_NONE) == 0, "discard the actual complete cross-word run");
  auto success = group->trim_success();
  set_trim_span(success, {62, 5}, true);
  check(success[0] == 0xc000000000000000ull && success[1] == 0x7u &&
      std::all_of(success.begin() + 2, success.end(), [](auto word) { return word == 0; }),
      "successful run mask includes exactly [62,67)");
  okay(group->finish_trim({62, 5}, true), "publish cross-word discard");
  set_trim_span(membership, {62, 5}, false);
  check(!group->has_trim() && group->mapped_pages() == mapped - 5,
        "cross-word completion clears membership and returns exactly five mapped pages");
  for (std::size_t p = 62; p < 67; ++p)
    check(!group->mapped(p) && !group->trimming(p), "successful cross-word page remained mapped or pending");
  check(group->page_address(61)[page - 1] == std::byte{0xa1} &&
      group->page_address(67)[0] == std::byte{0xb2} && active.address[0] == std::byte{0xc3} &&
      active.address[active.capacity - 1] == std::byte{0xc3}, "cross-word discard changed neighbour or active bytes");
  okay(group->release_home(active), "release active home after cross-word trim");
  okay(group->release_identity(identity), "release cross-word identity");
}
} // namespace

int main() {
  for (const auto palette : {Palette::a, Palette::b})
    for (const auto width : {64, 256, 1024})
      geometry(palette, static_cast<std::uint16_t>(width));
  lifecycle();
  trim();
  grid_selection();
  alignment_and_validation();
  prepared_home_selection();
  bitmap_trim_admission();
  bitmap_trim_cross_word();
  std::puts("finite: six native geometries, retained homes, binding, page unions and trim races passed");
}
