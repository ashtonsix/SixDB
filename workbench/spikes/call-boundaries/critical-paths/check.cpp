#include "access.h"
#include "../../../benchmarks/orbital/packed-construction.h"
#include "../../../benchmarks/orbital/lookup.h"
#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <thread>

namespace cp = critical_paths;
namespace p = cp::packed;
using namespace orbital_bench;
template<class T, class E> T need(std::expected<T,E> value) {
  if (!value) throw std::runtime_error("fixture real allocation/pack admission failed");
  return std::move(*value);
}
template<class E> void need(std::expected<void,E> value) {
  if (!value) throw std::runtime_error("fixture real allocation/pack admission failed");
}
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
// Expected contents come from ordinary records and an ordered map, never from
// the encoded bytes or any tested reader. Independent checksum follows the
// retained workload contract; the unmodified native Driver is an extra control.
Outcome expected(Key key, const std::map<Key,Record>& rows) {
  Outcome result{};
  const auto mix = [&](std::uint64_t x) {
    result.digest ^= x + 0x9e3779b97f4a7c15ull + (result.digest << 6) + (result.digest >> 2);
  };
  mix(key);
  if (const auto found = rows.find(key); found != rows.end()) {
    mix(found->second.next); mix(found->second.value); mix(found->second.tag); result.rows = 1;
  } else mix(0);
  return result;
}
struct Fixture {
  p::Schema schema;
  loom::Worker worker;
  orbital::Ref<p::alloc::Arena> arena;
  std::vector<std::unique_ptr<p::Page>> pages;
  std::vector<p::Entry> entries;
  std::map<Key,Record> golden;
  explicit Fixture(std::size_t count) {
    arena = need(p::alloc::open_arena(16 * 1024 * 1024));
    need(loom::adopt(worker)); need(loom::configure(worker, arena, 0));
    std::mt19937_64 rng(0x6a97b2139);
    for (std::size_t first = 0; first < count; first += p::capacity) {
      auto page = std::make_unique<p::Page>();
      p::alloc::AllocationRequest request;
      request.bytes = p::page_bytes; request.reserved_bytes = p::page_bytes + 64;
      request.alignment = 64; request.readable_padding = 64;
      page->allocation = need(loom::allocate(worker, request));
      const auto constructed = p::construct_blank_page(*page, schema, p::alloc::bytes(page->allocation), 64, true);
      require(constructed.issued_write_bytes > 0,"pack construction did not issue writes");
      std::array<std::uint16_t,p::capacity> slots;
      for (std::size_t i = 0; i < p::capacity; ++i) slots[i] = i;
      std::shuffle(slots.begin(), slots.end(), rng);
      const auto live = std::min(p::capacity, count - first);
      for (std::size_t rank = 0; rank < live; ++rank) {
        const auto ordinal = first + rank;
        const Key key = ordinal + 1 == count ? ~Key{0} : Key(ordinal * 4);
        const Record record{rng(), std::uint32_t(ordinal & 1 ? rng() & value_mask :
            (ordinal % 4 ? value_mask : 0)), std::uint32_t(rng())};
        const auto slot = slots[rank];
        auto& b = *page->bindings;
        std::array<ikea::owner_write,64> records;
        ikea::source_write_journal effects(records);
        const std::array<std::uint64_t,1> key_value{key}, next_value{record.next}, tag_value{record.tag};
        const std::array<std::uint32_t,1> scalar{record.value};
        need(b.write_key.replace(slot, key_value, effects));
        need(b.write_next.replace(slot, next_value, effects));
        need(b.write_tag.replace(slot, tag_value, effects));
        p::sp::sum_change change;
        need(b.write_value.replace(slot, scalar, p::sp::row_selection::all(), change, effects));
        page->sum += change.finish();
        page->order[rank] = slot;
        golden.emplace(key, record);
      }
      page->live = live;
      const auto maximum = golden.rbegin()->first;
      entries.push_back({maximum,page.get()}); pages.push_back(std::move(page));
    }
  }
  p::Source source() const { return {entries}; }
  void release_allocations() { for (auto& page : pages) p::alloc::release(page->allocation); }
  ~Fixture() {
    for (auto& page : pages) { page->bindings.reset(); p::alloc::release(page->allocation); }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (loom::shutdown(worker).kind() != orbital::Progress::Kind::done) {
      if (std::chrono::steady_clock::now() > deadline) std::terminate();
      loom::poll(worker, {128,0});
      std::this_thread::yield();
    }
    if (arena->charged_bytes.load()) std::terminate();
    loom::relinquish(worker);
  }
};

