#include "shape_write.h"
#include "../orbital/shape/shape_private.h"
#include "consumer/native.h"
#include <algorithm>
#include <cassert>
#include <new>
#include <utility>

namespace orbital_mvcc::metadata_consumer::shape_write {
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
    std::span<const v::WritePart> target, std::span<const v::Update> input,
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
void resolve(std::size_t count, std::span<const v::Update> input, Rows& rows,
             ReadKey&& read_key, ProbeCosts& probes) {
  const auto key = [&](std::size_t row) {
    ++probes.key_reads; return read_key(row);
  };
  for (std::size_t i = 0; i < input.size(); ++i) {
    // Same offered-order value-before-key refusal as the checked control.
    // invalid_word alone cannot detect bits beyond the three-byte packet map.
    require(input[i].record.value <= orbital_bench::value_mask);
    rows[i] = v::native_detail::seek(count, input[i].key, key);
    require(rows[i] < count && key(rows[i]) == input[i].key, orbital::Error::absent);
  }
}
template<class View>
void account(const ikea::source_write_journal& effects, const View& view,
             std::size_t extent, v::KernelCosts& costs) {
  costs.issued_spans += effects.used;
  for (const auto& effect : effects.entries()) {
    assert(effect.source == &view && effect.bytes.plane == 0 &&
        effect.bytes.offset <= extent && effect.bytes.size <= extent - effect.bytes.offset);
    costs.issued_bytes += effect.bytes.size;
  }
}

void tuples(const TupleLaw& law, std::size_t count,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    const Rows& rows, v::KernelCosts& costs) {
  const auto view = take(tp::view::bind(law.layout(),
      {reinterpret_cast<tp::byte*>(target[0].bytes.data()), target[0].bytes.size()}, count, v::tuple_bytes));
  const tp::native_writer next{take(tp::bind_writer(law.writers()[0], view))};
  const tp::native_writer value{take(tp::bind_writer(law.writers()[1], view))};
  const tp::native_writer tag{take(tp::bind_writer(law.writers()[2], view))};
  std::array<ikea::owner_write, 4> journal;
  require(next.effect_capacity() + value.effect_capacity() + tag.effect_capacity() <= journal.size());
  for (const auto& update : input)
    require(next.accepts(update.record.next) && value.accepts(update.record.value) && tag.accepts(update.record.tag));
  costs.bindings += 3;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::source_write_journal effects{journal};
    next.set_unchecked(rows[i], input[i].record.next, effects);
    value.set_unchecked(rows[i], std::uint64_t(input[i].record.value), effects);
    tag.set_unchecked(rows[i], std::uint64_t(input[i].record.tag), effects);
    account(effects, view, view.storage().size(), costs);
    costs.encoded_values += 3;
  }
}

template<unsigned K>
auto column_view(std::size_t count, orbital::MutableBytes bytes) {
  std::array<sp::plane<std::uint8_t>, 3> planes{};
  planes[0] = {{reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()}, v::Column<K>::tile_bytes};
  return take(sp::view<v::Column<K>, std::uint8_t>::attach(count, planes));
}
template<unsigned K>
auto decoder(const std::byte* pointer, std::size_t count) {
  return sp::bind_decoder<std::uint64_t>(sp::dense_source<v::Column<K>>{
      reinterpret_cast<const std::uint8_t*>(pointer), count});
}
void columns(std::size_t count, std::span<const v::WritePart> target,
    std::span<const v::Update> input, const Rows& rows, v::KernelCosts& costs) {
  // Each prepared mutation borrows a named stationary view through effect
  // resolution. Key is read through its typed decoder and never made mutable.
  const auto next_view = column_view<64>(count, target[1].bytes);
  const auto value_view = column_view<20>(count, target[2].bytes);
  const auto tag_view = column_view<32>(count, target[3].bytes);
  const auto next = take(sp::bind_mutation(next_view));
  const auto value = take(sp::bind_mutation(value_view));
  const auto tag = take(sp::bind_mutation(tag_view));
  std::array<ikea::owner_write, 4> journal;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto needed = take(next.effect_capacity(rows[i], 1, sp::row_selection::all())) +
        take(value.effect_capacity(rows[i], 1, sp::row_selection::all())) +
        take(tag.effect_capacity(rows[i], 1, sp::row_selection::all()));
    require(needed <= journal.size());
  }
  costs.bindings += 3;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  sp::no_summary summary;
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::source_write_journal effects{journal};
    sp::composition::replace_point_unchecked(next.expression(), rows[i], input[i].record.next, summary, effects);
    account(effects, next_view, next_view.stream(0).bytes.size(), costs); effects.used = 0;
    sp::composition::replace_point_unchecked(value.expression(), rows[i], std::uint64_t(input[i].record.value), summary, effects);
    account(effects, value_view, value_view.stream(0).bytes.size(), costs); effects.used = 0;
    sp::composition::replace_point_unchecked(tag.expression(), rows[i], std::uint64_t(input[i].record.tag), summary, effects);
    account(effects, tag_view, tag_view.stream(0).bytes.size(), costs);
    costs.encoded_values += 3;
  }
}

