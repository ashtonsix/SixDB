#include "object_ring.h"
#include "../metadata/orbital/candidates/common.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <source_location>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {
namespace lr = loom_object_ring;
namespace cs = compact_shape;
namespace v = orbital_bench::versioned;
namespace oa = orbital::allocation;
namespace sr = orbital_mvcc::metadata_consumer::shape_read;
using Catalog = lr::Catalog;
using orbital_bench::Row;
using orbital_bench::Outcome;
constexpr orbital::ObjectId a{17,101}, b{19,103}, born{23,107}, missing{31,113};
constexpr orbital::ProgramId program{29,109};
std::size_t checks{}, panels{}, requests_checked{}, histories{};
void check(bool value, const char* expression,
    std::source_location at = std::source_location::current()) noexcept {
  ++checks; if (value) return;
  std::fprintf(stderr,"%s:%u: %s\n",at.file_name(),at.line(),expression); std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template<class T> T take(orbital::Result<T> result) { CHECK(result); return std::move(*result); }

struct Worker {
  orbital::Ref<oa::Arena> arena{take(oa::open_arena(16 << 20))};
  loom::Worker worker;
  Worker() { CHECK(loom::adopt(worker)); CHECK(loom::configure(worker,arena,0)); }
  ~Worker() {
    CHECK(!worker.active_tasks);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto progress = loom::shutdown(worker);
      CHECK(progress.kind() != orbital::Progress::Kind::failed);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      CHECK(std::chrono::steady_clock::now() < deadline);
      loom::poll(worker,{.messages=64,.tasks=0});
      std::this_thread::yield();
    }
    CHECK(arena->charged_bytes.load() == 0 && !worker.memory);
    CHECK(!orbital::worker::pending(worker.lane)); loom::relinquish(worker);
  }
};

