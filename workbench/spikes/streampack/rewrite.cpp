#include "rewrite.h"
#include <string_view>
#include <unordered_map>

namespace streampack_material {
namespace sm = streampack_material;
// Concrete two-block caller. Hard construction is a full replacement; mutation
// after it uses the damage consumer. No byte-patch fiction across topology.
template<bool Widths> Result<Rewrite> hard_as(oa::Pool& pool, sm::Selection source, bool traversal, bool dedup, bool shrink_blocks) {
  const auto c = source.control(); const unsigned cap = c.capacity;
  const auto read = [&](unsigned b) { if constexpr (Widths) return sm::read_widths(source, b); else return sm::read(source, b); };
  Rewrite result; result.remap.resize(2 * cap, sm::absent);
  std::vector<bool> reachable(2 * cap);
  for (auto x : sm::holders(source)) if (x != sm::absent) reachable[sm::block_of(x) * cap + sm::rank_of(x)] = true;
  std::vector<std::uint32_t> order;
  if (traversal) {
    for (auto x : sm::holders(source)) if (x != sm::absent) order.push_back(x);
  } else for (unsigned i = 0; i < 2 * cap; ++i) if (reachable[i]) order.push_back(sm::location(i / cap, i % cap));
  std::unordered_map<std::string_view, unsigned> canonical;
  std::array<std::size_t, 2> extents{};
  for (auto x : order) {
    const auto index = sm::block_of(x) * cap + sm::rank_of(x);
    if (result.remap[index] != sm::absent) continue;
    const auto v = read(sm::block_of(x)); const auto r = sm::rank_of(x); const auto b = v.value(r);
    unsigned ordinal = result.sources.size();
    if (dedup) {
      const std::string_view key(reinterpret_cast<const char*>(b.data()), b.size());
      const auto [where, inserted] = canonical.emplace(key, ordinal);
      if (!inserted) ordinal = where->second;
    }
    result.remap[index] = sm::location(ordinal / cap, ordinal % cap);
    if (ordinal == result.sources.size()) {
      result.sources.push_back(x); extents[ordinal / cap] += v.reserved(r);
    }
  }
  // Both rewrite policies retain the same live reservation policy: copy each
  // surviving reservation width, initialize slack. No spare per-slot expansion.
  auto target = c; target.body_extent = target.second_body_extent = 0;
  target.holder_capacity = c.holder_limit();
  if (shrink_blocks) target.blocks = result.sources.size() <= cap ? 1 : 2;
  auto created = sm::Draft::create(pool, target, extents);
  if (!created) return std::unexpected(created.error());
  result.draft = std::move(*created);
  ikea::no_effects ignored;
  for (unsigned i = 0; i < result.sources.size(); ++i) {
    const auto x = result.sources[i]; const auto old = read(sm::block_of(x)); const auto r = sm::rank_of(x);
    auto dst = [&] { if constexpr (Widths) return result.draft.write_widths(i / cap); else return result.draft.write(i / cap); }();
    if (dst.append(old.value(r), old.reserved(r), ignored) != sn::Error::none) return std::unexpected(Error::invalid_argument);
  }
  auto h = result.draft.holder_view();
  for (unsigned row = 0; row < c.holders; ++row) {
    const auto old = sm::holders(source)[row];
    h.rows[row] = old == sm::absent ? sm::absent : result.remap[sm::block_of(old) * cap + sm::rank_of(old)];
  }
  result.draft.finish(); return result;
}
Result<Rewrite> hard(oa::Pool& pool, sm::Selection source, bool traversal, bool dedup, bool shrink_blocks) {
  if (!source) return std::unexpected(Error::invalid_argument);
  if (source.control().geometry == Geometry::widths8) return hard_as<true>(pool, source, traversal, dedup, shrink_blocks);
  return hard_as<false>(pool, source, traversal, dedup, shrink_blocks);
}
} // namespace streampack_material
