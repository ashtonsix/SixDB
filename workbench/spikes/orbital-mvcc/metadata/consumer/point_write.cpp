#include <ikea/tuplepack/author/execution.h>
#include "point_write.h"
#include "../orbital/shape/shape_private.h"
#include "consumer/native.h"
#include <algorithm>
#include <cassert>
#include <new>
#include <utility>

namespace orbital_mvcc::metadata_consumer::point_write {
namespace {
namespace sp = ikea::seriespack;
struct Refusal { orbital::Error error; };
void require(bool value, orbital::Error error = orbital::Error::invalid_argument) {
  if (!value) throw Refusal{error};
}
template<class T, class E> T take(std::expected<T, E> value) {
  require(bool(value)); return std::move(*value);
}
struct Range { std::uintptr_t first, last; };
Range range(const void* pointer, std::size_t bytes) {
  const auto first = reinterpret_cast<std::uintptr_t>(pointer);
  require((!bytes || first) && bytes <= UINTPTR_MAX - first);
  return {first, first + bytes};
}
bool overlaps(Range a, Range b) {
  return a.first < a.last && b.first < b.last && a.first < b.last && b.first < a.last;
}

bool same(const tp::packet_layout<8>& a, const tp::packet_layout<8>& b) {
  if (a.rows() != b.rows() || a.map_size() != b.map_size() ||
      a.used_bytes() != b.used_bytes() || a.row_indices() != b.row_indices() ||
      a.row_major() != b.row_major() || a.single_group() != b.single_group()) return false;
  for (unsigned row = 0; row < b.rows(); ++row)
    for (unsigned slot = 0; slot < b.map_size(); ++slot)
      if (a.offset(row, slot) != b.offset(row, slot)) return false;
  return true;
}
bool same(const tp::detail::gpr_write& a, const tp::detail::gpr_write& b) {
  if (a.read.codes != b.read.codes || !same(a.read.ordering, b.read.ordering) ||
      a.read.slots != b.read.slots || a.read.bytes != b.read.bytes || a.read.reads != b.read.reads ||
      a.invalid != b.invalid || a.count != b.count || a.runs != b.runs || a.writes != b.writes ||
      a.word.changed != b.word.changed || a.word.input_shift != b.word.input_shift ||
      a.word.output_shift != b.word.output_shift || a.word.offset != b.word.offset ||
      a.word.bytes != b.word.bytes || a.word.selected != b.word.selected ||
      a.transfer.mask != b.transfer.mask || a.transfer.offset != b.transfer.offset ||
      a.transfer.shift != b.transfer.shift || a.transfer.bytes != b.transfer.bytes ||
      a.invalid_word != b.invalid_word || a.native_reads != b.native_reads) return false;
  for (unsigned i = 0; i < 8; ++i) {
    const auto& x = a.stores[i]; const auto& y = b.stores[i];
    if (x.offset != y.offset || x.mask != y.mask || x.count != y.count ||
        x.input != y.input || x.shift != y.shift ||
        a.footprint[i].offset != b.footprint[i].offset ||
        a.footprint[i].size != b.footprint[i].size) return false;
  }
  return true;
}

void admit(const cs::Shape* shape, const TupleLaw* law, std::uint64_t count,
    std::span<const v::WritePart> target, std::span<const Command> input,
    v::KernelCosts& costs, ProbeCosts& probes) {
  require(!input.empty() && input.size() <= v::write_batch_limit);
  require(count <= SIZE_MAX && target.size() == shape->parts);
  // Shape owns the complete admitted geometry, law and descriptor. Protect its
  // entire allocation, not just the header or an 8-byte proof facade. The new
  // law owns every plan operand. Private clone authority remains a caller fact.
  const std::array protected_ranges{
      range(shape, shape->allocated_bytes), range(law, law ? sizeof(*law) : 0),
      range(input.data(), input.size_bytes()), range(target.data(), target.size_bytes()),
      range(&costs, sizeof(costs)), range(&probes, sizeof(probes))};
  for (unsigned i = 0; i < protected_ranges.size(); ++i)
    for (unsigned j = 0; j < i; ++j) require(!overlaps(protected_ranges[i], protected_ranges[j]));
  std::array<Range, 4> bodies{};
  for (unsigned i = 0; i < target.size(); ++i) {
    const auto& geometry = cs::geometries(shape)[i];
    const auto extent = geometry.extent(count);
    require(extent && *extent <= SIZE_MAX);
    require(target[i].id == geometry.id && target[i].bytes.size() == *extent);
    bodies[i] = range(target[i].bytes.data(), target[i].bytes.size());
    require(target[i].bytes.empty() || bodies[i].first % (std::size_t{1} << geometry.alignment_log2) == 0);
    for (unsigned j = 0; j < i; ++j) require(!overlaps(bodies[i], bodies[j]));
    for (const auto metadata : protected_ranges) require(!overlaps(bodies[i], metadata));
  }
}

using Rows = std::array<std::size_t, v::write_batch_limit>;
template<class ReadKey>
void resolve(std::size_t count, std::span<const Command> input, Rows& rows,
             ReadKey&& read_key, ProbeCosts& probes) {
  const auto key = [&](std::size_t row) {
    ++probes.key_reads; return read_key(row);
  };
  for (std::size_t i = 0; i < input.size(); ++i) {
    // Same offered-order value-before-key refusal as the checked control.
    // invalid_word alone cannot detect bits beyond the three-byte packet map.
    require(input[i].value <= orbital_bench::value_mask);
    rows[i] = v::native_detail::seek(count, input[i].key, key);
    require(rows[i] < count && key(rows[i]) == input[i].key, orbital::Error::absent);
  }
}
template<class View>
void account(const ikea::journal& effects, const View& view,
             std::size_t extent, v::KernelCosts& costs) {
  costs.issued_spans += effects.used;
  for (const auto& effect : effects.entries()) {
    assert(effect.source == &view && effect.plane == 0 &&
        effect.offset <= extent && effect.size <= extent - effect.offset);
    costs.issued_bytes += effect.size;
  }
}

void tuples(const TupleLaw& law, std::size_t count,
    std::span<const v::WritePart> target, std::span<const Command> input,
    const Rows& rows, v::KernelCosts& costs) {
  const auto view = take(tp::view::attach(law.layout(),
      {reinterpret_cast<std::byte*>(target[0].bytes.data()), target[0].bytes.size()}, count, v::tuple_bytes));
  const tp::author::native_writer value{take(tp::bind_writer(law.writers()[0], view))};
  const tp::author::native_writer tag{take(tp::bind_writer(law.writers()[1], view))};
  std::array<ikea::journal::entry, 2> journal;
  for (std::size_t i = 0; i < input.size(); ++i)
    require(take(value.effect_capacity(rows[i], 1)) + take(tag.effect_capacity(rows[i], 1)) <= journal.size());
  for (const auto& update : input)
    require(value.accepts(update.value) && tag.accepts(update.tag));
  costs.bindings += 2;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::journal effects{journal};
    value.set_unchecked(rows[i], std::uint64_t(input[i].value), effects);
    tag.set_unchecked(rows[i], std::uint64_t(input[i].tag), effects);
    account(effects, view, view.storage().size(), costs);
    costs.encoded_values += 2;
  }
}

template<unsigned K>
auto column_view(std::size_t count, orbital::MutableBytes bytes) {
  std::array<sp::plane<std::byte>, 3> planes{};
  planes[0] = {{reinterpret_cast<std::byte*>(bytes.data()), bytes.size()}, v::Column<K>::tile_bytes};
  return take(sp::view<v::Column<K>>::attach(count, planes));
}
template<unsigned K>
auto decoder(const std::byte* pointer, std::size_t count) {
  return sp::bind_reader<std::uint64_t>(sp::dense_source<v::Column<K>>{
      reinterpret_cast<const std::byte*>(pointer), count});
}
void columns(std::size_t count, std::span<const v::WritePart> target,
    std::span<const Command> input, const Rows& rows, v::KernelCosts& costs) {
  // Each prepared mutation borrows a named stationary view through effect
  // resolution. Key is read through its typed decoder and never made mutable.
  const auto value_view = column_view<20>(count, target[2].bytes);
  const auto tag_view = column_view<32>(count, target[3].bytes);
  const auto value = take(sp::bind_writer(value_view));
  const auto tag = take(sp::bind_writer(tag_view));
  std::array<ikea::journal::entry, 3> journal;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto needed = take(value.effect_capacity(rows[i], 1, sp::selection::all())) +
        take(tag.effect_capacity(rows[i], 1, sp::selection::all()));
    require(needed <= journal.size());
  }
  costs.bindings += 2;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  sp::author::no_summary summary;
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::journal effects{journal};
    sp::author::replace_point_unchecked(value.expression(), rows[i], std::uint64_t(input[i].value), summary, effects);
    account(effects, value_view, value_view.stream(0).bytes.size(), costs); effects.used = 0;
    sp::author::replace_point_unchecked(tag.expression(), rows[i], std::uint64_t(input[i].tag), summary, effects);
    account(effects, tag_view, tag_view.stream(0).bytes.size(), costs);
    costs.encoded_values += 2;
  }
}

} // namespace