template<class ReadKey, class ReadRecord>
orbital::Result<void> lookup_row(std::size_t count, Key key, std::size_t& output_row,
    Record& output, ReadKey&& read_key, ReadRecord&& read_record) {
  const auto row = v::native_detail::seek(count, key, read_key);
  if (row == count || read_key(row) != key) return std::unexpected(orbital::Error::absent);
  const auto record = read_record(row);
  output_row = row; output = record;
  return {};
}
orbital::Result<void> tuple_lookup(const compact_law::ReadLaw& law,
    const std::byte* pointer, std::size_t count, Key key, std::size_t& row, Record& output) {
  const auto address = [&](std::size_t i) { return reinterpret_cast<const tp::byte*>(pointer) + i * v::tuple_bytes; };
  return lookup_row(count, key, row, output,
      [&](std::size_t i) { return law.read_unchecked<0>(address(i)); },
      [&](std::size_t i) { const auto* source = address(i);
        return Record{law.read_unchecked<1>(source), std::uint32_t(law.read_unchecked<2>(source)),
            std::uint32_t(law.read_unchecked<3>(source))}; });
}
} // namespace

TupleLaw::TupleLaw(Admission, const cs::Shape* shape, const tp::layout& layout,
    std::span<const tp::writer<8>, 3> writers) noexcept
    : shape_(shape), layout_(layout), writers_{writers[0], writers[1], writers[2]} {}

orbital::Result<void> TupleLaw::prepare(std::optional<TupleLaw>& destination,
    const cs::Shape* shape, const tp::layout& layout, std::span<const tp::writer<8>, 3> writers,
    orbital::ProgramId program, orbital::PartId part) noexcept {
  const auto invalid = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (destination) return invalid();
  const auto read_proof = shape_read::admit_tuple_shape(shape, program, part);
  if (!read_proof || layout.bytes() != v::tuple_bytes || layout.encoded_size() != 75) return invalid();
  std::array<tp::byte, 75> encoded{};
  const auto serialized = layout.encode(encoded);
  if (!serialized || *serialized != encoded.size() || encoded != compact_law::detail::descriptor()) return invalid();
  constexpr std::array<unsigned, 3> starts{8, 16, 19}, widths{8, 3, 4};
  for (unsigned field = 0; field < 3; ++field) {
    std::array<tp::byte, 8> map{};
    for (unsigned i = 0; i < widths[field]; ++i) map[i] = starts[field] + i;
    const auto expected = tp::writer<8>::make(layout, std::span(map).first(widths[field]));
    if (!expected || writers[field].unit_bytes() != expected->unit_bytes() ||
        !same(writers[field].controls(), expected->controls())) return invalid();
  }
  static_assert(std::is_nothrow_copy_constructible_v<tp::layout> &&
      std::is_nothrow_copy_constructible_v<tp::writer<8>>);
  destination.emplace(Admission{}, shape, layout, writers);
  return {};
}