Catalog::Owner construct(Worker& worker, compact_version_probe::Memory& memory,
    Catalog& store, std::span<const Row> rows, bool guard = false) {
  v::Schema schema;
  auto shape = take(v::shape(schema,v::Format::rows,rows.size()));
  CHECK(shape.part_count == 1);
  orbital::allocation::Allocation allocation;
  orbital::MutableBytes writable;
  std::shared_ptr<const void> backing;
  if (guard) {
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    CHECK(shape.parts[0].body_bytes && shape.parts[0].body_bytes <= page);
    auto* mapping = static_cast<std::byte*>(mmap(nullptr,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    CHECK(mapping != MAP_FAILED && mprotect(mapping+page,page,PROT_READ|PROT_WRITE) == 0);
    writable = {mapping + 2*page - shape.parts[0].body_bytes,shape.parts[0].body_bytes};
    backing = std::shared_ptr<const void>(mapping,[page](const void* value) {
      CHECK(munmap(const_cast<void*>(value),3*page) == 0);
    });
  } else if (!rows.empty()) {
    allocation = take(loom::allocate(worker.worker,{.bytes=shape.parts[0].body_bytes,
        .alignment=shape.parts[0].alignment,.readable_padding=shape.parts[0].readable_padding}));
    writable = loom::bytes(allocation);
  }
  cs::Shape* proof{};
  {
    const std::array<v::WritePart,1> target{{{shape.parts[0].id,writable}}};
    v::KernelCosts costs; CHECK(v::construct_into(schema,shape,target,rows,costs));
    const std::array<compact_descriptor::PartSource,1> parts{{{shape.parts[0].id,writable,
        shape.parts[0].interpretation,shape.parts[0].readable_padding,shape.parts[0].alignment,{}}}};
    proof = take(cs::prove_fixture(memory,schema,program,shape.interpretation,parts));
  }
  const auto owners = guard || rows.empty() ? orbital::PartOwners::independent_shared :
      orbital::PartOwners::independent_allocation;
  const std::array<orbital::PartSource,1> source{{{shape.parts[0].id,writable,backing,&allocation}}};
  auto captured = store.capture({proof,rows.size(),owners},source);
  cs::release(proof); CHECK(captured && !allocation);
  return std::move(*captured);
}
void publish(Catalog& store, std::span<const Catalog::Change> changes) {
  Catalog::Prepared prepared(store);
  { Catalog::Scope basis(store); CHECK(store.prepare(basis,changes,prepared)); }
  CHECK(store.install(prepared));
}
sr::TupleProof proof(const Catalog& store) {
  Catalog::Scope scope(store); const auto selected = scope.bind(a); CHECK(selected);
  return take(sr::admit_tuple_shape(selected.shape(),program,cs::geometry(selected.shape(),0).id));
}

// Independent logical first-match scan and explicit digest recurrence. Neither
// binary-search implementation, native decoder nor consume_* supplies expected.
void mix(Outcome& output, std::uint64_t item) {
  output.digest ^= item + 0x9e3779b97f4a7c15ull + (output.digest << 6) + (output.digest >> 2);
}
void expected_row(Outcome& output, const Row& row) {
  mix(output,row.key); mix(output,row.record.next); mix(output,row.record.value); mix(output,row.record.tag);
  ++output.rows;
}
Outcome expected(std::span<const Row> rows, lr::Request request) {
  Outcome output;
  for (unsigned step = 0; step < request.steps; ++step) {
    const auto found = std::find_if(rows.begin(),rows.end(),[&](const Row& row) { return row.key == request.key; });
    if (found == rows.end()) { mix(output,request.key); mix(output,0); break; }
    expected_row(output,*found); request.key = found->record.next;
  }
  return output;
}
std::vector<Row> rows(std::size_t count) {
  std::vector<Row> output(count);
  for (std::size_t i = 0; i < count; ++i)
    output[i] = {i ? 2*i+11 : 0,{0,std::uint32_t((i*971+37)&orbital_bench::value_mask),std::uint32_t(i*193+71)}};
  if (count > 1) output.back().key = UINT64_MAX;
  if (count > 3) output[2].key = output[1].key; // unequal payload duplicate witness
  for (std::size_t i = 0; i < count; ++i)
    output[i].record.next = i%13 == 4 ? UINT64_MAX-1 : output[(i*17+3)%count].key;
  return output;
}
std::vector<lr::Request> requests(std::span<const Row> data) {
  std::vector<lr::Request> output;
  constexpr std::array<unsigned,7> counts{0,1,2,3,15,16,17};
  for (std::size_t i = 0; i < 129; ++i) {
    std::uint64_t key;
    switch (i%6) {
    case 0: key = 0; break;
    case 1: key = UINT64_MAX; break;
    case 2: key = UINT64_MAX-1; break;
    case 3: key = 1; break;
    default: key = data.empty() ? 11 : data[(i*7+1)%data.size()].key; break;
    }
    output.push_back({a,key,counts[i%counts.size()]});
  }
  return output;
}

void matched(std::span<const Row> data, bool guard = false) {
  Worker owner; compact_version_probe::Memory memory;
  {
    Catalog store(memory);
    { const std::array<Catalog::Change,1> initial{{{a,construct(owner,memory,store,data,guard)}}}; publish(store,initial); }
    const auto admitted = proof(store);
    const auto input = requests(data);
    std::vector<Outcome> oracle; for (auto request : input) oracle.push_back(expected(data,request));
    const std::byte* payload{};
    std::vector<std::byte> snapshot;
    {
      Catalog::Scope inspect(store); const auto selected = inspect.bind(a);
      const auto part = take(selected.part(0)); payload = part.address();
      if (!data.empty()) snapshot.assign(payload,payload+data.size()*v::tuple_bytes);
      for (std::size_t i = 0; i < input.size(); ++i) {
        Outcome actual{1,2,3}; CHECK(sr::read_proven(admitted,selected,input[i].key,input[i].steps,actual));
        CHECK(actual == oracle[i]);
      }
    }
    lr::Driver driver(owner.worker,admitted);
    for (auto width : {1U,2U,4U,8U,16U,32U}) {
      CHECK(driver.configure(width));
      for (bool hints : {false,true}) for (auto quantum : {1U,17U,128U}) {
        std::optional<lr::Statistics> manual;
        for (auto execution : {lr::Execution::serial,lr::Execution::manual,lr::Execution::ring}) {
          std::vector<Outcome> output(input.size(),Outcome{UINT64_MAX,UINT64_MAX,UINT64_MAX});
          const auto statistics = take(driver.run(store,input,output,execution,hints,quantum));
          CHECK(statistics.completed == input.size() && output == oracle);
          if (execution == lr::Execution::manual) manual = statistics;
          if (execution == lr::Execution::ring) {
            CHECK(manual && manual->replacement_admissions == statistics.replacement_admissions);
            CHECK(manual->returned_quanta == statistics.returned_quanta);
            if (data.empty()) CHECK(!statistics.ring_visits);
            else CHECK(statistics.ring_visits > 0);
          }
          CHECK(snapshot.empty() || std::memcmp(payload,snapshot.data(),snapshot.size()) == 0);
          ++panels; requests_checked += input.size();
        }
      }
    }
    CHECK(!driver.configure(0) && !driver.configure(65));
    // An equivalent separately admitted Shape is not this borrowed exact proof.
    { const std::array<Catalog::Change,1> other{{{b,construct(owner,memory,store,data)}}}; publish(store,other); }
    const std::array<lr::Request,2> bad{{{a,0,1},{missing,0,0}}};
    const auto sentinel = Outcome{101,103,107};
    for (auto execution : {lr::Execution::serial,lr::Execution::manual,lr::Execution::ring}) {
      std::array<Outcome,2> output{sentinel,sentinel};
      const auto refused = driver.run(store,bad,output,execution,true,1);
      CHECK(!refused && refused.error() == orbital::Error::absent && output[1] == sentinel);
      CHECK(driver.run(store,std::span(bad).first(1),std::span(output).first(1),execution,false,3));
      CHECK(output[0] == expected(data,bad[0]));
      const std::array<lr::Request,1> wrong_shape{{{b,0,0}}};
      output[0] = sentinel;
      const auto mismatch = driver.run(store,wrong_shape,std::span(output).first(1),execution,false,1);
      CHECK(!mismatch && mismatch.error() == orbital::Error::invalid_argument && output[0] == sentinel);
    }
    std::array<Outcome,1> untouched{sentinel};
    CHECK(!driver.run(store,{},untouched,lr::Execution::ring,true));
    CHECK(!driver.run(store,std::span(bad).first(1),untouched,lr::Execution::ring,true,0));
    CHECK(untouched[0] == sentinel);
    CHECK(driver.close());
  }
  CHECK(memory.live == 0);
}

// Separate correctness-only history. The tiny callbacks deliberately expose
// partial observations so the old-cut/sibling/late-discovery witnesses are exact.
// They are not a performance kernel or an extra production execution family.
struct HistoryFrame {
  Catalog::Scope scope;
  cs::Selection selected;
  const std::byte* first{};
  Outcome result;
  unsigned visits{}, published{};
  bool cancelled{};
  HistoryFrame(const Catalog& store, bool cancel) : scope(store),selected(scope.bind(a)),cancelled(cancel) {
    CHECK(selected && scope.cut() == 1);
    first = reinterpret_cast<const std::byte*>(take(selected.part(0)).address());
  }
  HistoryFrame(const HistoryFrame&) = delete;
  HistoryFrame& operator=(const HistoryFrame&) = delete;
};
using HistoryRing = loom::Ring<HistoryFrame*,std::uint64_t>;
LOOM_NATIVE_CC void history_visit(HistoryRing::Cursor ring, HistoryRing::Slot* slot, HistoryFrame* frame,
                                 std::uint64_t phase) {
  constexpr auto encoding = lr::law::ReadLaw::canonical();
  const std::byte* source{};
  if (phase == 0) source = frame->first;
  else if (phase == 1) {
    source = reinterpret_cast<const std::byte*>(take(frame->selected.part(0)).address()) + v::tuple_bytes;
  } else {
    // First discovery happens after publication and even ObjectStore reset.
    const auto selected = frame->scope.bind(b); CHECK(selected && !frame->scope.bind(born));
    source = reinterpret_cast<const std::byte*>(take(selected.part(0)).address());
  }
  const Row row{encoding.read_unchecked<0>(source),{encoding.read_unchecked<1>(source),
      std::uint32_t(encoding.read_unchecked<2>(source)),std::uint32_t(encoding.read_unchecked<3>(source))}};
  orbital_bench::consume_row(frame->result,row.key,row.record); ++frame->visits;
  if (++phase == 3) { ++frame->published; ring->complete(*slot); }
  [[clang::musttail]] return HistoryRing::yield(ring,slot,frame,phase);
}
void consequence() {
  Worker owner; compact_version_probe::Memory memory;
  constexpr std::array<Row,2> old_a{{{11,{31,101,71}},{31,{11,102,73}}}};
  constexpr std::array<Row,2> old_b{{{11,{31,201,81}},{31,{11,202,83}}}};
  constexpr std::array<Row,2> new_a{{{11,{31,301,91}},{31,{11,302,93}}}};
  constexpr std::array<Row,2> new_b{{{11,{31,401,97}},{31,{11,402,99}}}};
  Catalog store(memory);
  { const std::array<Catalog::Change,2> initial{{{a,construct(owner,memory,store,old_a)},
      {b,construct(owner,memory,store,old_b)}}}; publish(store,initial); }
  HistoryRing ring; CHECK(ring.configure(owner.worker,{2,2}));
  std::array<std::unique_ptr<HistoryFrame>,4> frames;
  for (unsigned i = 0; i < frames.size(); ++i) {
    frames[i] = std::make_unique<HistoryFrame>(store,!(i&1));
    CHECK(ring.push(history_visit,{frames[i].get(),0}));
  }
  CHECK(ring.spin({2}) == 2);
  Outcome first; expected_row(first,old_a[0]);
  CHECK(frames[0]->result == first && frames[1]->result == first);
  CHECK(frames[0]->visits == 1 && frames[1]->visits == 1 && !frames[2]->visits && !frames[3]->visits);
  auto bound = take(store.bound(frames[1]->scope,a));
  auto* escaped = take(bound.export_part(0,orbital::PartRetention::selected_copy));
  { const std::array<Catalog::Change,3> changes{{{a,construct(owner,memory,store,new_a)},
      {b,construct(owner,memory,store,new_b)},{born,construct(owner,memory,store,new_a)}}}; publish(store,changes); }
  {
    Catalog::Scope fresh(store); CHECK(fresh.cut() == 2 && fresh.bind(born));
    const auto current_a = fresh.bind(a);
    const auto* a_bytes = reinterpret_cast<const std::byte*>(take(current_a.part(0)).address());
    CHECK(cs::tuple_law(current_a.shape()).read_unchecked<2>(a_bytes) == new_a[0].record.value);
    const auto selected = fresh.bind(b);
    const auto* bytes = reinterpret_cast<const std::byte*>(take(selected.part(0)).address());
    CHECK(cs::tuple_law(selected.shape()).read_unchecked<2>(bytes) == new_b[0].record.value);
  }
  store = {};
  CHECK(ring.release_if([](const HistoryRing::Arguments& arguments) noexcept {
    return std::get<0>(arguments)->cancelled;
  }) == 2);
  CHECK(ring.pending() == 2 && !frames[0]->published && !frames[2]->published);
  frames[0].reset(); frames[2].reset(); // active AND overflow removed before destruction
  while (ring.pending()) CHECK(ring.spin({1}) == 1);
  Outcome oracle; expected_row(oracle,old_a[0]); expected_row(oracle,old_a[1]); expected_row(oracle,old_b[0]);
  for (unsigned i : {1U,3U}) CHECK(frames[i]->result == oracle && frames[i]->visits == 3 && frames[i]->published == 1);
  const auto counters = ring.counters();
  CHECK(counters.accepted == 4 && counters.visits == 7 && counters.completed == 2 &&
      counters.abandoned == 2 && counters.promoted == 1);
  frames[1].reset(); frames[3].reset();
  CHECK(memory.live > 0 && cs::records(escaped) == old_a.size());
  CHECK(orbital::binding(escaped).object == a && orbital::binding(escaped).version == 1);
  const auto* bytes = reinterpret_cast<const std::byte*>(orbital::view(escaped).address());
  CHECK(cs::tuple_law(escaped).read_unchecked<2>(bytes) == old_a[0].record.value);
  orbital::release(escaped); CHECK(memory.live == 0);
  CHECK(loom::shutdown(owner.worker).kind() == orbital::Progress::Kind::waiting); // empty Ring bank is live
  CHECK(ring.close()); ++histories;
}
} // namespace

int main() {
  static_assert(!std::is_move_constructible_v<lr::Frame>);
  static_assert(!std::is_move_constructible_v<HistoryFrame>);
  for (auto count : {0U,1U,2U,23U,257U,4097U}) matched(rows(count));
  constexpr std::array<Row,1> final_row{{{UINT64_MAX,{UINT64_MAX,17,UINT32_MAX}}}};
  matched(final_row,true); consequence();
  std::printf("{\"passed\":true,\"checks\":%zu,\"matched_panels\":%zu,\"requests_checked\":%zu,\"consequence_histories\":%zu,\"timing\":false}\n",
      checks,panels,requests_checked,histories);
}
