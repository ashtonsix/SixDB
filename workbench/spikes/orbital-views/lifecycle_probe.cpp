#include "prototype.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <source_location>
#include <thread>

using namespace views;
namespace {
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::fprintf(stderr, "lifecycle check failed at %u\n", at.line()); std::abort(); }
}
template<class T> T get(Result<T> r) { check(r.has_value()); return std::move(*r); }
void done(Result<void> r) { check(r.has_value()); }
const std::vector<std::size_t> first{0}, second{1}, both{0, 1};
void creation_growth_and_stage() {
  const auto page = page_size();
  auto pool = std::make_shared<recycling::Pool>();
  Part part({}, 0, false, page * 4, pool);
  auto draft = get(part.begin(Method::cow, {}));
  check(!draft.access(first));
  done(draft.grow(page));
  auto writer = get(draft.access(first));
  auto* base = writer.writable(); base[11] = 71;
  done(draft.grow(page * 3));
  check(writer.data() == base);
  check(!draft.grow(page * 5) && !draft.grow(3) && !draft.grow(page));
  check(draft.stage(std::vector<Range>{{11, 1}}).error() == Error::busy);
  writer.close();
  auto reader = get(draft.access(first, false));
  auto other = get(draft.access(second));
  std::atomic<bool> started{}, stop{};
  std::thread work([&] {
    started.store(true, std::memory_order_release);
    while (!stop.load(std::memory_order_acquire)) other.writable()[page + 19] = 92;
  });
  while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
  // A declared writer on page 1 need not stop while stable page 0 is captured.
  auto earlier = get(draft.stage(std::vector<Range>{{11, 1}}));
  stop.store(true, std::memory_order_release); work.join(); other.close();
  auto later = get(draft.stage(std::vector<Range>{{page + 19, 1}}));
  later->complete = true;
  check(draft.prepare(1).error() == Error::busy); reader.close();
  check(draft.prepare(1).error() == Error::pending);
  earlier->complete = true;
  auto prepared = get(draft.prepare(1));
  Bytes expected(page * 3); expected[11] = 71; expected[page + 19] = 92;
  check(prepared.bytes() == expected && part.latest() == 0 && part.size() == 0);
  check(!draft.access(first));
  done(prepared.install());
  auto latest = get(part.open(1, both));
  check(latest.data() == base && latest.size() == page * 3);
  check(std::memcmp(latest.data(), expected.data(), page * 2) == 0);
  latest.close(); done(part.advance(7));
  check(get(part.interval(5)).first == 1);
  check(pool->counts().maps == 1 && pool->counts().held == 1);
}
void cancelled_preparation() {
  Part part(Bytes(page_size(), 31));
  {
    auto draft = get(part.begin(Method::copy, first));
    auto use = get(draft.access(first)); use.writable()[7] = 54; use.close();
    auto delivery = get(draft.stage(std::vector<Range>{{7, 1}})); delivery->complete = true;
    auto prepared = get(draft.prepare(2));
    check(prepared.bytes()[7] == 54);
    check(get(part.open(1, first)).data()[7] == 31);
    check(!part.begin(Method::copy, first));
    // Abandoning a sealed candidate before the journal decision preserves the old head.
  }
  check(part.latest() == 1 && get(part.open(1, first)).data()[7] == 31);
  auto retry = get(part.begin(Method::cow, first)); done(retry.abort());
}
void growth_after_borrowers() {
  const auto page = page_size();
  Part part(Bytes(page, 31), 1, false, page * 2);
  auto draft = get(part.begin(Method::cow, first));
  auto writer = get(draft.access(first)); writer.writable()[7] = 54;
  check(draft.grow(page * 3).error() == Error::busy);
  writer.close();
  auto delivery = get(draft.stage(std::vector<Range>{{7, 1}})); delivery->complete = true;
  done(draft.grow(page * 3));
  auto enlarged = get(draft.access(both));
  check(enlarged.data()[7] == 54 && enlarged.data()[page + 7] == 0);
  enlarged.close(); done(draft.publish(2));
  check(part.size() == page * 3 && part.capacity() == page * 3);
  check(get(part.open(1, first)).data()[7] == 31);
}
void empty_damage() {
  for (auto extent : {std::size_t{0}, std::size_t{47}}) {
    Part part(Bytes(extent, 31));
    auto draft = get(part.begin(Method::cow, extent ? first : std::vector<std::size_t>{}));
    auto stage = get(draft.stage(std::vector<Range>{{extent, 0}})); stage->complete = true;
    auto prepared = get(draft.prepare(2)); check(prepared.bytes() == Bytes(extent, 31));
    done(prepared.install()); check(part.size() == extent);
  }
}
void fallback_geometry() {
  const auto page = page_size(); recycling::Pool pool;
  auto a = pool.acquire(recycling::Temperature::cold, 1, page * 4);
  auto b = pool.acquire(recycling::Temperature::cold, 1, page * 4);
  auto small = pool.acquire(recycling::Temperature::warm, 1);
  a.release(recycling::Temperature::cold, 1); b.release(recycling::Temperature::cold, 1);
  small.release(recycling::Temperature::warm, 1);
  pool.trim_cold(1); pool.wait_idle(); check(pool.counts().unmaps == 0);
  auto large = pool.acquire(recycling::Temperature::hot, 1, page * 4);
  check(pool.counts().maps == 3 && pool.counts().fallback == 1);
  large.release(recycling::Temperature::cold, 1);
  auto warm = pool.acquire(recycling::Temperature::warm, 1, page * 4);
  auto extra = pool.acquire(recycling::Temperature::cold, 1, page * 4);
  auto another = pool.acquire(recycling::Temperature::cold, 1, page * 4);
  extra.release(recycling::Temperature::cold, 1);
  another.release(recycling::Temperature::cold, 1);
  warm.release(recycling::Temperature::warm, 1); pool.wait_idle();
  check(pool.counts().unmaps == 1);
}
void lifetime_into_reuse() {
  const auto page = page_size();
  auto pool = std::make_shared<recycling::Pool>();
  std::optional<Lease> backend;
  const std::uint8_t* address{};
  const recycling::Hint hint{recycling::Temperature::hot, 17,
      std::chrono::steady_clock::now() - std::chrono::seconds(2)};
  {
    Part part(Bytes(page, 187), 1, false, page * 2, pool);
    auto reader = get(part.open(1, first, Method::copy)); address = reader.data();
    backend.emplace(reader); reader.release(hint);
    part.evict();
    check(pool->counts().held == 1 && pool->counts().free == 0);
  }
  check(backend->data()[0] == 187 && pool->counts().held == 1);
  auto unrelated = pool->acquire(recycling::Temperature::hot, 17, page * 2);
  check(reinterpret_cast<const void*>(unrelated.data()) != address);
  unrelated.release(recycling::Temperature::warm, 19);
  backend->close(); backend.reset();
  check(pool->counts().held == 0 && pool->counts().free == 2);
  {
    auto recycled = pool->acquire(recycling::Temperature::hot, 17, page * 2);
    check(reinterpret_cast<const void*>(recycled.data()) == address);
    check(recycled.hint().last_access == hint.last_access && recycled.hint().cpu == 17);
    recycled.data()[0] = std::byte{111};
    recycled.data()[page + 13] = std::byte{112};
    check(pool->counts().restored == 1);
  }
  Part replacement(Bytes(page, 0), 8, false, page * 2, pool);
  auto clean = get(replacement.open(8, first, Method::copy));
  check(clean.data() == address);
  for (std::size_t i = 0; i < page; ++i) check(clean.data()[i] == 0);
  check(pool->counts().maps == 2 && pool->counts().reused == 2);
}
}
int main() {
  creation_growth_and_stage(); cancelled_preparation(); growth_after_borrowers(); empty_damage(); fallback_geometry(); lifetime_into_reuse();
  std::puts("{\"result\":\"pass\",\"cases\":[\"empty-grow-stable-address\",\"disjoint-live-writer-stage\",\"prepare-before-decision\",\"cancel-sealed-candidate\",\"growth-beyond-reservation\",\"empty-damage\",\"fallback-matches-reservation\",\"backend-use-through-cache-eviction-and-reuse\"]}");
}
