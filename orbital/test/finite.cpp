#include "finite_qualification.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
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

// Independent class oracle: the first listed capacity at or above both the
// request and the page floor whose stride preserves the requested alignment.
unsigned expected_class(std::size_t page, std::size_t bytes, std::size_t alignment) {
  constexpr std::array<std::size_t, 26> capacities{
      16,32,48,64,80,96,112,128,160,192,224,256,320,384,448,512,640,768,896,1024,1280,1536,1792,2048,2560,3072};
  if (alignment > page) return capacities.size();
  for (unsigned cls = 0; cls < capacities.size(); ++cls)
    if (capacities[cls] >= bytes && capacities[cls] * 256 >= page && capacities[cls] % alignment == 0)
      return cls;
  return capacities.size();
}

void selection() {
  for (std::size_t page : {4096, 16384, 65536})
    for (std::size_t alignment = 1; alignment <= 8192; alignment *= 2)
      for (std::size_t bytes = 1; bytes <= 3100; ++bytes)
        check(select_class(page, bytes, alignment) == expected_class(page, bytes, alignment),
              "class selection disagrees with the capacity/alignment/page-floor oracle");
  check(finite_capacities[first_class(4096)] == 16 && finite_capacities[first_class(16384)] == 64 &&
        finite_capacities[first_class(65536)] == 256,
        "page floor does not fill one native page with 256 Homes");
}

std::set<std::size_t> covered(const FiniteGroup& group, std::size_t home) {
  std::set<std::size_t> pages;
  const auto page = group.native_page();
  const auto begin = static_cast<std::size_t>(group.address(home) - group.page_address(0));
  for (auto byte = begin; byte < begin + group.capacity(); byte = (byte / page + 1) * page)
    pages.insert(byte / page);
  return pages;
}

void map(FiniteGroup& group, PageSpan span) {
  check(::mprotect(group.page_address(span.first), span.count * group.native_page(),
        PROT_READ | PROT_WRITE) == 0, "protect payload pages");
  okay(group.mark_mapped(span), "publish mapped pages");
}

void geometry(std::size_t page) {
  const auto host = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  if (page < host) return;
  for (unsigned cls = 0; cls < finite_classes; ++cls) {
    auto group = take(FiniteQualification::create(cls, page), "create class geometry");
    okay(group->prepare(), "prepare class geometry");
    const auto capacity = group->capacity();
    check(group->cls() == cls && capacity == finite_capacities[cls], "geometry lost its class");
    check(group->payload_pages() == (256 * capacity + page - 1) / page, "payload covers exactly 256 Homes");
    check(reinterpret_cast<std::uintptr_t>(group->page_address(0)) % page == 0, "payload is page aligned");
    for (std::size_t home = 0; home < finite_homes; ++home) {
      check(group->address(home) == group->page_address(0) + home * capacity,
            "Home address is base + index × capacity");
      const auto span = group->pages(home);
      const auto pages = covered(*group, home);
      check(span.count == pages.size() && span.first == *pages.begin(), "Home page span disagrees with its bytes");
      check(reinterpret_cast<std::uintptr_t>(group->address(home)) % (capacity & -capacity) == 0,
            "Home loses its capacity's natural alignment");
    }
    check(group->has_free() && !group->has_ready() && group->free_count() == 256 &&
          group->take_ready() == finite_homes, "fresh Homes are free but none is ready without pages");
    okay(group->retire(), "retire class geometry");
  }
  check(!FiniteGroup::create(finite_classes) && !FiniteQualification::create(0, host + 1) &&
        !FiniteQualification::create(0, std::size_t{1} << 30), "invalid class or granule accepted");
}

void lifecycle() {
  auto group = take(FiniteGroup::create(3), "create 64-byte geometry");
  okay(group->prepare(), "prepare lifecycle geometry");
  const auto per_page = std::min(group->native_page() / 64, finite_homes);
  check(group->first_unready() == 0, "lowest free Home needs pages first");
  map(*group, {0, 1});
  std::vector<std::size_t> taken;
  while (group->has_ready()) taken.push_back(group->take_ready());
  check(taken.size() == per_page, "ready set is exactly the Homes on mapped pages");
  for (std::size_t i = 0; i < taken.size(); ++i) check(taken[i] == i, "ready Homes are taken lowest first");
  if (per_page < finite_homes)
    check(group->first_unready() == per_page && group->has_free(), "next free Home is on the unmapped page");
  std::vector<std::uint64_t> live(group->live_words());
  okay(group->live_pages(live), "count live pages");
  check(live[0] == 1, "bound Homes keep their page live");
  group->give(5); group->give(2);
  check(group->take_ready() == 2 && group->take_ready() == 5, "returned Homes are ready again, lowest first");
  for (auto home : taken) group->give(home);
  okay(group->live_pages(live), "count live pages after release");
  check(live[0] == 0 && group->free_count() == 256, "released Homes leave no live page");
  okay(group->retire(), "retire lifecycle geometry");
}

