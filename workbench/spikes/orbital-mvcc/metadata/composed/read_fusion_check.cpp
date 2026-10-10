#include "backend.h"
#include "../../performance/oracle.h"
#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
using namespace orbital_bench;
using HistoryStore = orbital_mvcc::composed::Catalog;

template<class T> void check(const T& test, std::source_location at = std::source_location::current()) {
  if (static_cast<bool>(test)) return;
  std::fprintf(stderr, "check failed: %s:%u\n", at.file_name(), at.line()); std::abort();
}
std::uint64_t comparisons{}, refusals{};
void exercise(const c::Inline& owner, const c::Inline::Scope& scope, std::size_t object,
    const p::Golden& expected) {
  const auto id = owner.corpus().images[object].address.object;
  const auto run = [&](Operation op) {
    Outcome output{41, 43, 47};
    check(c::InlineReadFusion::consume(owner, scope, id, op, output));
    check(output == p::oracle(expected, op));
    ++comparisons;
  };
  for (const auto& [key, record] : expected) { (void)record; run({.kind=Kind::read, .key=key, .record={}}); }
  check(!expected.contains(2));
  run({.kind=Kind::read, .key=2, .record={}});
  for (unsigned count : {0, 1, 16}) run({.kind=Kind::chain, .key=expected.begin()->first, .record={}, .count=count});
  run({.kind=Kind::scan, .key=expected.begin()->first, .record={}, .count=16});
}
void run(v::Format initial) {
  p::Corpus corpus(512, 8, 8143);
  constexpr std::uint64_t factor = UINT64_C(0x9e3779b97f4a7c15), hash = 39;
  // Distinct full opaque identities deliberately collide. No ordinal lookup is
  // sufficient; a deleted bucket must not hide later entries in the chain.
  for (std::size_t i=0; i<corpus.images.size(); ++i)
    corpus.images[i].address.object = {hash ^ ((i+1)*factor), i+1};
  const orbital::ObjectId missing{hash ^ (999*factor), 999};
  std::array<p::Golden, 8> original;
  for (std::size_t i=0; i<original.size(); ++i)
    for (const auto& row : corpus.images[i].rows) original[i].emplace(row.key, row.record);
  auto expected=original;
  c::Inline owner(corpus, 64*1024*1024, initial);
  std::optional<c::Inline::Scope> unseen(std::in_place, owner.catalog());
  const auto old_cut=unseen->cut();
  const auto opposite=initial==v::Format::rows ? v::Format::columns : v::Format::rows;
  {
    auto escaped_scope=owner.read();
    check(owner.convert(7, opposite));
    const auto escaped=owner.body(escaped_scope, 7, true);
    auto* const realization=escaped.selected.realization;
    check(owner.convert(7, initial));
    check(escaped.selected.realization==realization && escaped.format()==opposite);
    check(p::read_operation(escaped, {.kind=Kind::read, .key=original[7].begin()->first, .record={}}) ==
        p::oracle(original[7], {.kind=Kind::read, .key=original[7].begin()->first, .record={}}));
    ++comparisons;
  }
  check(owner.convert(7, opposite));
  std::array<std::size_t, 3> destinations{5, 6, 7};
  for (unsigned round=0; round<3; ++round) {
    std::array<Operation, 3> edits;
    std::array<Outcome, 3> results;
    for (std::size_t i=0; i<edits.size(); ++i) {
      const auto object=destinations[i], key=corpus.images[object].rows[round].key;
      edits[i]={.kind=Kind::update, .key=key, .record={UINT64_MAX, unsigned(700+10*round+i), unsigned(900+10*round+i)}};
      expected[object][key].value=edits[i].record.value;
      expected[object][key].tag=edits[i].record.tag;
    }
    check(owner.update(edits, results, destinations));
  }
  check(owner.cut()==old_cut+3);
  const auto allocation_calls=owner.statistics().allocation_calls;
  {
    auto current=owner.read();
    check(owner.body(current, 7).format()==opposite); // actual converted primary fallback
    for (std::size_t i=0; i<original.size(); ++i) {
      // First lookup in this old Scope, after three publications and conversion.
      exercise(owner, *unseen, i, original[i]);
      exercise(owner, current, i, expected[i]);
      const auto scopes_before=owner.statistics().scopes;
      Outcome current_output;
      const Operation current_op{.kind=Kind::chain,.key=expected[i].begin()->first, .record={},.count=16};
      check(c::InlineReadFusion::consume_current(owner,corpus.images[i].address.object,current_op,current_output));
      check(current_output==p::oracle(expected[i],current_op) && owner.statistics().scopes==scopes_before);
      ++comparisons;
      auto scalar=owner.bodies<1>();
      Outcome actual;
      Operation op{.kind=Kind::read, .key=expected[i].begin()->first, .record={}};
      scalar.consume_into(i, op, actual);
      check(actual==p::oracle(expected[i], op) && scalar.bindings==1); ++comparisons;
    }
    const Outcome sentinel{17, 19, 23}; Outcome output=sentinel;
    const Operation op{.kind=Kind::read, .key=1, .record={}};
    auto absent=c::InlineReadFusion::consume(owner, current, missing, op, output);
    check(!absent && absent.error()==orbital::Error::absent && output==sentinel); ++refusals;
    auto current_absent=c::InlineReadFusion::consume_current(owner,missing,op,output);
    check(!current_absent && current_absent.error()==orbital::Error::absent && output==sentinel); ++refusals;
    bool invalid=false;
    try { (void)c::InlineReadFusion::consume(owner, current, corpus.images[initial==v::Format::rows ? 0 : 7].address.object,
        {.kind=Kind::chain, .key=1, .record={}, .count=17}, output); }
    catch(const std::invalid_argument&) { invalid=true; }
    check(invalid && output==sentinel); ++refusals;
    c::Inline other(corpus, 64*1024*1024, initial); auto foreign=other.read();
    auto wrong=c::InlineReadFusion::consume(owner, foreign, corpus.images[0].address.object, op, output);
    check(!wrong && wrong.error()==orbital::Error::invalid_argument && output==sentinel); ++refusals;
  }
  check(owner.statistics().allocation_calls==allocation_calls);
  {
    // Actual tombstone publication, followed by old discovery and eventual
    // entry erasure. This modifies the real catalog only in the falsifier.
    auto& store=const_cast<HistoryStore&>(owner.catalog());
    HistoryStore::Prepared accepted(store);
    std::optional<HistoryStore::Scope> basis(std::in_place, store);
    auto retained=HistoryStore::Owner::retain(basis->bind(corpus.images[0].address.object).realization);
    const std::array<HistoryStore::Change,2> changes{{
        {corpus.images[1].address.object,{}}, {missing,retained}}};
    check(store.prepare(*basis, changes, accepted)); basis.reset(); check(store.install(accepted));
    auto current=owner.read();
    const Outcome sentinel{29, 31, 37}; Outcome output=sentinel;
    auto absent=c::InlineReadFusion::consume(owner,current,corpus.images[1].address.object,
        {.kind=Kind::read,.key=1, .record={}},output);
    check(!absent && absent.error()==orbital::Error::absent && output==sentinel); ++refusals;
    exercise(owner,*unseen,1,original[1]);
    const Operation born_op{.kind=Kind::read,.key=expected[0].begin()->first,.record={}};
    auto not_yet=c::InlineReadFusion::consume(owner,*unseen,missing,born_op,output);
    check(!not_yet && not_yet.error()==orbital::Error::absent && output==sentinel); ++refusals;
    check(c::InlineReadFusion::consume(owner,current,missing,born_op,output));
    check(output==p::oracle(expected[0],born_op)); ++comparisons;
    // This real Object has no Corpus ordinal at all.
    check(c::InlineReadFusion::consume_current(owner,missing,born_op,output));
    check(output==p::oracle(expected[0],born_op)); ++comparisons;
  }
  unseen.reset(); owner.drain(); owner.poll(65536);
  check(owner.statistics().scopes==0 && owner.statistics().objects==8);
  {
    auto current=owner.read();
    exercise(owner,current,7,expected[7]); // probe through erased collision bucket
  }
  // Backend/Runtime destruction retain their actual zero-PMR/Arena assertions.
}
int main() {
  static_assert(!std::is_move_constructible_v<c::Inline::Scope>);
  static_assert(!std::is_move_constructible_v<c::Inline::Bodies<1>>);
  run(v::Format::rows); run(v::Format::columns);
  std::printf("{\"passed\":true,\"comparisons\":%llu,\"refusals\":%llu,\"timing\":false}\n",
      static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(refusals));
}
