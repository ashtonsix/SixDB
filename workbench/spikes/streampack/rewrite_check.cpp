#include "rewrite.h"
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <thread>

namespace sm = streampack_material;
namespace sn = streampack_next;
namespace oa = orbital::allocation;
using Model = std::vector<std::optional<std::string>>;
void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital/material result"); return std::move(*r); }
sn::Bytes bytes(const std::string& s) { return std::as_bytes(std::span(s)); }
std::string value(unsigned global) {
  const auto key = global % 53;
  if (key == 7) return {};
  return std::string(4 + key % 21, char('A' + key % 26)) + char(key);
}
std::string get(sm::Selection s, unsigned row) {
  const auto x = sm::holders(s)[row]; const auto v = sm::read(s, sm::block_of(x)).value(sm::rank_of(x));
  return {reinterpret_cast<const char*>(v.data()), v.size()};
}
bool equal(sm::Selection s, const Model& expected) {
  if (sm::holders(s).size() != expected.size()) return false;
  sm::Summary summary;
  for (unsigned b = 0; b < s.control().blocks; ++b) if (sm::read(s, b).admit() != sn::Error::none) return false;
  for (unsigned row = 0; row < expected.size(); ++row) {
    const auto x = sm::holders(s)[row];
    if (!expected[row]) { if (x != sm::absent) return false; continue; }
    if (x == sm::absent || sm::block_of(x) >= s.control().blocks) return false;
    const auto v = sm::read(s, sm::block_of(x)); const auto r = sm::rank_of(x);
    if (r >= v.size() || !v.live(r) || get(s, row) != *expected[row]) return false;
    ++summary.live_rows; summary.used_bytes += expected[row]->size();
    for (unsigned char c : *expected[row]) { summary.minimum = std::min(summary.minimum, c); summary.maximum = std::max(summary.maximum, c); }
  }
  return summary == s.control().summary;
}
sm::Store::Owner capture(sm::Store& store, sm::Draft& d) { const auto sources = d.sources(); return take(store.capture(d.control, sources)); }
void publish(sm::Store& store, const sm::Store::Scope& basis, sm::Store::Owner owner) {
  const sm::Store::Change change{{23, 1}, std::move(owner)}; sm::Store::Prepared prepared(store);
  require(bool(store.prepare(basis, {&change, 1}, prepared)) && bool(store.install(prepared)), "publish completed material");
}
using sm::Rewrite;
void initial_plan_oracle(const Rewrite& rewrite, unsigned cap, bool traversal, bool dedup) {
  // Independent initial logical/physical model: no use of hard()'s source list,
  // geometry, hash table or holder reads to derive the expected canonical order.
  std::vector<std::uint32_t> expected_sources, expected_remap(cap * 2, sm::absent);
  std::vector<std::string> canonical_values;
  for (unsigned i = 0; i < cap * 2; ++i) {
    const auto global = traversal ? (i * 17) % (cap * 2) : i;
    if (global % 11 == 0) continue;
    const auto bytes = value(global);
    auto ordinal = canonical_values.size();
    if (dedup) {
      const auto found = std::find(canonical_values.begin(), canonical_values.end(), bytes);
      if (found != canonical_values.end()) ordinal = found - canonical_values.begin();
    }
    expected_remap[global] = sm::location(ordinal / cap, ordinal % cap);
    if (ordinal == canonical_values.size()) {
      canonical_values.push_back(bytes); expected_sources.push_back(sm::location(global / cap, global % cap));
    }
  }
  require(rewrite.sources == expected_sources, "independent stable/traversal/canonical source order");
  require(rewrite.remap == expected_remap, "independent complete partial/many-to-one remap");
}
void geometry(sm::Selection destination, sm::Selection source, const Rewrite& rewrite) {
  const auto cap = destination.control().capacity;
  for (unsigned b = 0; b < destination.control().blocks; ++b) {
    const auto out = sm::read(destination, b); unsigned offset = 0;
    const unsigned first = b * cap, count = std::min<unsigned>(cap, rewrite.sources.size() > first ? rewrite.sources.size() - first : 0);
    require(out.size() == count, "hard rank count");
    for (unsigned r = 0; r < count; ++r) {
      const auto x = rewrite.sources[first + r]; const auto input = sm::read(source, sm::block_of(x)); const auto rank = sm::rank_of(x);
      require(out.start(r) == offset && out.reserved(r) == input.reserved(rank) && out.length(r) == input.length(rank) && out.live(r), "hard geometry and live order");
      for (unsigned j = 0; j < input.reserved(rank); ++j) {
        const auto owner = out.reverse(offset + j); require(owner && owner->rank == r && !owner->tombstone, "hard reverse reservation owner");
      }
      offset += input.reserved(rank);
    }
    require(out.extent() == offset && !out.reverse(offset), "hard body extent");
  }
}

