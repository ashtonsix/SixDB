#include "access.h"

#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace selected;
namespace allocation = orbital::allocation;

void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "selected-access check: %s\n", message);
    std::abort();
  }
}

template<class T> T take(Result<T> result) {
  check(result.has_value(), "unexpected operation failure");
  return std::move(*result);
}

template<class T> void denied(const Result<T>& result, Error expected) {
  check(!result && result.error() == expected, "wrong refusal");
}

Bytes bytes(std::string_view text) {
  return std::as_bytes(std::span(text.data(), text.size()));
}

void equals(Bytes actual, std::string_view expected) {
  check(actual.size() == expected.size() &&
      (actual.empty() || std::memcmp(actual.data(), expected.data(), actual.size()) == 0),
      "independent expected bytes differ");
}

struct Fixture {
  orbital::Ref<allocation::Arena> arena;
  orbital::worker::Lane lane;
  allocation::Pool* pool{};

  explicit Fixture(std::uint64_t limit = 4 * 1024 * 1024) {
    auto opened = allocation::open_arena(limit);
    check(opened.has_value(), "Arena startup");
    arena = std::move(*opened);
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption");
    auto added = allocation::add_pool(*arena, lane);
    check(added.has_value(), "Pool startup");
    pool = *added;
  }

  void trim() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (allocation::trim_cold(*pool, 0).kind() != orbital::Progress::Kind::done) {
      check(std::chrono::steady_clock::now() < deadline, "trim did not drain");
      orbital::worker::poll(lane, 16);
      std::this_thread::yield();
    }
  }

  ~Fixture() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (allocation::close(*pool).kind() != orbital::Progress::Kind::done) {
      check(std::chrono::steady_clock::now() < deadline, "Pool close did not drain");
      orbital::worker::poll(lane, 16);
      std::this_thread::yield();
    }
    check(arena->charged_bytes.load() == 0, "shutdown retained physical charge");
    check(allocation::remove_pool(*pool).has_value(), "Pool removal");
    orbital::worker::relinquish(lane);
  }
};

Rep make_rep(allocation::Pool& pool, Key key, Id realization, Id format,
    std::string_view interpretation, std::initializer_list<std::string_view> columns) {
  std::vector<PartInput> input;
  for (auto text : columns) input.push_back({input.size(), bytes(text), true, 64, 32});
  auto builder = take(Builder::make(pool, key, realization, format, bytes(interpretation), input));
  return take(builder.freeze());
}

void publish(Store& store, Rep rep, std::string_view logical, Id epoch) {
  Draft draft(store.read());
  check(draft.replace(std::move(rep), bytes(logical)).has_value(), "private replacement");
  CommitOwner owner(store);
  auto prepared = take(owner.prepare(draft, epoch));
  check(owner.install(prepared).has_value(), "logical installation");
  prepared.retire();
}

