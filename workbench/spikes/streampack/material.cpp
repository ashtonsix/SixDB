#include "material.h"
#include <object/representation_impl.h>
#include <object/store_impl.h>

namespace streampack_material {
namespace {
sn::Direct<std::uint32_t, true, true> bind(Control c, unsigned block, const std::byte* body, const std::byte* metadata) {
  const Layout l{c.capacity}; const auto n = *reinterpret_cast<const std::uint16_t*>(metadata);
  return {{reinterpret_cast<const std::uint32_t*>(metadata + l.ends()), std::size_t(n) + 1},
      {body, c.body_bytes(block)}, {reinterpret_cast<const std::uint16_t*>(metadata + l.used()), c.capacity},
      {reinterpret_cast<const std::uint64_t*>(metadata + l.tombs()), std::size_t(c.capacity / 64)}};
}
template<class Read> Summary sum(Read&& read, std::span<const std::uint32_t> rows) {
  Summary result;
  for (auto location : rows) {
    if (location == absent) continue;
    const auto v = read(block_of(location)); const auto r = rank_of(location);
    assert(r < v.size() && v.live(r));
    ++result.live_rows; result.used_bytes += v.length(r);
    for (auto b : v.value(r)) {
      result.minimum = std::min(result.minimum, std::to_integer<std::uint8_t>(b));
      result.maximum = std::max(result.maximum, std::to_integer<std::uint8_t>(b));
    }
  }
  return result;
}
template<class Write> bool admitted_as(Draft& d, bool match_extent, Write write) {
  for (unsigned b = 0; b < d.control.blocks; ++b) {
    auto w = write(b);
    if (w.count > d.control.capacity || w.read().admit(d.control.maximum()) != sn::Error::none ||
        (match_extent && w.read().extent() != d.control.body_bytes(b))) return false;
  }
  for (auto x : d.holder_view().rows.first(d.control.holders)) {
    if (x == absent) continue;
    if (block_of(x) >= d.control.blocks) return false;
    const auto v = write(block_of(x)).read();
    if (rank_of(x) >= v.size() || !v.live(rank_of(x))) return false;
  }
  return true;
}
bool admitted(Draft& d, bool match_extent) {
  if (d.control.geometry == Geometry::widths8)
    return admitted_as(d, match_extent, [&](unsigned b) { return d.write_widths(b); });
  return admitted_as(d, match_extent, [&](unsigned b) { return d.write(b); });
}
}
Result<Physical::Value*> Physical::capture(std::pmr::memory_resource& r,
    const Control& c, std::span<const PartSource> parts) noexcept {
  return orbital::capture_representation(r, c, orbital::PartOwners::independent_allocation, parts);
}
Result<orbital::Part<Control>*> Physical::export_part(Selection s, std::size_t i,
    orbital::ObjectBinding b, ExportPolicy policy) noexcept {
  return orbital::object_detail::retain_part(s, i, b, policy);
}
sn::Direct<std::uint32_t, true, true> read(Selection s, unsigned block) {
  assert(block < s.control().blocks && s.control().geometry == Geometry::direct32);
  return bind(s.control(), block, s.part(block * 2)->address(), s.part(block * 2 + 1)->address());
}
sn::Grouped<std::uint8_t, false, true, true> read_widths(Selection s, unsigned block) {
  const auto& c = s.control(); assert(block < c.blocks && c.geometry == Geometry::widths8);
  const Layout l{c.capacity, c.geometry}; const auto* m = s.part(block * 2 + 1)->address();
  const auto n = *reinterpret_cast<const std::uint16_t*>(m);
  const auto groups = unsigned((n + 15) / 16);
  return {{reinterpret_cast<const std::uint8_t*>(m + l.ends()), groups * 16},
      {reinterpret_cast<const std::uint32_t*>(m + l.bases()), groups + 1},
      {s.part(block * 2)->address(), c.body_bytes(block)},
      {reinterpret_cast<const std::uint16_t*>(m + l.used()), c.capacity},
      {reinterpret_cast<const std::uint64_t*>(m + l.tombs()), std::size_t(c.capacity / 64)}, n};
}
std::span<const std::uint32_t> holders(Selection s) {
  return {reinterpret_cast<const std::uint32_t*>(s.part(s.control().holder_part())->address()), s.control().holders};
}
Summary summarize(Selection s) {
  if (s.control().geometry == Geometry::widths8) return sum([&](unsigned b) { return read_widths(s, b); }, holders(s));
  return sum([&](unsigned b) { return read(s, b); }, holders(s));
}
Result<Draft> Draft::create(oa::Pool& pool, Control c, std::size_t body_capacity) {
  return create(pool, c, std::array<std::size_t, 2>{body_capacity, c.second_body_extent});
}
Result<Draft> Draft::create(oa::Pool& pool, Control c, std::array<std::size_t, 2> capacities) {
  if (!c.memory || !c.valid(*c.memory) || c.selected != 255) return std::unexpected(Error::invalid_argument);
  for (unsigned b = 0; b < c.blocks; ++b) if (capacities[b] < c.body_bytes(b)) return std::unexpected(Error::invalid_argument);
  Draft d; d.control = c;
  for (unsigned i = 0; i < c.full_parts(); ++i) {
    const auto n = i < c.holder_part() && i % 2 == 0 ? capacities[i / 2] : *c.extent(i);
    if (!n) continue;
    auto result = oa::allocate(pool, {.bytes = n, .alignment = 8});
    if (!result) return std::unexpected(result.error());
    d.parts[i] = std::move(*result);
  }
  // Construction initializes the complete captured metadata capacity. All body
  // bytes are initialized by append; allocator capacity alone is not readable.
  if (d.parts[c.holder_part()].size) std::memset(d.parts[c.holder_part()].address, 255, d.parts[c.holder_part()].size);
  for (unsigned b = 0; b < c.blocks; ++b) {
    auto& m = d.parts[b * 2 + 1]; std::memset(m.address, 0, m.size);
    ikea::no_coverage effects;
    const auto error = c.geometry == Geometry::widths8 ? d.write_widths(b).initialize(effects) : d.write(b).initialize(effects);
    if (error != sn::Error::none) return std::unexpected(Error::invalid_argument);
  }
  return d;
}
Result<Draft> Draft::clone(oa::Pool& pool, Selection s, std::size_t body_capacity) {
  if (!s) return std::unexpected(Error::invalid_argument);
  return clone(pool, s, std::array<std::size_t, 2>{body_capacity, s.control().second_body_extent});
}
Result<Draft> Draft::clone(oa::Pool& pool, Selection s, std::array<std::size_t, 2> capacities) {
  if (!s) return std::unexpected(Error::invalid_argument);
  auto result = create(pool, s.control(), capacities);
  if (!result) return result;
  result->predecessor = Store::Owner::retain(s.realization);
  for (unsigned i = 0; i < s.control().full_parts(); ++i) {
    auto n = *s.control().extent(i);
    if (n) std::memcpy(result->parts[i].address, s.part(i)->address(), n);
  }
  return result;
}
sn::MutableDirect<std::uint32_t> Draft::write(unsigned block) {
  assert(block < control.blocks && control.geometry == Geometry::direct32);
  const Layout l{control.capacity}; auto* m = parts[block * 2 + 1].address;
  return {{reinterpret_cast<std::uint32_t*>(m + l.ends()), std::size_t(control.capacity) + 1},
      {parts[block * 2].address, parts[block * 2].size}, {reinterpret_cast<std::uint16_t*>(m + l.used()), control.capacity},
      {reinterpret_cast<std::uint64_t*>(m + l.tombs()), std::size_t(control.capacity / 64)},
      *reinterpret_cast<std::uint16_t*>(m)};
}
sn::MutableWidths<std::uint8_t> Draft::write_widths(unsigned block) {
  assert(block < control.blocks && control.geometry == Geometry::widths8);
  const Layout l{control.capacity, control.geometry}; auto* m = parts[block * 2 + 1].address;
  return {{reinterpret_cast<std::uint8_t*>(m + l.ends()), control.capacity},
      {reinterpret_cast<std::uint32_t*>(m + l.bases()), std::size_t(control.capacity / 16 + 1)},
      {parts[block * 2].address, parts[block * 2].size},
      {reinterpret_cast<std::uint16_t*>(m + l.used()), control.capacity},
      {reinterpret_cast<std::uint64_t*>(m + l.tombs()), std::size_t(control.capacity / 64)}, *reinterpret_cast<std::uint16_t*>(m)};
}
HolderView Draft::holder_view() {
  return {{reinterpret_cast<std::uint32_t*>(parts[control.holder_part()].address), control.holder_limit()}};
}
std::vector<orbital::PartSource> Draft::sources() {
  std::vector<orbital::PartSource> result(control.full_parts());
  for (unsigned i = 0; i < result.size(); ++i)
    result[i] = {control.id(i), {parts[i].address, *control.extent(i)}, {}, &parts[i]};
  return result;
}
void Draft::finish() {
  if (control.geometry == Geometry::widths8) {
    control.body_extent = write_widths().read().extent();
    control.second_body_extent = control.blocks == 2 ? write_widths(1).read().extent() : 0;
    control.summary = sum([&](unsigned b) { return write_widths(b).read(); }, holder_view().rows.first(control.holders));
    return;
  }
  control.body_extent = write().read().extent();
  control.second_body_extent = control.blocks == 2 ? write(1).read().extent() : 0;
  control.summary = sum([&](unsigned b) { return write(b).read(); }, holder_view().rows.first(control.holders));
}
template<class Source> Result<Damage> freeze_as(Selection predecessor, Draft& completed,
    std::span<const Source> sources, const HolderView& holder,
    std::span<const ikea::owner_write> issued) {
  constexpr bool widths = requires (Source s) { s.widths; };
  const auto& c = completed.control;
  if (!predecessor || predecessor.realization != completed.predecessor.get() || !c.memory || !c.valid(*c.memory) ||
      c.selected != 255 || sources.size() != c.blocks || c.geometry != predecessor.control().geometry ||
      c.geometry != (widths ? Geometry::widths8 : Geometry::direct32) ||
      c.capacity != predecessor.control().capacity || c.blocks != predecessor.control().blocks ||
      c.holder_limit() != predecessor.control().holder_limit()) return std::unexpected(Error::invalid_argument);
  for (unsigned b = 0; b < c.blocks; ++b) {
    const auto& source = sources[b];
    auto current = [&] { if constexpr (widths) return completed.write_widths(b); else return completed.write(b); }();
    if constexpr (widths) {
      if (source.widths.data() != current.widths.data() || source.widths.size() != current.widths.size() ||
          source.bases.data() != current.bases.data() || source.bases.size() != current.bases.size()) return std::unexpected(Error::invalid_argument);
    } else if (source.ends.data() != current.ends.data() || source.ends.size() != current.ends.size()) return std::unexpected(Error::invalid_argument);
    if (source.body.data() != current.body.data() ||
        &source.count != &current.count || source.used.data() != current.used.data() || source.tombs.data() != current.tombs.data() ||
        source.body.size() != current.body.size() ||
        source.used.size() != current.used.size() || source.tombs.size() != current.tombs.size()) return std::unexpected(Error::invalid_argument);
  }
  if (holder.rows.data() != completed.holder_view().rows.data() || holder.rows.size() != completed.holder_view().rows.size() ||
      !admitted(completed, false)) return std::unexpected(Error::invalid_argument);
  completed.finish();
  Damage result{Store::Owner::retain(predecessor.realization), completed.control, {}};
  const Layout l{c.capacity, c.geometry};
  for (const auto& e : issued) {
    unsigned part; std::size_t origin, extent;
    unsigned b = 0; for (; b < sources.size(); ++b) if (e.source == &sources[b]) break;
    if (b < sources.size()) {
      const auto& source = sources[b];
      switch (e.bytes.plane) {
        case 0: part = b * 2; origin = 0; extent = source.body.size(); break;
        case 1:
          part = b * 2 + 1; origin = l.ends();
          if constexpr (widths) extent = source.widths.size_bytes(); else extent = source.ends.size_bytes();
          break;
        case 2: part = b * 2 + 1; origin = l.used(); extent = source.used.size_bytes(); break;
        case 3: part = b * 2 + 1; origin = l.tombs(); extent = source.tombs.size_bytes(); break;
        case 4: part = b * 2 + 1; origin = 0; extent = sizeof(source.count); break;
        case 5:
          if constexpr (widths) { part = b * 2 + 1; origin = l.bases(); extent = source.bases.size_bytes(); break; }
          else return std::unexpected(Error::invalid_argument);
        default: return std::unexpected(Error::invalid_argument);
      }
    } else if (e.source == &holder && e.bytes.plane == 0) {
      part = c.holder_part(); origin = 0; extent = holder.rows.size_bytes();
    } else return std::unexpected(Error::invalid_argument);
    if (e.bytes.offset > extent || e.bytes.size > extent - e.bytes.offset) return std::unexpected(Error::invalid_argument);
    const auto target_extent = *c.extent(part);
    if (origin + e.bytes.offset >= target_extent) continue;
    const auto n = std::min(e.bytes.size, target_extent - origin - e.bytes.offset);
    const auto* begin = completed.parts[part].address + origin + e.bytes.offset;
    result.patches.push_back({part, origin + e.bytes.offset, {begin, begin + n}});
  }
  return result;
}
Result<Damage> freeze_damage(Selection p, Draft& d, const sn::MutableDirect<std::uint32_t>& s,
    const HolderView& h, std::span<const ikea::owner_write> e) { return freeze_as(p, d, std::span(&s, 1), h, e); }
Result<Damage> freeze_damage(Selection p, Draft& d, std::span<const sn::MutableDirect<std::uint32_t>> s,
    const HolderView& h, std::span<const ikea::owner_write> e) { return freeze_as(p, d, s, h, e); }
Result<Damage> freeze_damage(Selection p, Draft& d, const sn::MutableWidths<std::uint8_t>& s,
    const HolderView& h, std::span<const ikea::owner_write> e) { return freeze_as(p, d, std::span(&s, 1), h, e); }
Result<Damage> freeze_damage(Selection p, Draft& d, std::span<const sn::MutableWidths<std::uint8_t>> s,
    const HolderView& h, std::span<const ikea::owner_write> e) { return freeze_as(p, d, s, h, e); }
Result<Draft> reconstruct(oa::Pool& pool, Selection exact, const Damage& damage) {
  return reconstruct(pool, exact, damage, {damage.target.body_extent, damage.target.second_body_extent});
}
Result<Draft> reconstruct(oa::Pool& pool, Selection exact, const Damage& damage, std::array<std::size_t, 2> capacities) {
  const auto& c = damage.target;
  if (!exact || !damage.predecessor || !c.memory || !c.valid(*c.memory) || c.selected != 255) return std::unexpected(Error::invalid_argument);
  // Afterimages here cover edits within the same Part topology. A topology-
  // changing hard rewrite is captured as complete new material instead.
  if (exact.realization != damage.predecessor.get() || exact.control().capacity != c.capacity || exact.control().blocks != c.blocks ||
      exact.control().holder_limit() != c.holder_limit() || exact.control().geometry != c.geometry)
    return std::unexpected(Error::conflict);
  auto candidate = Draft::create(pool, c, capacities);
  if (!candidate) return candidate;
  candidate->predecessor = Store::Owner::retain(exact.realization);
  for (unsigned i = 0; i < c.full_parts(); ++i) {
    auto n = std::min(*exact.control().extent(i), *c.extent(i));
    if (n) std::memcpy(candidate->parts[i].address, exact.part(i)->address(), n);
  }
  std::array<std::vector<bool>, 2> new_body;
  for (unsigned b = 0; b < c.blocks; ++b) if (c.body_bytes(b) > exact.control().body_bytes(b))
    new_body[b].resize(c.body_bytes(b) - exact.control().body_bytes(b));
  for (const auto& p : damage.patches) {
    if (p.part >= c.full_parts() || p.offset > *c.extent(p.part) || p.postimage.size() > *c.extent(p.part) - p.offset)
      return std::unexpected(Error::invalid_argument);
    if (!p.postimage.empty()) std::memcpy(candidate->parts[p.part].address + p.offset, p.postimage.data(), p.postimage.size());
    if (p.part < c.holder_part() && p.part % 2 == 0) {
      const auto b = p.part / 2, old = exact.control().body_bytes(b);
      for (auto x = std::max<std::size_t>(p.offset, old); x < p.offset + p.postimage.size(); ++x) new_body[b][x - old] = true;
    }
  }
  for (const auto& coverage : new_body)
    if (std::find(coverage.begin(), coverage.end(), false) != coverage.end()) return std::unexpected(Error::invalid_argument);
  if (!admitted(*candidate, true)) return std::unexpected(Error::invalid_argument);
  return candidate;
}
} // namespace streampack_material
template struct orbital::object_detail::History<streampack_material::Physical>;
template class orbital::ObjectStore<streampack_material::Physical>;
template struct orbital::RepresentationSelection<streampack_material::Control>;
template void orbital::retain(orbital::Representation<streampack_material::Control>*) noexcept;
template void orbital::release(orbital::Representation<streampack_material::Control>*) noexcept;
template orbital::View orbital::view(const orbital::Part<streampack_material::Control>*) noexcept;
template void orbital::release(orbital::Part<streampack_material::Control>*) noexcept;