TupleLaw::TupleLaw(Admission, const cs::Shape* shape, const tp::layout& layout,
    std::span<const tp::write_plan<8>, 2> writers) noexcept
    : shape_(shape), layout_(layout), writers_{writers[0], writers[1]} {}

orbital::Result<void> TupleLaw::prepare(std::optional<TupleLaw>& destination,
    const cs::Shape* shape, const tp::layout& layout, std::span<const tp::write_plan<8>, 2> writers,
    orbital::ProgramId program, orbital::PartId part) noexcept {
  const auto invalid = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (destination) return invalid();
  const auto read_proof = shape_read::admit_tuple_shape(shape, program, part);
  if (!read_proof || layout.bytes() != v::tuple_bytes || layout.encoded_size() != 75) return invalid();
  std::array<std::uint8_t, 75> encoded{};
  const auto serialized = layout.encode(std::as_writable_bytes(std::span{encoded}));
  if (!serialized || *serialized != encoded.size() || encoded != compact_law::detail::descriptor()) return invalid();
  constexpr std::array<unsigned, 2> starts{16, 19}, widths{3, 4};
  for (unsigned field = 0; field < 2; ++field) {
    std::array<std::uint8_t, 8> map{};
    for (unsigned i = 0; i < widths[field]; ++i) map[i] = starts[field] + i;
    const auto expected = tp::write_plan<8>::make(layout, std::span(map).first(widths[field]));
    if (!expected || writers[field].unit_bytes() != expected->unit_bytes() ||
        !same(writers[field].controls(), expected->controls())) return invalid();
  }
  static_assert(std::is_nothrow_copy_constructible_v<tp::layout> &&
      std::is_nothrow_copy_constructible_v<tp::write_plan<8>>);
  destination.emplace(Admission{}, shape, layout, writers);
  return {};
}