void conversion_history() {
  Fixture f;
  Store store;
  publish(store, make_rep(*f.pool, {7, 11}, 101, 11, "columns-v1",
      {"A0A1", "B0B1", "C0C1"}), "L1", 1);
  auto old = store.read();
  auto columns = take(bind(old, 7));
  equals(take(ready(columns, 0, {11, 0, 4, 64, 32})).bytes(), "A0A1");
  Slot waiting_column;
  auto waiting = take(waiting_column.submit(take(retain(old, columns)), 1, *f.pool));

  auto newer = make_rep(*f.pool, {7, 12}, 102, 11, "columns-v2",
      {"N0N1", "O0O1", "P0P1"});
  Draft draft(old), competitor(old);
  check(draft.replace(newer, bytes("L2")).has_value(), "new private version");
  check(competitor.replace(make_rep(*f.pool, {7, 13}, 103, 11,
      "unpublished", {"XXXX"}), bytes("LOSER")).has_value(), "competing private version");
  CommitOwner owner(store);
  auto prepared = take(owner.prepare(draft, 2));
  auto stale = take(owner.prepare(competitor, 3));
  equals(store.read().logical_state(7), "L1");

  auto rows = make_rep(*f.pool, {7, 11}, 201, 22, "rows-v1", {"A0B0C0A1B1C1"});
  check(store.install_representation(old, rows).has_value(), "same-version conversion");
  check(store.read().epoch() == 1, "conversion changed logical epoch");
  auto captured_rows = take(store.alternative(old, 7, 201));
  auto row_selection = take(bind(old, 7, captured_rows));
  equals(take(ready(row_selection, 0, {22, 0, 12})).bytes(), "A0B0C0A1B1C1");
  denied(ready(row_selection, 2, {22, 0, 4}), Error::absent);
  check(owner.install(prepared).has_value(), "physical installation invalidated logical basis");
  denied(owner.install(prepared), Error::busy);
  denied(owner.install(stale), Error::stale);
  prepared.retire(); stale.retire();

  auto current = store.read();
  equals(current.logical_state(7), "L2");
  equals(take(ready(take(bind(current, 7)), 2, {11, 0, 4})).bytes(), "P0P1");
  denied(store.alternative(current, 7, 201), Error::absent);
  check(!bind(current, 7, rows), "old physical form selected at a newer logical version");

  // An old conversion completes after publication. Its target remains v11.
  auto late = make_rep(*f.pool, {7, 11}, 202, 22, "late-rows-v1", {"A0B0C0A1B1C1"});
  check(store.install_representation(old, late).has_value(), "late old-version conversion");
  check(store.alternative(old, 7, 202).has_value(), "old context lost late conversion");
  denied(store.alternative(current, 7, 202), Error::absent);
  equals(old.logical_state(7), "L1");
  check(finish(waiting).has_value() && settle(waiting), "column read did not resume");
  equals(take(waiting_column.ready({11, 0, 4})).bytes(), "B0B1");
  equals(take(ready(columns, 2, {11, 0, 4})).bytes(), "C0C1");
  equals(take(ready(columns, 1, {11, 0, 4})).interpretation(), "columns-v1");
  check(retire(waiting), "column read retirement");
  waiting_column.cancel();
  auto immediate = store.borrow();
  auto immediate_selection = take(bind(immediate, 7));
  auto immediate_view = take(ready(immediate_selection, 0, {11, 0, 4}));
  equals(immediate_view.bytes(), "N0N1");
  auto kept = take(RetainedView::make(take(retain(immediate, immediate_selection)), immediate_view));
  publish(store, make_rep(*f.pool, {7, 13}, 104, 11, "columns-v3",
      {"Q0Q1", "R0R1", "S0S1"}), "L3", 3);
  immediate = {};
  equals(kept.view().bytes(), "N0N1");
  equals(kept.view().interpretation(), "columns-v2");
}

struct ExternalOwner {
  allocation::Allocation payload;
  std::string interpretation{"external-schema-v1"};
  std::shared_ptr<const std::string> decoder = std::make_shared<const std::string>("decoder-v1");
};

struct External {
  Rep rep;
  std::weak_ptr<const ExternalOwner> owner;
  std::weak_ptr<const std::string> decoder;
  const std::byte* original{};
};

External make_external(allocation::Pool& pool, Key key, Id realization, Id format,
    std::string_view body, bool native = true, std::size_t size = 0) {
  auto closure = std::make_shared<ExternalOwner>();
  auto storage = allocation::allocate(pool, {size ? size : body.size(), 0, 64, 32});
  check(storage.has_value(), "external source allocation");
  closure->payload = std::move(*storage);
  std::memset(closure->payload.address, 0x5b, closure->payload.size);
  std::memcpy(closure->payload.address, body.data(), body.size());
  const std::array input{ExternalPart{0, allocation::bytes(closure->payload),
      bytes(*closure->decoder), native, 64, 32}};
  auto rep = take(capture(key, realization, format, bytes(closure->interpretation), input, closure));
  return {std::move(rep), closure, closure->decoder, closure->payload.address};
}

