#include "caller.h"
#include "oracle.h"
#include "../orbital-selected-access/access.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <utility>

namespace stage2_composition {
namespace {
constexpr selected::Id object = 17;
constexpr fixture::LogicalIdentity initial{{0, object}, 10};

template<class T, class E> T take(std::expected<T, E> result) {
  assert(result);
  return std::move(*result);
}

selected::Id format(fixture::Kind kind) { return kind == fixture::Kind::columns ? 1 : 2; }

selected::Rep capture(const fixture::Completed& completed, selected::Id realization,
                      bool wait_first = false) {
  const auto description = completed->describe();
  assert(description.logical.object.high == 0);
  std::vector<selected::ExternalPart> parts;
  for (std::size_t i = 0; i < description.parts.size(); ++i) {
    const auto& part = description.parts[i];
    // The spike adapter uses ordinal IDs local to each selected realization.
    // Ikea's own Part IDs remain in its descriptor for read_parts qualification.
    parts.push_back({i + 1, part.body, part.interpretation, !wait_first || i != 0,
                     part.alignment, part.readable_padding});
  }
  return take(selected::capture({description.logical.object.low, description.logical.version},
      realization, format(description.kind), std::as_bytes(description.field_widths), parts,
      std::shared_ptr<const void>{completed}));
}

selected::Requirements requirements(const fixture::Description& description, std::size_t i) {
  const auto& part = description.parts[i];
  return {format(description.kind), 0, part.body.size(), part.alignment, part.readable_padding};
}

struct RetainedRecords {
  fixture::Description shape;
  std::vector<fixture::Part> parts;
  std::vector<selected::RetainedView> access;