void preparation() {
  auto group = take(FiniteGroup::create(finite_classes - 1), "create 3072-byte geometry");
  okay(group->prepare(), "prepare preparation geometry");
  const auto page = group->native_page();
  const auto home = group->pages(1).count == 2 ? std::size_t{1} : std::size_t{0};
  const auto span = group->pages(home);
  std::array<PageSpan, 2> spans{};
  const auto runs = take(group->begin_prepare(home, spans), "reserve Home");
  check(runs == 1 && spans[0].first == span.first && spans[0].count == span.count &&
        group->pending(home) && !group->is_free(home), "pending Home leaves the free set with its absent pages");
  for (auto p = span.first; p < span.first + span.count; ++p)
    check(group->preparing(p), "absent pages become preparing");
  const auto offered = group->first_unready();
  for (auto p : covered(*group, offered))
    check(!group->preparing(p), "a Home sharing a preparing page is offered for new mapping");
  check(!group->begin_prepare(home, spans), "pending Home cannot be reserved twice");
  std::vector<std::uint64_t> live(group->live_words());
  okay(group->live_pages(live), "count pending live pages");
  check(live[0] & 1, "pending Home's pages are live");
  // Confirm discard of the first page only; any second page stays uncertain.
  const PageSpan first{span.first, 1};
  okay(group->discard_preparation(home, std::span(&first, 1)), "confirm first-page discard");
  check(!group->preparing(span.first) && (span.count == 1 || group->preparing(span.first + 1)),
        "confirmed discard and uncertain page are distinguished");
  if (span.count == 2) {
    // Another Home on the confirmed page can be prepared and bound while the
    // original's second page is still uncertain.
    std::array<PageSpan, 2> other{};
    const auto reused = take(group->begin_prepare(0, other), "reuse the confirmed page");
    check(reused == 1 && other[0].first == span.first && other[0].count == 1, "reuse covers only the confirmed page");
    check(::mprotect(group->page_address(span.first), page, PROT_READ | PROT_WRITE) == 0, "map reused page");
    okay(group->finish_prepare(0, std::span(other).first(1), true), "finish reused Home");
    group->bind_prepared(0);
    check(group->preparing(span.first + 1) && group->mapped(span.first), "reuse disturbed the uncertain page");
    const PageSpan second{span.first + 1, 1};
    okay(group->discard_preparation(home, std::span(&second, 1)), "confirm second-page discard");
    check(group->mapped(span.first) && !group->preparing(span.first + 1), "late settlement cleared the reused page");
    group->give(0);
  }
  group->end_prepare(home);
  check(group->is_free(home) && !group->pending(home) && (span.count == 2 || !group->has_ready()),
        "ended preparation frees the Home unready");
  // Successful preparation: pages mapped, other covered free Homes become ready.
  const auto again = take(group->begin_prepare(home, spans), "reserve Home again");
  check(::mprotect(group->page_address(spans[0].first), spans[0].count * page, PROT_READ | PROT_WRITE) == 0,
        "map prepared pages");
  okay(group->finish_prepare(home, std::span(spans).first(again), true), "finish preparation");
  check(group->pending(home), "finished pages leave the Home pending until bound");
  group->bind_prepared(home);
  check(!group->pending(home) && !group->is_free(home), "prepared Home is bound");
  const auto neighbour = group->take_ready();
  check(neighbour != finite_homes && neighbour != home, "a neighbour on newly mapped pages is ready");
  group->give(neighbour); group->give(home);
  okay(group->retire(), "retire preparation geometry");
}