Retained isolated_selection(Rep rep) {
  Store store;
  publish(store, std::move(rep), "retained-logical-v1", 1);
  auto context = store.read();
  return take(retain(context, take(bind(context, 7))));
}

void frozen_and_external_access() {
  Fixture f;
  std::optional<WriteView> surviving_writer;
  {
    const std::array original{PartInput{0, bytes("TEMP"), true, 64, 32}};
    auto abandoned = take(Builder::make(*f.pool, {7, 1}, 1, 31, {}, original));
    auto writer = take(abandoned.write(0));
    surviving_writer.emplace(std::move(writer));
    check(writer.bytes().empty(), "moved writer retained borrowed bytes");
    auto replacement = take(Builder::make(*f.pool, {7, 2}, 2, 31, {}, original));
    abandoned = std::move(replacement);
  }
  std::memcpy(surviving_writer->bytes().data(), "KEEP", 4);
  equals(surviving_writer->bytes(), "KEEP");
  surviving_writer.reset();
  f.trim();
  check(f.arena->charged_bytes.load() == 0, "last writer did not release private storage");

  std::string input = "INIT", metadata = "frozen-schema";
  const std::array parts{PartInput{0, bytes(input), true, 64, 32}};
  auto builder = take(Builder::make(*f.pool, {7, 1}, 1, 31, bytes(metadata), parts));
  input.assign("JUNK"); metadata.assign("wrong-schema!");
  {
    auto writer = take(builder.write(0));
    std::memcpy(writer.bytes().data(), "EDIT", 4);
    denied(builder.freeze(), Error::busy);
  }
  auto frozen = take(builder.freeze());
  denied(builder.freeze(), Error::stale);
  Store frozen_store;
  publish(frozen_store, frozen, "FROZEN", 1);
  auto context = frozen_store.read();
  auto selection = take(bind(context, 7));
  equals(take(ready(selection, 0, {31, 0, 4})).bytes(), "EDIT");
  equals(take(ready(selection, 0, {31, 0, 4})).interpretation(), "frozen-schema");
  denied(ready(selection, 0, {99, 0, 4}), Error::invalid);
  denied(ready(selection, 0, {31, 3, 2}), Error::invalid);
  denied(ready(selection, 0, {31, 1, 1, 64}), Error::invalid);
  denied(ready(selection, 0, {31, 0, 4, 64, 33}), Error::invalid);

  std::optional<RetainedView> escaped;
  Bytes borrowed_decoder;
  auto external = make_external(*f.pool, {7, 2}, 2, 31, "UVWX");
  {
    Store store;
    publish(store, external.rep, "EXTERNAL", 1);
    auto read = store.read();
    const auto references = external.rep.use_count();
    auto borrowed = take(bind(read, 7));
    auto view = take(ready(borrowed, 0, {31, 0, 4, 64, 32}));
    borrowed_decoder = take(part_interpretation(borrowed.representation(), 0));
    const std::array hostile{ExternalPart{0, view.bytes(), borrowed_decoder,
        true, 64, std::numeric_limits<std::size_t>::max()}};
    denied(capture({7, 2}, 9, 31, {}, hostile, external.owner.lock()), Error::invalid);
    check(view.bytes().data() == external.original, "capture copied external pack bytes");
    check(external.rep.use_count() == references, "ordinary borrow retained independently");
    escaped.emplace(take(RetainedView::make(take(retain(read, borrowed)), view)));
    external.rep.reset();
  }
  check(!external.owner.expired() && !external.decoder.expired(), "escape dropped parent closure");
  equals(escaped->view().bytes(), "UVWX");
  equals(escaped->view().interpretation(), "external-schema-v1");
  equals(borrowed_decoder, "decoder-v1");
  // The decoder span is borrowed from the parent closure, not a process-global fixture.
  escaped.reset();
  check(external.owner.expired() && external.decoder.expired(), "escaped closure failed to retire");

  auto empty_metadata = std::make_shared<const std::string>("empty-schema");
  const std::array empty_part{ExternalPart{0, {}, bytes(*empty_metadata), true, 1, 0}};
  auto empty = take(capture({8, 1}, 1, 31, bytes(*empty_metadata), empty_part, empty_metadata));
  empty_metadata.reset();
  Store empty_store;
  publish(empty_store, empty, "EMPTY", 1);
  auto empty_context = empty_store.read();
  auto empty_selection = take(bind(empty_context, 8));
  check(take(ready(empty_selection, 0, {31, 0, 0})).bytes().empty(), "zero-body access");
  equals(take(ready(empty_selection, 0, {31, 0, 0})).interpretation(), "empty-schema");
  equals(take(part_interpretation(empty_selection.representation(), 0)), "empty-schema");
  denied(ready(empty_selection, 0, {31, 0, 0, 1, 1}), Error::invalid);
}

