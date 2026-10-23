#include "write_batch.h"
#if CONSUMER_METADATA_FROZEN
#include "workbench/benchmarks/orbital/versioned/native.h"
#else
#include "consumer/native.h"
#endif
#include <ikea/tuplepack/author/execution.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <new>
#include <utility>

namespace orbital_mvcc::metadata_consumer::write_batch {
namespace {
namespace tp = ikea::tuplepack;
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

v::ReadData admit(const v::Schema& schema, const v::Shape& shape,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    v::KernelCosts& costs, ProbeCosts& probes) {
  require(!input.empty() && input.size() <= v::write_batch_limit);
  require(shape.format == v::Format::rows || shape.format == v::Format::columns);
  const unsigned count = shape.format == v::Format::rows ? 1 : 4;
  require(shape.part_count == count && target.size() == count);

  // Known metadata/command/output extents cannot be clobbered by the private
  // writes. Ordinary readable memory and private authority remain caller facts.
  std::array<Range, 11> protected_ranges{
      range(&schema, sizeof(schema)), range(&shape, sizeof(shape)),
      range(input.data(), input.size_bytes()), range(target.data(), target.size_bytes()),
      range(&costs, sizeof(costs)), range(&probes, sizeof(probes)),
      range(shape.interpretation.data(), shape.interpretation.size())};
  for (unsigned i = 0; i < 4; ++i)
    protected_ranges[7 + i] = range(shape.parts[i].interpretation.data(),
                                    shape.parts[i].interpretation.size());
  for (unsigned i = 0; i < protected_ranges.size(); ++i)
    for (unsigned j = 0; j < i; ++j) require(!overlaps(protected_ranges[i], protected_ranges[j]));

  std::array<Range, 4> bodies{};
  std::array<v::ReadPart, 4> readable{};
  for (unsigned i = 0; i < count; ++i) {
    const auto& declared = shape.parts[i];
    require(declared.readable_padding == 0 &&
        declared.alignment == (shape.format == v::Format::rows ? 1u : 64u));
    require(target[i].id == declared.id && target[i].bytes.size() == declared.body_bytes);
    bodies[i] = range(target[i].bytes.data(), target[i].bytes.size());
    require(target[i].bytes.empty() || bodies[i].first % declared.alignment == 0);
    for (unsigned j = 0; j < i; ++j) require(!overlaps(bodies[i], bodies[j]));
    for (const auto metadata : protected_ranges) require(!overlaps(bodies[i], metadata));
    readable[i] = {target[i].id, target[i].bytes, declared.interpretation};
  }
  for (unsigned i = count; i < 4; ++i) {
    const auto& unused = shape.parts[i];
    require(unused.id == orbital::PartId{} && !unused.body_bytes && unused.alignment == 1 &&
        !unused.readable_padding && unused.interpretation.empty());
  }
  // This real cold codec validator proves canonical parent and all supplied
  // Part descriptors, exact extents, placements and native reader admission.
  // Shape's declared geometry is compared above; no Corpus fact substitutes.
  auto source = take(v::ReadData::bind(schema, orbital::Bytes{shape.interpretation},
                                      std::span(readable).first(count)));
  require(source.size() == shape.records && source.format() == shape.format);
  return source; // Borrows backing/Schema, never this local ReadPart array.
}

using Rows = std::array<std::size_t, v::write_batch_limit>;
void resolve(const v::ReadData& source, std::span<const v::Update> input,
             std::span<std::size_t> rows, bool full_row, ProbeCosts& probes) {
  const auto key = [&](std::size_t row) {
    ++probes.key_reads;
    return source.field_unchecked(v::Field::key, row);
  };
  for (std::size_t i = 0; i < input.size(); ++i) {
    // Preserve the consumer's offered-order value/key error precedence while
    // finishing the entire batch preflight before any destination store.
    require(input[i].record.value <= orbital_bench::value_mask);
    rows[i] = v::native_detail::seek(source.size(), input[i].key, key);
    require(rows[i] < source.size(), orbital::Error::absent);
    std::uint64_t found;
    if (full_row) {
      found = source.row_unchecked(rows[i]).key;
      ++probes.full_rows_read; ++probes.key_reads;
    } else found = key(rows[i]);
    require(found == input[i].key, orbital::Error::absent);
  }
}

template<class View>
void account(const ikea::journal& effects, const View& view,
             std::size_t extent, v::KernelCosts& costs) {
  costs.issued_spans += effects.used;
  for (const auto& effect : effects.entries()) {
    // Trusted post-admission continuation: these are invariant checks, never
    // a fallible public return after stores have begun. Resolve before closure.
    assert(effect.source == &view && effect.plane == 0 &&
        effect.offset <= extent && effect.size <= extent - effect.offset);
    costs.issued_bytes += effect.size;
  }
}
template<unsigned K>
auto column_view(std::size_t records, orbital::MutableBytes bytes) {
  std::array<sp::plane<std::byte>, 3> planes{};
  planes[0] = {{reinterpret_cast<std::byte*>(bytes.data()), bytes.size()}, v::Column<K>::tile_bytes};
  return take(sp::view<v::Column<K>>::attach(records, planes));
}
struct Columns {
  sp::view<v::Column<64>> key, next;
  sp::view<v::Column<20>> value;
  sp::view<v::Column<32>> tag;
};

void tuples(const v::Schema& schema, const v::Shape& shape,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    const Rows& rows, v::KernelCosts& costs) {
  // Named stationary view; returned/moved endpoint bundles would be unsafe.
  const auto view = take(tp::view::attach(schema.tuple,
      {reinterpret_cast<std::byte*>(target[0].bytes.data()), target[0].bytes.size()},
      shape.records, v::tuple_bytes));
  const tp::author::native_writer next{take(tp::bind_writer(schema.writers[1], view))};
  const tp::author::native_writer value{take(tp::bind_writer(schema.writers[2], view))};
  const tp::author::native_writer tag{take(tp::bind_writer(schema.writers[3], view))};
  std::array<ikea::journal::entry, 4> journal;
  for (std::size_t i = 0; i < input.size(); ++i)
    require(take(next.effect_capacity(rows[i], 1)) + take(value.effect_capacity(rows[i], 1)) +
        take(tag.effect_capacity(rows[i], 1)) <= journal.size());
  for (const auto& update : input)
    require(next.accepts(update.record.next) && value.accepts(update.record.value) && tag.accepts(update.record.tag));
  costs.bindings += 3;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::journal effects{journal};
    // Full batch value/range/capacity admission precedes the first store.
    next.set_unchecked(rows[i], input[i].record.next, effects);
    value.set_unchecked(rows[i], std::uint64_t(input[i].record.value), effects);
    tag.set_unchecked(rows[i], std::uint64_t(input[i].record.tag), effects);
    account(effects, view, view.storage().size(), costs);
    costs.encoded_values += 3;
  }
}

void columns(const v::Shape& shape, std::span<const v::WritePart> target,
    std::span<const v::Update> input, const Rows& rows, v::KernelCosts& costs) {
  const Columns views{column_view<64>(shape.records, target[0].bytes),
      column_view<64>(shape.records, target[1].bytes),
      column_view<20>(shape.records, target[2].bytes),
      column_view<32>(shape.records, target[3].bytes)};
  const auto next = take(sp::bind_writer(views.next));
  const auto value = take(sp::bind_writer(views.value));
  const auto tag = take(sp::bind_writer(views.tag));
  std::array<ikea::journal::entry, 4> journal;
  for (std::size_t i = 0; i < input.size(); ++i) {
    const auto needed = take(next.effect_capacity(rows[i], 1, sp::selection::all())) +
        take(value.effect_capacity(rows[i], 1, sp::selection::all())) +
        take(tag.effect_capacity(rows[i], 1, sp::selection::all()));
    require(needed <= journal.size());
  }
  costs.bindings += 3;
  costs.scratch_bound_bytes = std::max(costs.scratch_bound_bytes, sizeof(journal));
  sp::author::no_summary summary;
  for (std::size_t i = 0; i < input.size(); ++i) {
    ikea::journal effects{journal};
    // The exact trusted continuation of prepared_mutation::set, preserving
    // the same point kernel/effect walk rather than switching to a range path.
    sp::author::replace_point_unchecked(next.expression(), rows[i], input[i].record.next, summary, effects);
    account(effects, views.next, views.next.stream(0).bytes.size(), costs); effects.used = 0;
    sp::author::replace_point_unchecked(value.expression(), rows[i], std::uint64_t(input[i].record.value), summary, effects);
    account(effects, views.value, views.value.stream(0).bytes.size(), costs); effects.used = 0;
    sp::author::replace_point_unchecked(tag.expression(), rows[i], std::uint64_t(input[i].record.tag), summary, effects);
    account(effects, views.tag, views.tag.stream(0).bytes.size(), costs);
    costs.encoded_values += 3;
  }
}
} // namespace

