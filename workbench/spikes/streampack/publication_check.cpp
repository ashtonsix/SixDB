#include "material.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace sm = streampack_material;
namespace sn = streampack_next;
namespace oa = orbital::allocation;
using Model = std::vector<std::optional<std::string>>;
struct PhysicalSlot { unsigned reservation, used; bool dead{}; };
void require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
template<class R> auto take(R r) { require(bool(r), "Orbital/material result"); return std::move(*r); }
sn::Bytes bytes(const std::string& value) { return std::as_bytes(std::span(value)); }
bool equal(sm::Selection selected, const Model& model) {
  auto rows = sm::holders(selected); auto view = sm::read(selected);
  if (rows.size() != model.size() || view.admit() != sn::Error::none) return false;
  sm::Summary expected;
  for (unsigned i = 0; i < rows.size(); ++i) {
    if (!model[i]) { if (rows[i] != sm::absent) return false; continue; }
    if (rows[i] >= view.size() || !view.live(rows[i])) return false;
    const auto value = view.value(rows[i]); const auto& wanted = *model[i];
    if (value.size() != wanted.size() || !std::equal(value.begin(), value.end(), bytes(wanted).begin())) return false;
    ++expected.live_rows; expected.used_bytes += wanted.size();
    for (unsigned char c : wanted) { expected.minimum = std::min(expected.minimum, c); expected.maximum = std::max(expected.maximum, c); }
  }
  return selected.control().summary == expected;
}
bool geometry_equal(sm::Selection selected, const std::vector<PhysicalSlot>& slots) {
  auto v = sm::read(selected);
  if (v.size() != slots.size() || v.admit() != sn::Error::none) return false;
  unsigned offset = 0;
  for (unsigned r = 0; r < slots.size(); ++r) {
    const auto& s = slots[r];
    if (v.start(r) != offset || v.reserved(r) != s.reservation || v.length(r) != s.used || v.live(r) == s.dead) return false;
    for (unsigned p = 0; p < s.reservation; ++p) {
      auto owner = v.reverse(offset + p);
      if (!owner || owner->rank != r || owner->tombstone != s.dead) return false;
    }
    offset += s.reservation;
  }
  return v.extent() == offset && !v.reverse(offset);
}
sm::Store::Owner capture(sm::Store& store, sm::Draft& d) {
  const auto parts = d.sources(); return take(store.capture(d.control, parts));
}
void publish(sm::Store& store, const sm::Store::Scope& basis, sm::Store::Owner owner,
    orbital::ObjectId object = {19, 1}) {
  const sm::Store::Change change{object, std::move(owner)};
  sm::Store::Prepared packet(store);
  require(bool(store.prepare(basis, {&change, 1}, packet)), "prepare");
  require(bool(store.install(packet)), "install");
}
void set_holder(sm::HolderView& h, unsigned row, unsigned rank, ikea::source_write_journal& effects) {
  require(effects.remaining() != 0, "holder effects preadmitted");
  effects.before(h, {0, row * 4, 4}); h.rows[row] = rank;
}