void narrow_retention() {
  Fixture f;
  auto a = make_external(*f.pool, {7, 1}, 1, 61, "ONLY");
  auto b = make_external(*f.pool, {8, 1}, 1, 61, "GONE");
  std::optional<RetainedView> retained_a;
  {
    Store store;
    auto replace_both = [&](Rep first, Rep second, Id epoch) {
      Draft draft(store.read());
      check(draft.replace(std::move(first), bytes("A-state")).has_value(), "Object A private edit");
      check(draft.replace(std::move(second), bytes("B-state")).has_value(), "Object B private edit");
      CommitOwner owner(store);
      auto prepared = take(owner.prepare(draft, epoch));
      check(owner.install(prepared).has_value(), "two-Object installation");
      prepared.retire();
    };
    replace_both(a.rep, b.rep, 1);
    auto old = store.read();
    auto selected_a = take(bind(old, 7));
    retained_a.emplace(take(RetainedView::make(take(retain(old, selected_a)),
        take(ready(selected_a, 0, {61, 0, 4})))));
    {
      Store other;
      publish(other, a.rep, "foreign-Store", 1);
      auto foreign = other.read();
      auto alternative = make_rep(*f.pool, {7, 1}, 9, 62, "other-layout", {"ONLY"});
      denied(store.install_representation(foreign, alternative), Error::invalid);
      denied(store.alternative(foreign, 7, 9), Error::invalid);
    }
    replace_both(make_rep(*f.pool, {7, 2}, 2, 61, "new-A", {"NEXT"}),
        make_rep(*f.pool, {8, 2}, 2, 61, "new-B", {"NEXT"}), 2);
    a.rep.reset(); b.rep.reset(); old = {};
  }
  check(b.owner.expired() && b.decoder.expired(), "retained A kept unrelated old Object B");
  check(!a.owner.expired(), "narrow retention lost Object A");
  equals(retained_a->view().bytes(), "ONLY");
  equals(retained_a->view().interpretation(), "external-schema-v1");
  retained_a.reset();
  check(a.owner.expired() && a.decoder.expired(), "narrow retention leaked Object A");

  // Reuse the exact Store address: an old context still belongs to its old incarnation.
  std::optional<Store> recycled;
  recycled.emplace();
  publish(*recycled, make_rep(*f.pool, {7, 1}, 1, 61, "old-Store", {"OLD!"}), "OLD", 1);
  auto old_store_context = recycled->read();
  auto* address = &*recycled;
  recycled.reset(); recycled.emplace();
  check(&*recycled == address, "Store incarnation fixture did not reuse its address");
  publish(*recycled, make_rep(*f.pool, {7, 1}, 1, 61, "fresh-Store", {"NEW!"}), "NEW", 1);
  auto alternative = make_rep(*f.pool, {7, 1}, 9, 62, "old-conversion", {"OLD!"});
  denied(recycled->install_representation(old_store_context, alternative), Error::invalid);
  denied(recycled->alternative(old_store_context, 7, 9), Error::invalid);
  equals(take(ready(take(bind(old_store_context, 7)), 0, {61, 0, 4})).bytes(), "OLD!");
  alternative.reset();

  auto fresh_context = recycled->read();
  f.trim();
  const auto baseline = f.arena->charged_bytes.load();
  for (unsigned round = 0; round != 3; ++round) {
    auto converted = make_external(*f.pool, {7, 1}, 20 + round, 62, "ALT!", false, 64 * 1024);
    check(recycled->install_representation(fresh_context, converted.rep).has_value(), "alternative installation");
    converted.rep.reset();
    check(!converted.owner.expired(), "installed alternative lost its closure");
    recycled->forget_alternatives();
    check(converted.owner.expired() && converted.decoder.expired(), "forgotten alternative kept hidden ownership");
    f.trim();
    check(f.arena->charged_bytes.load() == baseline, "alternative release did not restore charge baseline");
  }
}