  void check(const std::vector<fixture::Record>& expected) const {
    auto qualified = parts;
    assert(access.size() == qualified.size());
    for (std::size_t i = 0; i < access.size(); ++i) {
      const auto& view = access[i].view();
      // Exercise the relocated access record itself, including its retained
      // descriptor pointer, rather than only our saved adapter spans.
      assert(std::ranges::equal(view.interpretation(), std::as_bytes(shape.field_widths)));
      qualified[i].body = view.bytes();
      assert(qualified[i].body.data() == parts[i].body.data());
    }
    auto description = shape;
    description.parts = qualified;
    matches(take(fixture::read_parts(description)), expected);
  }
};

RetainedRecords read(const selected::ReadContext& context, selected::Selection selection,
                     const fixture::Representation& fixture, selected::Slot* first_wait = nullptr,
                     const selected::View* earlier_second = nullptr) {
  RetainedRecords result{fixture.describe(), {}, {}};
  for (std::size_t i = 0; i < result.shape.parts.size(); ++i) {
    const auto request = requirements(result.shape, i);
    auto view = earlier_second && i == 1 ? *earlier_second :
        first_wait && i == 0 ? take(first_wait->ready(request)) :
        take(selected::ready(selection, i + 1, request));
    // Native access adopts the actual encoded bytes; adaptation adds no copy.
    if (!(first_wait && i == 0)) assert(view.bytes().data() == result.shape.parts[i].body.data());
    auto part = result.shape.parts[i];
    part.body = view.bytes();
    part.interpretation = take(selected::part_interpretation(selection.representation(), i + 1));
    result.parts.push_back(part);
    result.access.push_back(take(selected::RetainedView::make(
        take(selected::retain(context, selection)), view)));
  }
  return result;
}

void publish(selected::Store& store, selected::Rep rep, selected::Id epoch) {
  const auto basis = store.read();
  selected::Draft draft(basis);
  const std::array logical{std::byte{0x41}};
  assert(draft.replace(std::move(rep), logical));
  selected::CommitOwner owner(store);
  auto prepared = take(owner.prepare(draft, epoch));
  assert(owner.install(prepared));
  prepared.retire();
}

void conversion_history(orbital::allocation::Pool& pool) {
  const auto expected = records(67);
  const auto changed = records(67, 1);
  fixture::Costs costs;
  auto columns = take(fixture::construct(pool, initial, schema(), expected, fixture::Kind::columns, costs));
  std::weak_ptr<const fixture::Representation> source_lifetime = columns;
  std::weak_ptr<const fixture::Representation> target_lifetime;
  const auto* source = columns.get();
  std::vector<RetainedRecords> escaped;

  {
    selected::Store store;
    selected::CommitOwner commits(store);
    publish(store, capture(columns, 101, true), 1);
    auto before = store.read();
    const auto original = take(selected::bind(before, object));
    assert(selected::describe(original.representation()).parts == 8);
    const auto early = take(selected::ready(original, 2, requirements(source->describe(), 1)));
    assert(!early.bytes().empty() && early.bytes().data() == source->describe().parts[1].body.data());
    assert(std::ranges::equal(early.interpretation(), std::as_bytes(source->describe().field_widths)));
    assert(!selected::ready(original, 1, requirements(source->describe(), 0)));
    selected::Slot pending;
    auto request = take(pending.submit(take(selected::retain(before, original)), 1, pool));
    assert(pending.outstanding() == 1);
    assert(!pending.ready(requirements(source->describe(), 0)));

    // Neither the source handle nor the capture adapter remains. The selected
    // owner protects source data AND the Ikea interpretation across the wait.
    columns.reset();
    assert(!source_lifetime.expired());

    auto next = take(fixture::construct(pool, {{0, object}, 11}, schema(), changed,
        fixture::Kind::columns, costs));
    selected::Draft logical_edit(before);
    const std::array next_logical{std::byte{0x42}};
    assert(logical_edit.replace(capture(next, 201), next_logical));
    auto prepared = take(commits.prepare(logical_edit, 2));

    // Same-version conversion changes physical structure from eight Parts to
    // one. It must not invalidate the already prepared logical predecessor.
    auto rows = take(fixture::convert(pool, *source, fixture::Kind::rows, costs));
    assert(rows->describe().logical == initial && rows->describe().parts.size() == 1);
    auto row_rep = capture(rows, 102);
    assert(store.install_representation(before, row_rep));
    const auto row_selection = take(selected::bind(before, object, row_rep));
    auto row_access = read(before, row_selection, *rows);
    row_access.check(expected);
    // Equal logical versions do not make descriptor/interpretation owners
    // interchangeable when retaining an already qualified physical View.
    assert(!selected::RetainedView::make(take(selected::retain(before, original)),
        take(selected::ready(row_selection, 1, requirements(rows->describe(), 0)))));
    target_lifetime = rows;
    rows.reset();
    escaped.push_back(std::move(row_access));
    assert(store.read().epoch() == 1);
    assert(commits.install(prepared));
    prepared.retire();
    auto after = store.read();
    assert(after.epoch() == 2 && after.logical_state(object)[0] == std::byte{0x42});
    read(after, take(selected::bind(after, object)), *next).check(changed);

    // Work captured for v10 finishes after v11 publication. The conversion
    // routine is synchronous; delayed execution models its retained input.
    auto late_rows = take(fixture::convert(pool, *source, fixture::Kind::rows, costs));
    assert(late_rows->describe().logical == initial);
    auto late_rep = capture(late_rows, 103);
    assert(store.install_representation(before, late_rep));
    assert(!store.install_representation(after, late_rep));
    assert(!selected::bind(after, object, late_rep));
    assert(!store.alternative(after, object, 103));
    auto late_selection = take(selected::bind(before, object, late_rep));
    read(before, late_selection, *late_rows).check(expected);
    const auto wrong_format = selected::Requirements{format(fixture::Kind::columns), 0,
        late_rows->describe().parts[0].body.size(), 1, 0};
    assert(!selected::ready(late_selection, 1, wrong_format));
    assert(!selected::ready(late_selection, 2, requirements(late_rows->describe(), 0)));

    // A genuinely started, private TuplePack target fails after one record.
    // There is no Completed owner to capture or make locally selectable.
    fixture::Costs failed_costs;
    auto partial = fixture::convert(pool, *source, fixture::Kind::rows, failed_costs,
        {.fail_after_units = 1});
    assert(!partial && partial.error() == fixture::Error::injected);
    assert(failed_costs.completed_units == 1 && failed_costs.allocations == 1);
    assert(!store.alternative(before, object, 104));
    assert(store.read().epoch() == 2);

    // Completion, owner receipt and physical retirement remain separate. The
    // resumed column binding still reads v10 after both physical/logical changes.
    assert(selected::finish(request));
    assert(!pending.ready(requirements(source->describe(), 0)));
    assert(selected::settle(request));
    // Column 2 was qualified before waiting. Columns 3..8 are discovered only
    // now under the same original descriptor, not the current v11 selection.
    auto old_access = read(before, original, *source, &pending, &early);
    old_access.check(expected);
    escaped.push_back(std::move(old_access));
    pending.cancel();
    assert(pending.outstanding() == 1 && !selected::physically_retired(request));
    assert(selected::retire(request));
    assert(pending.outstanding() == 0 && selected::physically_retired(request));
    request.reset();
    store.forget_alternatives();
  }

  // ReadContext, Store, Slot, original Completed and Rep handles are all gone.
  // Move retained access again to expose accidental pointers into former owners.
  auto moved = std::move(escaped);
  for (auto& access : moved) {
    std::vector<selected::RetainedView> relocated;
    relocated.reserve(access.access.size());
    for (auto& view : access.access) relocated.push_back(std::move(view));
    access.access = std::move(relocated);
  }
  assert(!source_lifetime.expired() && !target_lifetime.expired());
  for (const auto& access : moved) access.check(expected);
  moved.pop_back(); // Resolved row access need not keep the old column closure.
  assert(source_lifetime.expired() && !target_lifetime.expired());
  moved.front().check(expected);
  moved.clear();
  assert(source_lifetime.expired() && target_lifetime.expired());
}

void boundary_history(orbital::allocation::Pool& pool, std::size_t count) {
  const auto expected = records(count);
  fixture::Costs costs;
  auto columns = take(fixture::construct(pool, initial, schema(), expected, fixture::Kind::columns, costs));
  auto rows = take(fixture::convert(pool, *columns, fixture::Kind::rows, costs));
  assert(columns->describe().parts.size() == 8 && rows->describe().parts.size() == 1);
  if (!count) assert(costs.allocations == 0);
  selected::Store store;
  publish(store, capture(columns, 301), 1);
  auto context = store.read();
  read(context, take(selected::bind(context, object)), *columns).check(expected);
  auto row_rep = capture(rows, 302);
  assert(store.install_representation(context, row_rep));
  read(context, take(selected::bind(context, object, row_rep)), *rows).check(expected);
  auto rebuilt = take(fixture::convert(pool, *rows, fixture::Kind::columns, costs));
  assert(rebuilt->describe().logical == initial && rebuilt->describe().parts.size() == 8);
  auto rebuilt_rep = capture(rebuilt, 303);
  assert(store.install_representation(context, rebuilt_rep));
  read(context, take(selected::bind(context, object, rebuilt_rep)), *rebuilt).check(expected);
  if (!count) assert(costs.allocations == 0);
}
} // namespace

void histories(orbital::allocation::Pool& pool) {
  boundary_history(pool, 0);
  boundary_history(pool, 1);
  conversion_history(pool);
}
} // namespace stage2_composition