// A Home needing more absent pages must not hide one needing fewer.
void cheapest_selection() {
  auto group = take(FiniteGroup::create(finite_classes - 1), "create selection geometry");
  okay(group->prepare(), "prepare selection geometry");
  std::array<PageSpan, 2> spans{};
  (void)take(group->begin_prepare(0, spans), "occupy the first page with a preparation");
  std::size_t best = finite_homes, fewest = ~std::size_t{0};
  for (std::size_t home = 0; home < finite_homes; ++home) {
    if (!group->is_free(home)) continue;
    const auto span = group->pages(home);
    std::size_t missing = 0;
    bool usable = true;
    for (auto p = span.first; p < span.first + span.count; ++p) {
      usable = usable && !group->preparing(p) && !group->trimming(p);
      missing += !group->mapped(p);
    }
    if (usable && missing && missing < fewest) { best = home; fewest = missing; }
  }
  check(group->cheapest_unready() == best, "selection preferred a costlier unready Home");
  okay(group->discard_preparation(0, std::span(spans).first(1)), "discard preparation pages");
  group->end_prepare(0);
  okay(group->retire(), "retire selection geometry");
}

void trim_admission() {
  auto group = take(FiniteGroup::create(finite_classes - 1), "create trim geometry");
  okay(group->prepare(), "prepare trim geometry");
  check(group->payload_pages() >= 4, "geometry has the four pages the first fixture uses");
  const auto page = group->native_page();
  map(*group, {0, 3});
  group->page_address(0)[0] = std::byte{0x71};
  group->page_address(2)[page - 1] = std::byte{0x93};
  auto membership = group->trim_membership();
  check(!group->has_trim(), "fresh trim membership is empty");
  membership[0] = 0xdu; // Pages 0/2 plus absent page 3.
  const auto absent = group->begin_trim();
  check(!absent && absent.error() == Error::conflict && group->mapped_pages() == 3,
        "absent selected page refuses the complete bitmap");
  if (const auto tail = group->payload_pages() % 64; tail) {
    std::fill(membership.begin(), membership.end(), 0);
    membership.back() |= std::uint64_t{1} << tail;
    const auto beyond = group->begin_trim();
    check(!beyond && beyond.error() == Error::invalid_argument, "tail bit beyond geometry refused");
    membership.back() = 0;
  }
  const auto bound = group->take_ready();
  check(bound == 0, "lowest ready Home taken");
  membership[0] = 0x1u;
  const auto live = group->begin_trim();
  check(!live && live.error() == Error::conflict, "page with a bound Home refused");
  group->give(bound);
  membership[0] = 0x7u;
  okay(group->begin_trim(), "reserve idle pages");
  check(!group->has_ready(), "free Homes on trimming pages are not ready");
  for (auto p : covered(*group, group->first_unready()))
    check(!group->trimming(p), "trimming pages are offered for mapping");
  okay(group->finish_trim({0, 1}, false), "failed discard restores the page");
  check(::madvise(group->page_address(1), page, MADV_DONTNEED) == 0 &&
        ::mprotect(group->page_address(1), page, PROT_NONE) == 0, "discard middle page");
  okay(group->finish_trim({1, 1}, true), "confirmed discard refunds its page");
  okay(group->finish_trim({2, 1}, false), "failed discard restores the page");
  membership[0] = 0;
  check(group->mapped_pages() == 2 && !group->mapped(1) && group->page_address(0)[0] == std::byte{0x71} &&
        group->page_address(2)[page - 1] == std::byte{0x93}, "independent completions keep bytes and charge");
  check(group->has_ready(), "Homes wholly on restored pages are ready again");
  if (group->payload_pages() <= 68) { // 16/64 KiB pages: no Group spans a bitmap word.
    okay(group->retire(), "retire trim geometry");
    return;
  }
  // Cross-word run 61..67 next to a later live Home.
  map(*group, {61, 7});
  const auto late = 68 * page / group->capacity() + 1;
  map(*group, group->pages(late));
  std::vector<std::size_t> held;
  while (group->has_ready()) held.push_back(group->take_ready());
  for (auto home : held) if (home != late) group->give(home); // Only the late Home stays bound.
  for (std::size_t p = 61; p <= 67; ++p) membership[p / 64] |= std::uint64_t{1} << (p % 64);
  okay(group->begin_trim(), "cross-word run without live Homes is admitted");
  std::size_t cursor = 0; PageSpan span;
  check(next_trim_span(membership, cursor, span) && span.first == 61 && span.count == 7 &&
        !next_trim_span(membership, cursor, span), "cross-word run enumerates as one span");
  okay(group->finish_trim({61, 7}, false), "restore cross-word run");
  okay(group->retire(), "retire trim geometry");
}
} // namespace

int main() {
  selection();
  for (std::size_t page : {4096, 16384, 65536}) geometry(page);
  lifecycle();
  preparation();
  cheapest_selection();
  trim_admission();
  std::puts("finite: class selection, geometry, readiness, preparation and trim checks passed");
}