int main() {
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(32u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  const orbital::ObjectId id{19, 1};
  orbital::Part<sm::Control>* escaped{};
  orbital::Part<sm::Control>* copied_escape{};
  std::byte escaped_first{};
  {
    // Orbital's catalog metadata resource is its existing public integration
    // seam. All payload/sidecar backing uses Allocation directly.
    auto& memory = *std::pmr::new_delete_resource();
    sm::Store store(memory);
    require(!sm::reconstruct(*pool, {}, {}), "empty damage/basis refuses");
    sm::Control invalid{&memory, 0, 128, 0, {}}; invalid.selected = 1;
    require(!sm::Draft::create(*pool, invalid, 0), "selected Part is not a full writable material");
    auto initial = take(sm::Draft::create(*pool, {&memory, 0, 128, 3, {}}, 1024));
    auto w = initial.write(); auto holders = initial.holder_view(); ikea::no_coverage ignored;
    Model model{std::string("A"), std::string("bc"), std::string("tail")};
    std::vector<PhysicalSlot> geometry{{8, 1}, {2, 2}, {4, 4}};
    for (unsigned i = 0; i < model.size(); ++i) {
      require(w.append(bytes(*model[i]), i == 0 ? 8 : model[i]->size(), ignored) == sn::Error::none, "initial append");
      holders.rows[i] = i;
    }
    initial.finish();
    { sm::Store::Scope basis(store); publish(store, basis, capture(store, initial)); }
    {
      auto empty = take(sm::Draft::create(*pool, {&memory, 0, 128, 1, {}}, 0));
      auto ew = empty.write();
      require(ew.append({}, 0, ignored) == sn::Error::none, "empty-body slot construction");
      empty.holder_view().rows[0] = 0; empty.finish();
      require(!empty.parts[0], "empty body has no Allocation");
      auto owner = capture(store, empty);
      require(equal(owner.selection(), {std::string{}}), "wholly empty-body representation capture");
    }
    sm::Store::Scope old(store);
    sm::Store::Scope late(store); // first bind occurs after every edit below
    const Model original = model;
    auto escaped_binding = take(store.bound(old, id));
    escaped = take(escaped_binding.export_part(0));
    copied_escape = take(escaped_binding.export_part(0, orbital::PartRetention::selected_copy));
    escaped_first = orbital::view(escaped).address()[0];
    for (unsigned command = 0; command < 6; ++command) {
      sm::Store::Scope basis(store); auto selected = basis.bind(id);
      auto candidate = take(sm::Draft::clone(*pool, selected, 1024));
      auto operation = candidate.write(); auto h = candidate.holder_view();
      std::array<ikea::owner_write, 1024> records;
      ikea::source_write_journal effects{records};
      if (command == 0) { // same-reservation bytes invalidate the pruning bound
        std::string v = "Z"; require(operation.replace(0, bytes(v), effects) == sn::Error::none, "replace A to Z");
        model[0] = v;
      } else if (command == 1) { // live-empty insertion: no body store
        const auto rank = operation.count;
        require(operation.append({}, 0, effects) == sn::Error::none, "live empty append");
        set_holder(h, candidate.control.holders++, rank, effects); model.emplace_back(std::string{});
        geometry.push_back({0, 0});
      } else if (command == 2) { // shortening keeps initialized stale slack
        std::string v = "b"; require(operation.replace(1, bytes(v), effects) == sn::Error::none, "same body new length");
        model[1] = v;
        geometry[1].used = 1;
      } else if (command == 3) { // logical removal needs holder and tombstone damage
        require(operation.retire(1, effects) == sn::Error::none, "retirement");
        set_holder(h, 1, sm::absent, effects); model[1] = std::nullopt;
        geometry[1].dead = true;
      } else if (command == 4) {
        const auto rank = operation.count; const std::string value = "new payload";
        require(operation.append(bytes(value), 24, effects) == sn::Error::none, "nonempty append with slack");
        set_holder(h, candidate.control.holders++, rank, effects); model.emplace_back(value);
        geometry.push_back({24, unsigned(value.size())});
      } else {
        require(sn::soft_pack(operation, true, effects) == sn::Error::none, "soft relocation");
        for (auto& s : geometry) { if (s.dead) s.used = 0; s.reservation = s.used; }
      }
      auto damage = take(sm::freeze_damage(selected, candidate, operation, h, effects.entries()));
      if (command == 0) {
        auto other_draft = take(sm::Draft::clone(*pool, selected, 1024));
        other_draft.finish(); auto other = capture(store, other_draft);
        auto wrong_basis = take(sm::Draft::clone(*pool, other.selection(), 1024));
        auto wrong_w = wrong_basis.write(); auto wrong_h = wrong_basis.holder_view();
        require(!sm::freeze_damage(selected, wrong_basis, wrong_w, wrong_h, {}), "clone/freeze physical basis mismatch refuses");
      }
      if (command == 0) {
        auto bad = damage;
        bad.patches.push_back({2, 0, {std::byte{0xfe}, std::byte{0xff}}});
        require(!sm::reconstruct(*pool, selected, bad), "out-of-range live holder refuses");
      }
      if (command == 0) {
        auto oversized = operation;
        oversized.body = {operation.body.data(), operation.body.size() + 1};
        require(!sm::freeze_damage(selected, candidate, oversized, h, {}), "same-base wrong extent rejected");
        const auto sources = candidate.sources();
        auto wrong = candidate.control; ++wrong.body_extent;
        require(!store.capture(wrong, sources), "capture geometry refusal");
        require(bool(candidate.parts[0]) && bool(candidate.parts[1]) && bool(candidate.parts[2]),
            "capture refusal preserves Allocation owners");
      }
      if (command == 1) {
        for (const auto& p : damage.patches) require(p.part != 0, "empty append has no payload patch");
      }
      if (command == 4) {
        auto truncated = damage;
        auto p = std::find_if(truncated.patches.begin(), truncated.patches.end(), [](const auto& p) { return p.part == 0; });
        require(p != truncated.patches.end() && p->postimage.size() == 24, "new reservation has complete afterimage");
        p->postimage.pop_back();
        require(!sm::reconstruct(*pool, selected, truncated), "missing new slack byte refuses reconstruction");
      }
      if (command == 5) {
        require(std::any_of(damage.patches.begin(), damage.patches.end(), [](const auto& p) { return p.part == 0; }),
            "surviving tail actually moves and publishes body damage");
      }
      // Afterimages own their bytes and survive disposal/overwrite of builder.
      candidate = {};
      auto reconstructed = take(sm::reconstruct(*pool, selected, damage));
      auto owner = capture(store, reconstructed);
      require(equal(owner.selection(), model), "exact damage-reconstructed result");
      require(geometry_equal(owner.selection(), geometry), "exact physical damage-reconstructed result");
      for (unsigned drop = 0; drop < damage.patches.size(); ++drop) {
        const auto& patch = damage.patches[drop];
        const auto before_extent = *selected.control().extent(patch.part);
        const bool changed = patch.offset + patch.postimage.size() > before_extent ||
            !std::equal(patch.postimage.begin(), patch.postimage.end(), selected.part(patch.part)->address() + patch.offset);
        if (!changed) continue; // issued preserving stores need not change semantics
        auto omitted = damage; omitted.patches.erase(omitted.patches.begin() + drop);
        auto bad = sm::reconstruct(*pool, selected, omitted);
        if (bad) {
          auto broken = capture(store, *bad);
          require(!equal(broken.selection(), model) || !geometry_equal(broken.selection(), geometry),
              "each changed patch is necessary for these complete answers");
        }
      }
      // Dropping semantic/physical metadata must be caught by the independent
      // logical oracle even if geometric admission accepts the representation.
      if (command == 0) {
        auto bad = damage; bad.target.summary = selected.control().summary;
        auto d = take(sm::reconstruct(*pool, selected, bad)); auto o = capture(store, d);
        require(!equal(o.selection(), model), "stale summary rejected by oracle");
      }
      if (command != 0) {
        auto bad = damage;
        bad.patches.erase(std::remove_if(bad.patches.begin(), bad.patches.end(),
            [](const auto& p) { return p.part == 1; }), bad.patches.end());
        auto d = sm::reconstruct(*pool, selected, bad);
        if (d) { auto o = capture(store, *d); require(!equal(o.selection(), model) ||
            !geometry_equal(o.selection(), geometry), "missing metadata fails logical or byte-owner oracle"); }
      }
      if (command == 5) {
        // Compaction is physically equivalent, with a coherent holder/body law.
        auto target = take(store.conversion(basis, id));
        require(bool(store.adopt(target, owner)), "equivalent soft adoption");
        auto after = take(basis.bind_alternative(id));
        require(equal(after, model), "adopted alternative exact values");
        require(!sm::reconstruct(*pool, after, damage), "same-version different physical source rejects damage");
      } else publish(store, basis, std::move(owner));
      require(equal(old.bind(id), original), "old delayed discovery remains exact");
      require(orbital::view(escaped).address()[0] == escaped_first, "escaping body unchanged");
    }
    // A rewrite prepared from an old basis cannot discard an intervening edit,
    // even when that publication touched a different Object.
    {
      sm::Store::Scope stale(store); auto selected = stale.bind(id);
      auto d = take(sm::Draft::clone(*pool, selected, selected.control().body_extent)); d.finish();
      auto owner = capture(store, d);
      { sm::Store::Scope fresh(store); publish(store, fresh, owner, {19, 2}); }
      sm::Store::Prepared rejected(store); const sm::Store::Change change{id, owner};
      require(!store.prepare(stale, {&change, 1}, rejected), "stale global basis rejected");
      require(equal(stale.bind(id), model), "stale source intact");
      sm::Store::Scope current(store); require(equal(current.bind(id), model), "current source intact");
    }
    require(equal(late.bind(id), original), "actual late discovery after publications");
    require(equal(old.bind(id), original), "old reader before retirement");
  }
  // Only the body Part escapes. It preserves physical bytes and its inline
  // geometry; it does not implicitly retain holder/metadata allocations.
  require(orbital::view(escaped).address()[0] == escaped_first &&
      orbital::view(copied_escape).address()[0] == escaped_first, "body escape after all Scopes and Store end");
  orbital::release(escaped); orbital::release(copied_escape);
  for (;;) {
    const auto p = oa::close(*pool);
    if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool");
    orbital::worker::poll(lane, 128); std::this_thread::yield();
  }
  require(arena->charged_bytes == 0, "actual Arena refund after readers retire");
  require(bool(oa::remove_pool(*pool)), "remove Pool"); arena.reset(); orbital::worker::relinquish(lane);
  std::puts("StreamPack damage reconstruction, real MVCC publication/adoption, retained readers, stale basis and settlement passed");
}