orbital::Result<void> apply(const v::Schema& schema, const v::Shape& shape,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    v::KernelCosts& output, ProbeCosts& output_probes) {
  try {
    const auto source = admit(schema, shape, target, input, output, output_probes);
    auto costs = output; auto probes = output_probes;
    ++probes.private_admissions;
    Rows rows;
    resolve(source, input, std::span(rows).first(input.size()), false, probes);
    if (shape.format == v::Format::rows) tuples(schema, shape, target, input, rows, costs);
    else columns(shape, target, input, rows, costs);
    probes.applied_updates += input.size();
    output = costs; output_probes = probes;
    return {};
  } catch (Refusal refusal) { return std::unexpected(refusal.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}

orbital::Result<void> generic_control(const v::Schema& schema, const v::Shape& shape,
    std::span<const v::WritePart> target, std::span<const v::Update> input,
    v::KernelCosts& output, ProbeCosts& output_probes) {
  try {
    const auto source = admit(schema, shape, target, input, output, output_probes);
    auto costs = output; auto probes = output_probes;
    ++probes.private_admissions;
    Rows rows;
    resolve(source, input, std::span(rows).first(input.size()), true, probes);
    // Immutable Schema/Shape, isolated targets and admitted rows/values make
    // generic per-row checks deterministic. Unexpected failure is an invariant
    // violation, not a returned partial-write error from this batch interface.
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto result = v::update_into(schema, shape, target, rows[i], input[i].record, costs);
      assert(result); if (!result) std::unreachable();
    }
    probes.applied_updates += input.size();
    output = costs; output_probes = probes;
    return {};
  } catch (Refusal refusal) { return std::unexpected(refusal.error); }
    catch (const std::bad_alloc&) { return std::unexpected(orbital::Error::capacity); }
}
} // namespace orbital_mvcc::metadata_consumer::write_batch