int main() {
  const std::array<cp::Query,6> arms{cp::ordinary,cp::visible,cp::fused,cp::fused_multiply,cp::fused_simd,cp::fused_fp};
  std::uint64_t checks = 0;
  for (const auto count : {0u,1u,7u,8u,9u,127u,255u,256u,257u,639u}) {
    Fixture fixture(count);
    std::vector<Key> keys{0,1,2,3,~Key{0}-1,~Key{0}};
    for (const auto& [key,record] : fixture.golden) {
      keys.push_back(key); if (key < ~Key{0}) keys.push_back(key + 1);
    }
    for (const auto protection : {cp::Protection::owner_borrow,cp::Protection::retained_scope}) {
      auto original_scope = need(cp::Scope::qualify(fixture.source(),protection));
      auto scope = std::move(original_scope);
      Outcome moved_output{13,17,19};
      require(cp::fused(original_scope,0,moved_output) == cp::Status::closed &&
          moved_output == Outcome{13,17,19},"moved-from scope remained usable");
      for (const auto key : keys) {
        const auto oracle = expected(key,fixture.golden);
        for (const auto query : arms) {
          Outcome output{13,17,19};
          const auto result = query(scope,key,output);
          require(output == oracle,"point operation differs from independent ordered-map oracle");
          require(result == (oracle.rows ? cp::Status::found : cp::Status::absent),"point status differs");
          ++checks;
        }
        // Real Stage 1 complete single-operation native caller, unchanged.
        lookup::Driver driver;
        const std::array<Operation,1> operations{{{.kind=Kind::read,.key=key,.record={},.count=1}}};
        std::array<Outcome,1> actual;
        driver.run(fixture.worker,fixture.source(),operations,actual,ReadExecution::native_direct);
        require(actual[0] == oracle,"unmodified native caller differs from oracle");
        driver.close(); ++checks;
      }
      scope.revoke();
      for (const auto query : arms) {
        Outcome output{13,17,19};
        require(query(scope,0,output) == cp::Status::closed && output == Outcome{13,17,19},
            "revoked scope touched or changed output"); ++checks;
      }
    }
    if (!fixture.pages.empty()) {
      auto& page = *fixture.pages.front();
      const auto saved = page.bindings->keys;
      page.bindings->keys = page.bindings->next;
      auto wrong = cp::Scope::qualify(fixture.source(),cp::Protection::retained_scope);
      require(!wrong && wrong.error() == cp::Admission::description,"wrong interpretation admitted");
      page.bindings->keys = saved;
      const auto live = page.live;
      page.live = p::capacity + 1;
      require(!cp::Scope::qualify(fixture.source(),cp::Protection::owner_borrow),"oversized live extent admitted");
      page.live = live;
      if (live > 1) {
        const auto saved_slot = page.order[1]; page.order[1] = page.order[0];
        require(!cp::Scope::qualify(fixture.source(),cp::Protection::owner_borrow),"aliased live physical slots admitted");
        page.order[1] = saved_slot;
      }
    }
    // Actual physical retention, not an owning-handle-free synthetic read.
    auto retained = need(cp::Scope::qualify(fixture.source(),cp::Protection::retained_scope));
    fixture.release_allocations();
    for (const auto key : keys) for (const auto query : arms) {
      Outcome output{}; query(retained,key,output);
      require(output == expected(key,fixture.golden),"retained old backing failed after real Allocation release"); ++checks;
    }
    for (const auto& page : fixture.pages) require(!page->allocation,"release control did not actually release Allocation");
  }
  std::cout << "{\"check\":\"complete-packed-point-read\",\"passed\":true,\"histories\":" << checks
      << ",\"query_arms\":6,\"native_caller_control\":true,\"retained_release\":true,\"final_charge_zero\":true}\n";
}