orbital::Result<SeriesProof> admit_series_shape(const cs::Shape* shape,
    orbital::ProgramId program, std::span<const orbital::PartId, 4> ids) noexcept {
  const auto invalid = [] { return std::unexpected(orbital::Error::invalid_argument); };
  if (!shape || shape->program != program || shape->kind != cs::Kind::series || shape->parts != 4) return invalid();
  constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
  for (unsigned i = 0; i < 4; ++i) {
    const auto& geometry = cs::geometries(shape)[i];
    if (geometry.id != ids[i] || geometry.kind != 1 || geometry.tile_rows != 8 ||
        geometry.tile_bytes != widths[i] || geometry.readable_padding || geometry.alignment_log2 != 6) return invalid();
  }
  return SeriesProof{shape};
}

orbital::Result<void> lookup(TupleProof proof, cs::Selection selected,
    Key key, std::size_t& row, Record& output) noexcept {
  if (!selected) return std::unexpected(orbital::Error::absent);
  auto* value = selected.realization;
  if (value->control.shape != proof.law().shape()) return std::unexpected(orbital::Error::invalid_argument);
  return tuple_lookup(*cs::law(value->control.shape), *cs::address_slot(value, 0), value->control.records, key, row, output);
}
orbital::Result<void> lookup(SeriesProof proof, cs::Selection selected,
    Key key, std::size_t& row, Record& output) noexcept {
  if (!selected) return std::unexpected(orbital::Error::absent);
  auto* value = selected.realization;
  if (value->control.shape != proof.shape()) return std::unexpected(orbital::Error::invalid_argument);
  const auto count = static_cast<std::size_t>(value->control.records);
  const auto keys = decoder<64>(*cs::address_slot(value, 0), count);
  return lookup_row(count, key, row, output,
      [&](std::size_t i) { return keys.get_unchecked(i); },
      [&](std::size_t i) { return Record{
          decoder<64>(*cs::address_slot(value, 1), count).get_unchecked(i),
          std::uint32_t(decoder<20>(*cs::address_slot(value, 2), count).get_unchecked(i)),
          std::uint32_t(decoder<32>(*cs::address_slot(value, 3), count).get_unchecked(i))}; });
}
orbital::Result<void> lookup_mapped(TupleProof proof, std::uint64_t count,
    orbital::Bytes body, Key key, std::size_t& row, Record& output) noexcept {
  if (count > SIZE_MAX / v::tuple_bytes || body.size() != count * v::tuple_bytes ||
      !compact_law::detail::valid_span(body)) return std::unexpected(orbital::Error::invalid_argument);
  return tuple_lookup(*cs::law(const_cast<cs::Shape*>(proof.law().shape())), body.data(), count, key, row, output);
}

orbital::Result<void> apply(TupleProof proof, std::uint64_t count,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    v::KernelCosts& output, ProbeCosts& output_probes) noexcept {
  try {
    const auto& law = proof.law();
    admit(law.shape(), &law, count, target, input, output, output_probes);
    auto costs = output; auto probes = output_probes; ++probes.private_admissions;
    Rows rows;
    const auto& read_law = *cs::law(const_cast<cs::Shape*>(law.shape()));
    const auto* pointer = reinterpret_cast<const tp::byte*>(target[0].bytes.data());
    resolve(count, input, rows, [&](std::size_t i) { return read_law.read_unchecked<0>(pointer + i * v::tuple_bytes); }, probes);
    tuples(law, count, target, input, rows, costs);
    probes.applied_updates += input.size(); output = costs; output_probes = probes;
    return {};
  } catch (Refusal refusal) { return std::unexpected(refusal.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
orbital::Result<void> apply(SeriesProof proof, std::uint64_t count,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
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
} // namespace orbital_mvcc::metadata_consumer::shape_write