void transfer_history() {
  Fixture f;
  Slot slot;
  auto old = make_external(*f.pool, {7, 1}, 1, 41, "OLD!", false);
  auto next = make_external(*f.pool, {7, 2}, 2, 41, "NEW?", false);
  auto first = take(slot.submit(isolated_selection(old.rep), 0, *f.pool));
  auto next_selection = isolated_selection(next.rep);
  auto second = take(slot.submit(next_selection, 0, *f.pool));
  old.rep.reset(); next.rep.reset();
  check(slot.outstanding() == 2, "submission debt not counted");
  check(!retire(first) && !settle(first), "pending backend ownership retired");
  check(finish(second).has_value(), "newer physical completion");
  denied(slot.ready({41, 0, 4}), Error::unavailable);
  check(!physically_retired(second) && !retire(second), "finish replaced owner receipt");
  check(settle(second), "owner receipt");
  equals(take(slot.ready({41, 0, 4})).bytes(), "NEW?");
  auto escaped = take(RetainedView::make(next_selection, take(slot.ready({41, 0, 4}))));
  std::optional<RetainedView> moved(std::move(escaped));
  // A second retention after move detects a View that still points at the old buffer handle.
  auto moved_again = take(RetainedView::make(next_selection, moved->view()));
  moved.emplace(std::move(moved_again));
  next_selection = {};

  denied(finish(first, true, 2), Error::io);
  check(settle(first), "failed old receipt was not consumed");
  equals(take(slot.ready({41, 0, 4})).bytes(), "NEW?");
  check(!old.owner.expired() && !old.decoder.expired(), "failure released backend dependencies early");
  check(!settle(first), "duplicate owner receipt");
  denied(finish(first), Error::stale);
  check(retire(first) && physically_retired(first), "old physical retirement");
  check(!retire(first) && slot.outstanding() == 1, "duplicate retirement changed debt");
  check(old.owner.expired() && old.decoder.expired(), "retired old source remained owned");
  check(retire(second) && !retire(second), "new physical retirement");
  check(slot.outstanding() == 0, "receipt retirement left debt");
  equals(take(slot.ready({41, 0, 4})).bytes(), "NEW?");
  slot.cancel();
  denied(slot.ready({41, 0, 4}), Error::unavailable);
  check(!next.owner.expired(), "moved buffered escape lost selected closure");
  equals(moved->view().bytes(), "NEW?");
  equals(moved->view().interpretation(), "external-schema-v1");
  moved.reset();
  check(next.owner.expired(), "cancel retained settled selection");

  // A live old completion after cancellation cannot create ready access.
  auto cancelled = make_external(*f.pool, {7, 3}, 3, 41, "LATE", false);
  auto request = take(slot.submit(isolated_selection(cancelled.rep), 0, *f.pool));
  cancelled.rep.reset(); slot.cancel();
  check(finish(request).has_value() && settle(request), "late cancelled completion");
  denied(slot.ready({41, 0, 4}), Error::unavailable);
  check(!cancelled.owner.expired(), "cancel retired backend source");
  check(retire(request) && cancelled.owner.expired(), "cancelled physical use did not retire");

  Request orphan;
  auto departed = make_external(*f.pool, {7, 4}, 4, 41, "DEAD", false);
  {
    Slot owner;
    orphan = take(owner.submit(isolated_selection(departed.rep), 0, *f.pool));
    departed.rep.reset();
  }
  check(!departed.owner.expired(), "owner death dropped actual backend use");
  check(finish(orphan).has_value() && settle(orphan) && retire(orphan), "orphan retirement");
  check(departed.owner.expired() && departed.decoder.expired(), "orphan closure leak");
}