orbital::Result<void> admit_mapped(TupleProof, std::uint64_t count,
    orbital::Bytes body) noexcept {
  if (count > SIZE_MAX / v::tuple_bytes || body.size() != count * v::tuple_bytes ||
      !compact_law::detail::valid_span(body)) return std::unexpected(orbital::Error::invalid_argument);
  return {};
}

orbital::Result<void> apply(TupleProof proof, std::uint64_t count,
    std::span<const v::WritePart> target, std::span<const Command> input,
    v::KernelCosts& output, ProbeCosts& output_probes) noexcept {
  try {
    const auto& law = proof.law();
    admit(law.shape(), &law, count, target, input, output, output_probes);
    auto costs = output; auto probes = output_probes; ++probes.private_admissions;
    Rows rows;
    const auto& read_law = *cs::law(const_cast<cs::Shape*>(law.shape()));
    const auto* pointer = reinterpret_cast<const std::byte*>(target[0].bytes.data());
    resolve(count, input, rows, [&](std::size_t i) { return read_law.read_unchecked<0>(pointer + i * v::tuple_bytes); }, probes);
    tuples(law, count, target, input, rows, costs);
    probes.applied_updates += input.size(); output = costs; output_probes = probes;
    return {};
  } catch (Refusal refusal) { return std::unexpected(refusal.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> apply(SeriesProof proof, std::uint64_t count,
    std::span<const v::WritePart> target, std::span<const Command> input,
    v::KernelCosts& output, ProbeCosts& output_probes) noexcept {
  try {
    admit(proof.shape(), nullptr, count, target, input, output, output_probes);
    auto costs = output; auto probes = output_probes; ++probes.private_admissions;
    Rows rows;
    const auto keys = decoder<64>(target[0].bytes.data(), count);
    resolve(count, input, rows, [&](std::size_t i) { return keys.get_unchecked(i); }, probes);
    columns(count, target, input, rows, costs);
    probes.applied_updates += input.size(); output = costs; output_probes = probes;
    return {};
  } catch (Refusal refusal) { return std::unexpected(refusal.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
} // namespace orbital_mvcc::metadata_consumer::point_write