void history(oa::Pool& pool, unsigned cap, bool traversal, bool dedup) {
  auto& memory = *std::pmr::new_delete_resource(); sm::Store store(memory);
  sm::Control control{&memory, 0, std::uint16_t(cap), std::uint16_t(cap * 2), {}}; control.blocks = 2;
  auto initial = take(sm::Draft::create(pool, control, std::array<std::size_t, 2>{cap * 40, cap * 40}));
  ikea::no_coverage ignored;
  for (unsigned b = 0; b < 2; ++b) {
    auto w = initial.write(b);
    for (unsigned r = 0; r < cap; ++r) {
      const auto v = value(b * cap + r); require(w.append(bytes(v), v.size() + 8, ignored) == sn::Error::none, "initial two-block append");
      if ((b * cap + r) % 11 == 0) require(w.retire(r, ignored) == sn::Error::none, "initial unreferenced tombstone");
    }
  }
  Model model(cap * 2); auto h = initial.holder_view();
  for (unsigned row = 0; row < cap * 2; ++row) {
    const unsigned global = (row * 17) % (cap * 2);
    if (global % 11) { h.rows[row] = sm::location(global / cap, global % cap); model[row] = value(global); }
  }
  initial.finish(); { sm::Store::Scope basis(store); publish(store, basis, capture(store, initial)); }
  sm::Store::Scope old(store); const auto original = old.bind({23, 1}); const Model old_model = model;
  auto bound = take(store.bound(old, {23, 1})); auto* escaped = take(bound.export_part(0, orbital::PartRetention::selected_copy));
  std::vector<std::byte> escaped_bytes(sm::read(original).body.begin(), sm::read(original).body.end());
  auto rebuilt = take(sm::hard(pool, original, traversal, dedup));
  initial_plan_oracle(rebuilt, cap, traversal, dedup);
  auto owner = capture(store, rebuilt.draft);
  require(equal(owner.selection(), model), "hard exact logical values/presence/multiplicity"); geometry(owner.selection(), original, rebuilt);
  unsigned crossed = 0;
  for (unsigned row = 0; row < model.size(); ++row) if (model[row])
    crossed += sm::block_of(sm::holders(original)[row]) != sm::block_of(sm::holders(owner.selection())[row]);
  require(crossed != 0, "values genuinely cross block boundaries");
  if (dedup) {
    require(sm::read(owner.selection()).size() + sm::read(owner.selection(), 1).size() < cap, "cross-block duplicates coalesced");
    // Same-sized but wrong holder references can preserve cheap summaries.
    auto corrupt = take(sm::Draft::clone(pool, owner.selection(), std::array<std::size_t, 2>{cap * 40, cap * 40}));
    auto rows = corrupt.holder_view(); unsigned a = 1, b = 2;
    while (!model[a]) ++a; while (!model[b] || model[b] == model[a]) ++b;
    std::swap(rows.rows[a], rows.rows[b]); corrupt.finish(); auto bad = capture(store, corrupt);
    require(!equal(bad.selection(), model), "wrong holder repair fails row identity oracle");
  }
  auto conversion = take(store.conversion(old, {23, 1})); require(bool(store.adopt(conversion, owner)), "adopt hard rewrite");
  auto rewritten = take(old.bind_alternative({23, 1})); require(rewritten.realization == owner.get(), "continue exact adopted physical source");
  require(equal(old.bind({23, 1}), old_model), "primary selection remains intact after adoption");
  // Retain this alternative through the next logical publication.
  const auto rewritten_before = sm::Store::Owner::retain(rewritten.realization);
  auto candidate = take(sm::Draft::clone(pool, rewritten, std::array<std::size_t, 2>{cap * 40, cap * 40}));
  std::array operations{candidate.write(0), candidate.write(1)}; auto holders = candidate.holder_view();
  std::array<ikea::owner_write, 128> records{}; ikea::source_write_journal effects{records};
  unsigned edited = 1; while (!model[edited]) ++edited;
  const auto previous = holders.rows[edited]; unsigned aliases = 0;
  for (auto x : holders.rows.first(candidate.control.holders)) aliases += x == previous;
  if (dedup) require(aliases > 1, "selected edit has shared old reservation");
  // Always exercise append/retarget here, including when it would fit. Shared
  // edits require detach; unique in-reservation writes have separate coverage.
  const std::string replacement = "detached";
  const unsigned block = operations[1].count < cap ? 1 : 0, rank = operations[block].count;
  require(operations[block].append(bytes(replacement), 24, effects) == sn::Error::none, "continued append after hard rewrite");
  effects.before(holders, {0, edited * 4, 4}); holders.rows[edited] = sm::location(block, rank); model[edited] = replacement;
  if (aliases == 1) require(operations[sm::block_of(previous)].retire(sm::rank_of(previous), effects) == sn::Error::none, "last holder retirement");
  if (dedup) {
    unsigned remaining = aliases - 1;
    for (unsigned row = 0; row < model.size(); ++row) if (holders.rows[row] == previous) {
      require(operations[sm::block_of(previous)].read().live(sm::rank_of(previous)), "shared reservation stays live before last removal");
      effects.before(holders, {0, row * 4, 4}); holders.rows[row] = sm::absent; model[row].reset();
      if (--remaining == 0) require(operations[sm::block_of(previous)].retire(sm::rank_of(previous), effects) == sn::Error::none, "retire exactly after last shared holder");
    }
    require(!remaining && !operations[sm::block_of(previous)].read().live(sm::rank_of(previous)), "all aliases removed and old slot retired");
  }
  // One batch writes both bodies. A bulk update deliberately changes every
  // holder of this reservation; this is distinct from detaching a single alias.
  unsigned first = 0;
  while (!operations[0].read().live(first) || !operations[0].read().length(first)) ++first;
  const auto first_span = operations[0].read().value(first);
  std::string bulk(reinterpret_cast<const char*>(first_span.data()), first_span.size()); bulk[0] = '!';
  require(operations[0].replace(first, bytes(bulk), effects) == sn::Error::none, "same batch changes block zero body");
  for (unsigned row = 0; row < model.size(); ++row) if (holders.rows[row] == sm::location(0, first)) model[row] = bulk;
  {
    auto wrong = std::vector<ikea::owner_write>(effects.entries().begin(), effects.entries().end());
    const auto copied_source = operations[1]; bool changed_source = false;
    for (auto& e : wrong) if (e.source == &operations[1]) { e.source = &copied_source; changed_source = true; break; }
    require(changed_source && !sm::freeze_damage(rewritten, candidate, operations, holders, wrong), "equal-shaped but unknown block source identity refuses");
  }
  auto damage = take(sm::freeze_damage(rewritten, candidate, operations, holders, effects.entries())); candidate = {};
  require(std::any_of(damage.patches.begin(), damage.patches.end(), [](const auto& p) { return p.part == 0; }) &&
      std::any_of(damage.patches.begin(), damage.patches.end(), [](const auto& p) { return p.part == 2; }), "mixed source body routing exercised");
  {
    auto omitted = damage;
    std::erase_if(omitted.patches, [](const auto& p) { return p.part == 2; });
    require(!sm::reconstruct(pool, rewritten, omitted), "missing block one new body afterimage refuses");
    omitted = damage; std::erase_if(omitted.patches, [](const auto& p) { return p.part == 0; });
    auto bad = sm::reconstruct(pool, rewritten, omitted);
    if (bad) { auto broken = capture(store, *bad); require(!equal(broken.selection(), model), "missing block zero payload fails exact oracle"); }
  }
  auto reconstructed = take(sm::reconstruct(pool, rewritten, damage)); auto changed = capture(store, reconstructed);
  require(equal(changed.selection(), model), "two-block damage consumes body+geometry+holder edits");
  publish(store, old, std::move(changed));
  sm::Store::Scope current(store); require(equal(current.bind({23, 1}), model), "new primary uses rewritten layout and actual edit");
  require(equal(rewritten_before.selection(), old_model) && equal(original, old_model), "both retained physical interpretations unchanged");
  require(std::equal(escaped_bytes.begin(), escaped_bytes.end(), orbital::view(escaped).address()), "selected body survives hard rewrite and edit");
  // A second rewrite starts from the result of useful work, not the original.
  auto again = take(sm::hard(pool, current.bind({23, 1}), true, true)); auto second = capture(store, again.draft);
  require(equal(second.selection(), model), "second hard rewrite after continued mutation");
  auto next_conversion = take(store.conversion(current, {23, 1})); require(bool(store.adopt(next_conversion, second)), "second hard adoption");
  auto smaller = take(sm::hard(pool, second.selection(), true, true, true)); auto compact = capture(store, smaller.draft);
  require(compact.selection().control().blocks == 1 && compact.selection().control().holders == cap * 2 &&
      equal(compact.selection(), model), "two-to-one block repartition retains complete logical population");
  geometry(compact.selection(), second.selection(), smaller);
  require(bool(store.adopt(next_conversion, compact)), "adopt one-block physical replacement");
  const auto one = take(current.bind_alternative({23, 1}));
  auto last = take(sm::Draft::clone(pool, one, cap * 40)); auto one_write = last.write(); auto many_holders = last.holder_view();
  unsigned high_row = cap; while (!model[high_row]) ++high_row;
  const auto previous_high = many_holders.rows[high_row]; const auto new_rank = one_write.count;
  const std::string high_value = "high logical row after shrinking";
  std::array<ikea::owner_write, 16> last_records{}; ikea::source_write_journal last_effects{last_records};
  require(one_write.append(bytes(high_value), 40, last_effects) == sn::Error::none, "continued append after two-to-one repartition");
  last_effects.before(many_holders, {0, high_row * 4, 4}); many_holders.rows[high_row] = sm::location(0, new_rank);
  if (std::find(many_holders.rows.begin(), many_holders.rows.end(), previous_high) == many_holders.rows.end())
    require(one_write.retire(sm::rank_of(previous_high), last_effects) == sn::Error::none, "last owner after topology change");
  const Model before_high = model; model[high_row] = high_value;
  auto one_damage = take(sm::freeze_damage(one, last, one_write, many_holders, last_effects.entries())); last = {};
  auto one_restored = take(sm::reconstruct(pool, one, one_damage)); auto final = capture(store, one_restored);
  require(equal(final.selection(), model), "one-block damage routes high logical holder to new holder Part");
  publish(store, current, std::move(final));
  sm::Store::Scope final_scope(store);
  require(equal(final_scope.bind({23, 1}), model) && equal(one, before_high) && equal(original, old_model),
      "continued publication preserves each old physical topology");
  orbital::release(escaped);
}
int main() {
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(32u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  for (unsigned cap : {128, 256, 512}) for (bool traversal : {false, true}) for (bool dedup : {false, true}) history(*pool, cap, traversal, dedup);
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "hard rewrite and retained reader settlement"); require(bool(oa::remove_pool(*pool)), "remove Pool");
  arena.reset(); orbital::worker::relinquish(lane);
  std::puts("StreamPack cross-block hard remap/dedup, old readers, continued damage publication and repeated rewrite passed");
}