void bounded_pressure() {
  Fixture f(256 * 1024);
  auto source = make_external(*f.pool, {7, 1}, 1, 51, "DATA", false, 64 * 1024);
  auto selected = isolated_selection(source.rep);
  source.rep.reset();
  f.trim();
  const auto baseline = f.arena->charged_bytes.load();
  Slot slot;
  for (unsigned round = 0; round != 3; ++round) {
    std::vector<Request> requests;
    for (unsigned count = 0; count != 16; ++count) {
      auto request = slot.submit(selected, 0, *f.pool);
      if (!request) { denied(request, Error::capacity); break; }
      requests.push_back(std::move(*request));
    }
    check(!requests.empty() && requests.size() < 16, "small budget did not exercise refusal");
    check(slot.outstanding() == requests.size(), "refusal changed outstanding debt");
    slot.cancel();
    for (auto& request : requests) {
      denied(finish(request, true, 3), Error::io);
      check(!retire(request), "failed completion retired before receipt");
    }
    f.trim();
    check(f.arena->charged_bytes.load() > baseline &&
        f.arena->charged_bytes.load() <= f.arena->byte_limit, "pending physical buffers not charged");
    denied(slot.ready({51, 0, 4}), Error::unavailable);
    for (auto& request : requests) check(settle(request) && retire(request), "bounded retirement");
    check(slot.outstanding() == 0, "bounded round leaked debt");
    f.trim();
    check(f.arena->charged_bytes.load() == baseline, "release/drain did not recover the budget");
  }

  auto ready_request = take(slot.submit(selected, 0, *f.pool));
  check(finish(ready_request).has_value() && settle(ready_request) && retire(ready_request), "ready target preparation");
  equals(take(slot.ready({51, 0, 4})).bytes(), "DATA");
  std::vector<allocation::Allocation> fillers;
  for (unsigned count = 0; count != 16; ++count) {
    auto filler = allocation::allocate(*f.pool, {64 * 1024, 0, 64, 32,
        allocation::Initialization::zero});
    if (!filler) break;
    fillers.push_back(std::move(*filler));
  }
  check(!fillers.empty() && fillers.size() < 16, "ready-slot pressure did not exhaust the budget");
  denied(slot.submit(selected, 0, *f.pool), Error::capacity);
  check(slot.outstanding() == 0, "refused submission created physical debt");
  equals(take(slot.ready({51, 0, 4})).bytes(), "DATA");
  fillers.clear(); slot.cancel(); f.trim();
  check(f.arena->charged_bytes.load() == baseline, "ready-slot pressure did not recover baseline");
  selected = {};
  check(source.owner.expired() && source.decoder.expired(), "bounded source failed to retire");
  f.trim();
  check(f.arena->charged_bytes.load() == 0, "small-budget drain retained charge");
}
} // namespace

int main() {
  conversion_history();
  frozen_and_external_access();
  narrow_retention();
  transfer_history();
  bounded_pressure();
  std::puts("selected-access: 5 independent histories passed");
}
