#include "backend.h"
#include <cstdio>
#include <cstring>
#include <optional>
#include <source_location>
#include <unistd.h>

namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace b = orbital_bench;
namespace cs = compact_shape;
using orbital::Error;
std::size_t row_checks{}, wire_checks{}, refusals{}, publications{};
template<class T> void check(const T& value, std::source_location at = std::source_location::current()) {
  if (bool(value)) return;
  std::fprintf(stderr, "prefix control check failed at %s:%u\n", at.file_name(), at.line()); std::abort();
}
const std::byte* address(c::Body body) { return p::need(body.selected.part(std::size_t{0})).address(); }
// Independent little-endian fixed Tuple encoding, including the spare nibble.
void verify(c::Body body, std::span<const b::Row> rows, const std::byte* original) {
  check(body.size() == rows.size() && address(body) == original);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    check(body.row_unchecked(i) == rows[i]); ++row_checks;
    const std::array<std::uint64_t, 4> fields{rows[i].key, rows[i].record.next, rows[i].record.value, rows[i].record.tag};
    constexpr std::array<unsigned, 4> widths{8, 8, 3, 4};
    std::size_t offset{};
    for (unsigned f = 0; f < widths.size(); ++f)
      for (unsigned j = 0; j < widths[f]; ++j) {
        check(std::to_integer<unsigned>(original[i * 23 + offset++]) == ((fields[f] >> (j * 8)) & 0xff)); ++wire_checks;
      }
  }
  check(!body.material->generic(body.selected).row(rows.size()));
}
b::Row next(std::span<const b::Row> rows, unsigned salt = 0) {
  return {rows.back().key + 19, {rows.front().key, std::uint32_t((rows.size() * 137 + salt) & b::value_mask),
      std::uint32_t(0x89210000u + rows.size() + salt)}};
}
int main() {
  const auto native_page = ::sysconf(_SC_PAGESIZE); check(native_page > 0);
  const auto page = std::size_t(native_page), initial = page / 23;
  p::Corpus corpus(initial, 1, 8132);
  { bool refused{}; try { c::PrefixGrowth invalid(corpus, 1024 * 1024, initial * 23 - 1); }
    catch (const std::invalid_argument&) { refused = true; } check(refused); ++refusals; }
  c::PrefixGrowth store(corpus, 1024 * 1024, 3 * page);
  const auto original_cut = store.cut();
  std::optional<c::PrefixGrowth::Scope> original(std::in_place, store.catalog());
  std::optional<c::PrefixGrowth::Scope> rotating(std::in_place, store.catalog());
  std::vector<b::Row> expected = corpus.images[0].rows;
  const auto* base = address(store.body(*original, 0));
  const auto initial_accounting = store.accounting();
  check(initial_accounting.active_allocations == 1 && initial_accounting.pool.allocations == 1);
  const auto refuse = [&](std::span<const b::Row> input, c::v::ConstructionOptions options,
      Error error, std::optional<std::size_t> metadata_headroom = {}) {
    const auto cut = store.cut(), count = expected.size();
    const auto before = store.accounting();
    if (metadata_headroom) store.metadata_limit(before.metadata_live + *metadata_headroom);
    const auto status = store.append(input, options);
    store.metadata_limit(before.metadata_limit);
    check(!status && status.error() == error && store.cut() == cut);
    auto current = store.read(); verify(store.body(current, 0), expected, base);
    verify(store.body(*original, 0), std::span(expected).first(initial), base);
    check(store.body(current, 0).size() == count && store.accounting().active_allocations == 1);
    const auto calls = store.accounting().metadata_calls - before.metadata_calls;
    if (metadata_headroom) check(calls == (*metadata_headroom ? 1u : 0u));
    ++refusals;
  };
  std::vector<b::Row> large;
  auto oracle = expected;
  for (unsigned i = 0; i < 70; ++i) { large.push_back(next(oracle)); oracle.push_back(large.back()); }
  refuse(large, {.fail_after_rows = 1}, Error::rejected);
  check(store.growth_calls() == 1 && store.accounting().pool.growths == 1);
  // Reuse a SMALLER suffix after all failed speculative work has ended. No
  // shrink/grow occurs; change the failed first row so stale data cannot pass.
  auto row = next(expected, 77);
  p::need(store.append({&row, 1})); expected.push_back(row); ++publications;
  check(store.growth_calls() == 1 && store.accounting().pool.growths == 1);
  row = next(expected, 91);
  refuse({&row, 1}, {}, Error::capacity, 0); // capture allocation refuses
  const auto repr = cs::footprint(store.catalog().borrow().bind(corpus.images[0].address.object).realization).requested_bytes;
  refuse({&row, 1}, {}, Error::capacity, repr); // capture succeeds, prepare refuses
  row = next(expected, 117); // retry overwrites speculative bytes after capture owner died
  p::need(store.append({&row, 1})); expected.push_back(row); ++publications;
  std::size_t rotated_count = initial;
  for (unsigned i = 0; i < 130; ++i) {
    row = next(expected, i); p::need(store.append({&row, 1})); expected.push_back(row); ++publications;
    const bool rotate = i < 3 || (i + 1) % 64 == 0;
    if (rotate) {
      verify(store.body(*rotating, 0), std::span(expected).first(rotated_count), base);
      rotating.reset(); rotating.emplace(store.catalog()); rotated_count = expected.size(); store.drain(); store.poll(128);
    }
  }
  // Original Scope discovers its old body late, after many publications.
  verify(store.body(*original, 0), std::span(expected).first(initial), base);
  { auto current = store.read(); verify(store.body(current, 0), expected, base);
    check(!store.body(*original, 0).get(expected.back().key)); }
  check(store.cut() == original_cut + publications);
  oracle = expected; large.clear();
  while ((oracle.size() + 1) * 23 <= 3 * page + 23) { large.push_back(next(oracle)); oracle.push_back(large.back()); }
  const auto grows = store.growth_calls(), capacity = store.accounting().live_capacity_bytes;
  refuse(large, {}, Error::conflict); // explicit reservation refuses before grow/write
  check(store.growth_calls() == grows && store.accounting().live_capacity_bytes == capacity);
  original.reset(); rotating.reset(); store.drain(); store.poll(65536);
  const auto final = store.accounting();
  check(store.statistics().versions == 1 && store.statistics().scopes == 0 &&
      final.active_allocations == 1 && final.pool.allocations == 1 && final.pool.relocations == 0 &&
      final.pool.reserved_bytes == 3 * page && store.costs.copied_bytes == 0);
  std::printf("{\"passed\":true,\"timing_valid\":false,\"page_bytes\":%zu,\"rows\":%zu,\"publications\":%zu,\"refusals\":%zu,\"row_checks\":%zu,\"wire_byte_checks\":%zu,\"body_allocations\":%llu,\"growth_calls\":%llu,\"relocations\":%llu,\"prefix_copied_bytes\":%llu,\"shared_block_bytes\":%zu,\"representation_bytes\":%zu,\"backend_bytes\":%zu,\"metadata_live\":%zu,\"metadata_peak\":%zu,\"mapped_bytes\":%llu,\"reserved_bytes\":%llu}\n",
      page, expected.size(), publications, refusals, row_checks, wire_checks,
      (unsigned long long)final.pool.allocations, (unsigned long long)store.growth_calls(),
      (unsigned long long)final.pool.relocations, (unsigned long long)store.costs.copied_bytes,
      store.shared_owner_bytes(), repr, sizeof(store), final.metadata_live, final.metadata_peak,
      (unsigned long long)final.pool.mapped_bytes, (unsigned long long)final.pool.reserved_bytes);
}
