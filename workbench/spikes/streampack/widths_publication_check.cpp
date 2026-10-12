#include "rewrite.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace sm = streampack_material;
namespace sn = streampack_next;
namespace oa = orbital::allocation;
using Model = std::vector<std::optional<std::string>>;
struct Slot { unsigned reservation; std::string value; bool dead{}; };
using Geometry = std::array<std::vector<Slot>, 2>;
void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital/material result"); return std::move(*r); }
sn::Bytes bytes(const std::string& s) { return std::as_bytes(std::span(s)); }
bool equal(sm::Selection selected, const Model& model) {
  const auto h = sm::holders(selected);
  if (h.size() != model.size() || selected.control().geometry != sm::Geometry::widths8) return false;
  sm::Summary summary;
  for (unsigned i = 0; i < h.size(); ++i) {
    if (!model[i]) { if (h[i] != sm::absent) return false; continue; }
    if (h[i] == sm::absent || sm::block_of(h[i]) >= selected.control().blocks) return false;
    const auto v = sm::read_widths(selected, sm::block_of(h[i])); const auto r = sm::rank_of(h[i]);
    if (r >= v.size() || !v.live(r)) return false;
    const auto value = v.value(r), expected = bytes(*model[i]);
    if (value.size() != expected.size() || !std::equal(value.begin(), value.end(), expected.begin())) return false;
    ++summary.live_rows; summary.used_bytes += expected.size();
    for (auto b : expected) { const auto x = std::to_integer<std::uint8_t>(b); summary.minimum = std::min(summary.minimum, x); summary.maximum = std::max(summary.maximum, x); }
  }
  return selected.control().summary == summary;
}
void geometry(sm::Selection s, const Geometry& model) {
  for (unsigned b = 0; b < s.control().blocks; ++b) {
    const auto v = sm::read_widths(s, b); require(v.admit(255) == sn::Error::none && v.size() == model[b].size(), "width material admission");
    unsigned end = 0;
    for (unsigned r = 0; r < model[b].size(); ++r) {
      const auto& slot = model[b][r];
      require(v.start(r) == end && v.reserved(r) == slot.reservation && v.length(r) == slot.value.size() && v.live(r) != slot.dead, "independent physical geometry");
      for (unsigned j = 0; j < slot.reservation; ++j) {
        const auto owner = v.reverse(end + j); require(owner && owner->rank == r && owner->tombstone == slot.dead, "all physical byte owners");
      }
      end += slot.reservation;
    }
    require(v.extent() == end && !v.reverse(end), "physical tail unowned");
  }
}
sm::Store::Owner capture(sm::Store& store, sm::Draft& d) { const auto parts = d.sources(); return take(store.capture(d.control, parts)); }
void publish(sm::Store& store, const sm::Store::Scope& basis, sm::Store::Owner owner) {
  sm::Store::Change change{{43, 1}, std::move(owner)}; sm::Store::Prepared packet(store);
  require(bool(store.prepare(basis, {&change, 1}, packet)) && bool(store.install(packet)), "publish coherent width material");
}
void holder(sm::HolderView& h, unsigned row, std::uint32_t location, ikea::source_write_journal& e) {
  require(e.remaining() != 0, "holder coverage capacity"); e.before(h, {0, row * 4, 4}); h.rows[row] = location;
}
void history(oa::Pool& pool, unsigned cap) {
  auto& memory = *std::pmr::new_delete_resource(); sm::Store store(memory);
  sm::Control c{&memory, 0, std::uint16_t(cap), 34, {}}; c.blocks = 2; c.geometry = sm::Geometry::widths8;
  {
    sm::Control direct{&memory, 0, std::uint16_t(cap), 0, {}};
    auto empty = take(sm::Draft::create(pool, direct, 0)); empty.finish(); auto predecessor = capture(store, empty);
    auto invalid = take(sm::Draft::clone(pool, predecessor.selection(), 0));
    invalid.control.geometry = sm::Geometry::widths8;
    auto w = invalid.write_widths(); auto h = invalid.holder_view();
    require(w.read().admit(255) == sn::Error::none, "otherwise valid empty recipe reinterpretation");
    require(!sm::freeze_damage(predecessor.selection(), invalid, w, h, {}), "recipe change on empty predecessor refuses freezing");
  }
  auto initial = take(sm::Draft::create(pool, c, std::array<std::size_t, 2>{cap * 32, cap * 32}));
  Model model; Geometry slots; ikea::no_coverage no;
  for (unsigned b = 0; b < 2; ++b) {
    auto w = initial.write_widths(b);
    for (unsigned r = 0; r < 17; ++r) {
      const std::string value(r % 5, char('a' + r % 4)); const auto width = (r % 4 + 1) * 8;
      require(w.append(bytes(value), width, no) == sn::Error::none, "initial width append");
      initial.holder_view().rows[model.size()] = sm::location(b, r); model.emplace_back(value); slots[b].push_back({width, value});
    }
  }
  initial.finish(); { sm::Store::Scope basis(store); publish(store, basis, capture(store, initial)); }
  sm::Store::Scope old(store), late(store); const auto original = model; const auto old_slots = slots;
  auto bound = take(store.bound(old, {43, 1})); auto* escaped = take(bound.export_part(1, orbital::PartRetention::selected_copy));
  const auto old_meta = orbital::view(escaped);
  const std::vector<std::byte> old_metadata(old_meta.address(), old_meta.address() + sm::Layout{cap, sm::Geometry::widths8}.bytes());
  for (unsigned step = 0; step < 3; ++step) {
    sm::Store::Scope basis(store); const auto prior = basis.bind({43, 1});
    auto d = take(sm::Draft::clone(pool, prior, std::array<std::size_t, 2>{cap * 32, cap * 32}));
    std::array writers{d.write_widths(0), d.write_widths(1)}; auto h = d.holder_view();
    std::array<ikea::owner_write, 4096> storage; ikea::source_write_journal e{storage};
    if (step == 0) {
      const std::string changed = "M";
      require(writers[0].replace(1, bytes(changed), e) == sn::Error::none && writers[1].retire(2, e) == sn::Error::none, "replace and retire across sources");
      model[1] = changed; slots[0][1].value = changed;
      model[19] = std::nullopt; slots[1][2].dead = true; holder(h, 19, sm::absent, e);
      for (unsigned b = 0; b < 2; ++b) {
        const std::string value = b ? "tail" : ""; const auto rank = writers[b].count;
        require(writers[b].append(bytes(value), b ? 31 : 0, e) == sn::Error::none, "append mixed empty and full");
        holder(h, d.control.holders++, sm::location(b, rank), e); model.emplace_back(value); slots[b].push_back({b ? 31u : 0u, value});
      }
    } else if (step == 1) {
      for (unsigned b = 0; b < 2; ++b) {
        require(sn::soft_pack(writers[b], true, e) == sn::Error::none, "soft width movement");
        for (auto& s : slots[b]) { if (s.dead) s.value.clear(); s.reservation = s.value.size(); }
      }
    } else {
      // Continue through the next group boundary after soft compaction.
      for (unsigned r = 18; r < 34; ++r) {
        const std::string value(r % 6, char('A' + r % 9));
        require(writers[0].append(bytes(value), 16, e) == sn::Error::none, "continued append into initialized padding");
        holder(h, d.control.holders++, sm::location(0, r), e); model.emplace_back(value); slots[0].push_back({16, value});
      }
    }
    auto malformed = writers; malformed[1].bases = malformed[1].bases.first(malformed[1].bases.size() - 1);
    require(!sm::freeze_damage(prior, d, malformed, h, {}), "wrong checkpoint source extent refuses");
    const auto valid_control = d.control;
    d.control.capacity = cap == 128 ? 256 : 128;
    require(!sm::freeze_damage(prior, d, writers, h, {}), "capacity reinterpretation refuses before rebinding"); d.control = valid_control;
    d.control.blocks = 1; d.control.second_body_extent = 0;
    require(!sm::freeze_damage(prior, d, std::span<const sn::MutableWidths<std::uint8_t>>(writers.data(), 1), h, {}), "topology reinterpretation refuses before rebinding"); d.control = valid_control;
    d.control.holder_capacity = d.control.holder_limit() - 1;
    require(!sm::freeze_damage(prior, d, writers, h, {}), "holder extent reinterpretation refuses before rebinding"); d.control = valid_control;
    const auto damage = take(sm::freeze_damage(prior, d, writers, h, e.entries()));
    d = {}; // frozen afterimages must survive destruction of all writable backing
    auto changed_recipe = damage; changed_recipe.target.geometry = sm::Geometry::direct32;
    require(!sm::reconstruct(pool, prior, changed_recipe), "recipe-changing byte reconstruction refuses");
    if (step == 0 || step == 1) {
      auto missing = damage; const sm::Layout layout{cap, sm::Geometry::widths8};
      std::erase_if(missing.patches, [&](const auto& p) { return p.part < 4 && p.part % 2 && p.offset >= layout.bases() && p.offset < layout.used(); });
      require(!sm::reconstruct(pool, prior, missing), "missing changed checkpoint afterimage refuses");
    }
    auto rebuilt = take(sm::reconstruct(pool, prior, damage)); auto ready = capture(store, rebuilt);
    require(equal(ready.selection(), model), "native width damage exact logical result"); geometry(ready.selection(), slots);
    publish(store, basis, std::move(ready));
    require(equal(old.bind({43, 1}), original), "retained old width interpretation"); geometry(old.bind({43, 1}), old_slots);
  }
  sm::Store::Scope basis(store); const auto before = basis.bind({43, 1});
  auto rewrite = take(sm::hard(pool, before, true, true, true)); auto compact = capture(store, rewrite.draft);
  require(compact.selection().control().blocks == 1 && equal(compact.selection(), model), "hard width dedup/repartition exact multiplicity");
  auto conversion = take(store.conversion(basis, {43, 1})); require(bool(store.adopt(conversion, compact)), "adopt native width rewrite");
  const auto one = take(basis.bind_alternative({43, 1}));
  auto last = take(sm::Draft::clone(pool, one, cap * 32)); auto w = last.write_widths(); auto h = last.holder_view();
  std::array<ikea::owner_write, 64> storage; ikea::source_write_journal e{storage};
  const auto new_rank = w.count; const std::string replacement = "detached after hard rewrite";
  require(w.append(bytes(replacement), 32, e) == sn::Error::none, "append after adopted width rewrite");
  // Old rank may have other aliases: detach only this holder, preserve others.
  holder(h, 0, new_rank, e); model[0] = replacement;
  auto damage = take(sm::freeze_damage(one, last, w, h, e.entries()));
  auto rebuilt = take(sm::reconstruct(pool, one, damage)); auto ready = capture(store, rebuilt);
  require(equal(ready.selection(), model), "continued damage after metadata topology change"); publish(store, basis, std::move(ready));
  require(equal(late.bind({43, 1}), original), "delayed old-width discovery");
  const auto kept = orbital::view(escaped);
  require(std::equal(old_metadata.begin(), old_metadata.end(), kept.address()), "escaped checkpoint metadata unchanged");
  orbital::release(escaped);
}
int main() {
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(32u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  for (unsigned cap : {128, 256, 512}) history(*pool, cap);
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "width material refund"); require(bool(oa::remove_pool(*pool)), "remove Pool");
  arena.reset(); orbital::worker::relinquish(lane);
  std::puts("Native width checkpoint damage, retained readers, rewrite and continued publication passed");
}
