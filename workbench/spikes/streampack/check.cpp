#include "byte_search.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <orbital/allocation.h>

namespace sn = streampack_next;
using ByteVector = std::vector<std::byte>;
using Match = std::pair<std::size_t, std::size_t>;
void require(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::abort(); }
}
struct LogicalSlot { ByteVector value; std::uint16_t reservation; bool dead{}; };

template<class End, bool Used = true, bool Dead = true> struct Fixture {
  std::vector<End> ends;
  ByteVector storage;
  std::vector<std::uint16_t> used;
  std::vector<std::uint64_t> tombs;
  std::uint16_t count{}, maximum;
  std::vector<LogicalSlot> model;
  static constexpr std::size_t guard = 32;
  Fixture(unsigned capacity, std::size_t body, unsigned maximum = 65535)
      : ends(capacity + 1, End{77}), storage(body + 2 * guard, std::byte{0x9d}),
        used(Used ? capacity : 0, 99), tombs(Dead ? capacity / 64 : 0, ~0ull),
        maximum(maximum) {
    auto w = write(); ikea::no_effects effects;
    require(w.initialize(effects) == sn::Error::none, "initialize");
  }
  sn::MutableDirect<End, Used, Dead> write() {
    return {ends, std::span(storage).subspan(guard, storage.size() - 2 * guard),
            used, tombs, count, maximum};
  }
  auto read() { return write().read(); }
  void append(ByteVector value, unsigned reservation) {
    auto w = write(); ikea::no_effects effects;
    require(w.append(value, reservation, effects) == sn::Error::none, "append");
    model.push_back({std::move(value), std::uint16_t(reservation)});
  }
  void verify() {
    auto view = read();
    require(view.admit(maximum) == sn::Error::none, "admission");
    require(view.size() == model.size(), "assigned identities");
    std::vector<unsigned> owner;
    for (std::size_t r = 0; r < model.size(); ++r) {
      auto& expected = model[r];
      require(view.live(r) == !expected.dead, "logical liveness");
      if (!expected.dead) {
        require(view.length(r) == expected.value.size(), "logical length");
        auto got = view.value(r);
        require(std::equal(got.begin(), got.end(), expected.value.begin(), expected.value.end()), "exact row bytes");
      }
      for (unsigned j = 0; j < expected.reservation; ++j) owner.push_back(r);
    }
    require(view.extent() == owner.size(), "model body extent");
    for (unsigned x = 0; x < owner.size(); ++x) {
      auto got = view.reverse(x);
      require(got && got->rank == owner[x] && got->tombstone == model[owner[x]].dead, "byte ownership");
    }
    require(!view.reverse(owner.size()) && !view.reverse(0xffffffff), "reverse outside body");
    for (unsigned i = 0; i < guard; ++i)
      require(storage[i] == std::byte{0x9d} && storage[storage.size() - 1 - i] == std::byte{0x9d}, "body canaries");
  }
  void search(std::byte needle) {
    std::vector<Match> expected;
    for (unsigned r = 0; r < model.size(); ++r) {
      if (model[r].dead) continue;
      for (unsigned p = 0; p < model[r].value.size(); ++p)
        if (model[r].value[p] == needle) expected.emplace_back(r, p);
    }
    auto view = read();
    for (unsigned method = 0; method < 3; ++method) {
      std::vector<Match> got;
      const auto emit = [&](auto r, auto p) { got.emplace_back(r, p); return true; };
      if (method == 0) sn::byte_per_value(view, needle, emit);
      if (method == 1) sn::byte_sparse(view, needle, emit);
      if (method == 2) sn::byte_segmented(view, needle, [&](auto r, auto base, auto begin, auto mask) {
        while (mask.bits) { emit(r, base + mask.first() - begin); mask.drop_first(); }
        return true;
      });
      require(got == expected, "exact ordered occurrence output");
    }
    std::vector<bool> expected_rows(model.size()), got_rows(model.size());
    for (auto [r, p] : expected) { (void)p; expected_rows[r] = true; }
    std::size_t count = 0;
    sn::byte_segmented(view, needle, [&](auto r, auto, auto, auto mask) {
      got_rows[r] = true; count += mask.population(); return true;
    });
    require(got_rows == expected_rows && count == expected.size(), "bitmap/count masks");
    bool any = false;
    sn::byte_segmented(view, needle, [&](auto, auto, auto, auto) { any = true; return false; });
    require(any == !expected.empty(), "global any");
  }
};

template<class End> void histories() {
  std::mt19937 rng(724193);
  for (unsigned capacity : {128, 256, 512}) {
    Fixture<End> f(capacity, 60000, 255);
    ikea::no_effects effects;
    f.verify(); f.search(std::byte{0});
    for (unsigned i = 0; i < capacity; ++i) {
      auto reservation = i % 11 == 0 ? 0 : rng() % 70;
      ByteVector value(reservation ? rng() % (reservation + 1) : 0);
      for (auto& b : value) b = std::byte(rng() % 256);
      f.append(value, reservation);
      if (i % 13 == 0) { auto w = f.write(); require(w.retire(i, effects) == sn::Error::none, "retire"); f.model[i].dead = true; }
      f.verify();
    }
    for (unsigned step = 0; step < 300; ++step) {
      auto r = rng() % capacity;
      if (f.model[r].dead) continue;
      ByteVector value(rng() % (f.model[r].reservation + 1));
      for (auto& b : value) b = std::byte(rng() % 256);
      auto w = f.write();
      require(w.replace(r, value, effects) == sn::Error::none, "replace");
      f.model[r].value = value;
      // Exact per-operation oracle, not a commutative checksum.
      f.verify();
    }
    f.verify();
    for (auto needle : {0, 1, 42, 127, 255}) f.search(std::byte(needle));
    for (bool shrink : {false, true}) {
      auto w = f.write();
      require(sn::soft_pack(w, shrink, effects) == sn::Error::none, "soft pack");
      for (auto& slot : f.model)
        if (slot.dead) slot.reservation = 0;
        else if (shrink) slot.reservation = slot.value.size();
      f.verify();
      for (auto needle : {0, 42, 255}) f.search(std::byte(needle));
    }
    auto before = f.storage; auto ends = f.ends; auto used = f.used; auto tombs = f.tombs;
    auto w = f.write();
    require(w.append({}, 0, effects) == sn::Error::capacity, "rank exhaustion with space");
    require(f.storage == before && f.ends == ends && f.used == used && f.tombs == tombs, "refusal unchanged");
  }
}

void exhaustive_reverse() {
  // Independent expanded owner oracle; no candidate decode or binary search.
  for (unsigned n = 0; n <= 5; ++n) {
    const unsigned combinations = 1u << (2 * n);
    for (unsigned widths = 0; widths < combinations; ++widths) {
      std::array<std::uint16_t, 6> ends{};
      std::array<std::byte, 15> body{};
      std::vector<unsigned> owner;
      for (unsigned r = 0; r < n; ++r) {
        unsigned width = (widths >> (2 * r)) & 3;
        for (unsigned b = 0; b < width; ++b) owner.push_back(r);
        ends[r + 1] = owner.size();
      }
      for (std::uint64_t bits = 0; bits < (1u << n); ++bits) {
        sn::Direct<std::uint16_t, false, true> view{std::span(ends).first(n + 1), body, {}, {&bits, 1}};
        require(view.admit(3) == sn::Error::none, "exhaustive admitted");
        for (unsigned x = 0; x <= owner.size(); ++x) {
          auto got = view.reverse(x);
          if (x == owner.size()) require(!got, "exhaustive sentinel");
          else require(got && got->rank == owner[x] && got->tombstone == bool(bits & (1ull << owner[x])), "exhaustive interval");
        }
      }
    }
  }
}

void aliases_and_effects() {
  Fixture<std::uint16_t> f(128, 256);
  ByteVector bytes;
  for (unsigned i = 0; i < 40; ++i) bytes.push_back(std::byte(i));
  f.append(bytes, 64);
  auto w = f.write();
  std::array<ikea::journal::entry, 8> records;
  ikea::journal effects{records};
  auto aliased = w.read().value(0).subspan(3, 20);
  ByteVector oracle(aliased.begin(), aliased.end());
  require(w.replace(0, aliased, effects) == sn::Error::none, "overlap edit");
  f.model[0].value = oracle;
  require(effects.used == 2 && effects.entries()[0].source == &w &&
      effects.entries()[0].plane == 0 && effects.entries()[0].size == 20,
      "source-qualified issued body writes");
  f.verify();
  effects.used = 0;
  auto input = w.read().value(0);
  require(w.append(input, 25, effects) == sn::Error::none, "self source append without grow");
  f.model.push_back({oracle, 25});
  require(effects.used == 5 && effects.entries()[0].size == 25, "append plane coverage");
  f.verify();
  auto old_bytes = f.storage; auto old_ends = f.ends; auto old_used = f.used;
  ikea::journal tiny{std::span(records).first(1)};
  require(w.replace<true>(0, {}, tiny) == sn::Error::effects, "effect refusal");
  require(tiny.used == 0 && old_bytes == f.storage && old_ends == f.ends && old_used == f.used,
      "effect refusal before stores");
  auto meta = std::as_bytes(std::span(f.ends)).first(4);
  require(w.append(meta, 4, effects) == sn::Error::alias, "metadata input alias");
  require(w.replace(127, {}, tiny) == sn::Error::rank, "unassigned rank");
  const auto journal_input = std::as_bytes(std::span(records)).first(4);
  require(w.replace(0, journal_input, effects) == sn::Error::alias, "source aliases effect storage");
}

void continued_soft_history() {
  Fixture<std::uint32_t> f(128, 65536);
  ikea::no_effects effects;
  for (unsigned round = 0; round < 6; ++round) {
    for (unsigned i = 0; i < 12; ++i) {
      f.append(ByteVector(i, std::byte(i + round)), i + 8); f.verify();
    }
    for (unsigned r = 0; r < f.model.size(); ++r) if (!f.model[r].dead) {
      auto w = f.write();
      if ((r + round) % 4 == 0) {
        require(w.retire(r, effects) == sn::Error::none, "continued retire"); f.model[r].dead = true;
      } else {
        auto n = std::min<unsigned>(f.model[r].reservation, round + 1);
        auto v = ByteVector(n, std::byte(17 + round));
        require(w.replace(r, v, effects) == sn::Error::none, "continued edit"); f.model[r].value = v;
      }
      f.verify();
    }
    auto w = f.write(); const bool shrink = round % 2;
    require(sn::soft_pack(w, shrink, effects) == sn::Error::none, "continued pack");
    for (auto& v : f.model) if (v.dead) v.reservation = 0; else if (shrink) v.reservation = v.value.size();
    f.verify(); f.search(std::byte(17 + round));
  }
  auto w = f.write();
  const auto r = unsigned(f.model.size() - 1);
  if (w.read().live(r) && w.read().length(r)) {
    require(w.replace<true>(r, {}, effects) == sn::Error::none, "clear slack policy");
    f.model[r].value.clear(); f.verify();
    for (auto b : w.body.subspan(w.read().start(r), w.read().reserved(r)))
      require(b == std::byte{0}, "cleared used range after packed source");
  }
}

void maxima_and_exact() {
  Fixture<std::uint16_t> narrow(128, 65536);
  ByteVector full(65535, std::byte{42});
  narrow.append(full, 65535); narrow.append({}, 0); narrow.verify();
  auto w = narrow.write(); ikea::no_effects effects;
  std::array<std::byte, 1> one{std::byte{7}};
  require(w.append(one, 1, effects) == sn::Error::capacity, "u16 exclusive end cliff");
  Fixture<std::uint32_t> wide(512, 131071);
  wide.append(full, 65535); wide.append(full, 65535); wide.verify();
  Fixture<std::uint16_t, false, false> exact(256, 1024);
  exact.append({}, 0); exact.append({std::byte{1}, std::byte{2}}, 2);
  exact.verify(); exact.search(std::byte{2});
  auto ew = exact.write();
  require(ew.replace(1, one, effects) == sn::Error::length, "full recipe rejects slack");
  require(sn::soft_pack(ew, true, effects) == sn::Error::none, "full recipe soft");
  Fixture<std::uint16_t> empty(128, 0, 0);
  for (unsigned r = 0; r < 128; ++r) empty.append({}, 0);
  empty.verify(); empty.search(std::byte{0});
}

void guarded_tails() {
  const auto page = std::size_t(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<std::byte*>(mmap(nullptr, page * 3, PROT_NONE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  require(memory != MAP_FAILED, "mmap guards");
  require(mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0, "writable middle page");
  for (unsigned length = 0; length <= 193; ++length) {
    auto* start = memory + page * 2 - length;
    for (unsigned i = 0; i < length; ++i) start[i] = std::byte((i % 5) ? 42 : 7);
    std::array<std::uint16_t, 2> ends{0, std::uint16_t(length)};
    sn::Direct<std::uint16_t> view{ends, {start, length}, {}, {}};
    std::vector<Match> got, expected;
    for (unsigned i = 0; i < length; ++i) if (start[i] == std::byte{42}) expected.emplace_back(0, i);
    sn::byte_segmented(view, std::byte{42}, [&](auto r, auto base, auto begin, auto mask) {
      while (mask.bits) { got.emplace_back(r, base + mask.first() - begin); mask.drop_first(); }
      return true;
    });
    require(got == expected, "guarded native tail");
  }
  require(munmap(memory, page * 3) == 0, "munmap");
}

template<class T> T take(orbital::Result<T> value) {
  require(bool(value), "Orbital operation"); return std::move(*value);
}
void orbital_growth() {
  namespace oa = orbital::allocation;
  orbital::worker::Lane lane;
  require(bool(orbital::worker::adopt(lane)), "adopt");
  auto arena = take(oa::open_arena(32u << 20));
  auto* pool = take(oa::add_pool(*arena, lane));
  {
    auto body = take(oa::allocate(*pool, {.bytes = 64, .alignment = 1}));
    std::array<std::uint16_t, 129> ends{}, used{};
    std::array<std::uint64_t, 2> tombs{};
    std::uint16_t count{};
    sn::MutableDirect<std::uint16_t> w{ends, {body.address, body.size}, used, tombs, count};
    ikea::no_effects effects;
    require(w.initialize(effects) == sn::Error::none, "Orbital core initialize");
    const std::array<std::byte, 5> original{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    require(w.append(original, 8, effects) == sn::Error::none, "Orbital append");
    const auto source_offset = w.read().start(0), source_length = w.read().length(0);
    auto held = take(oa::hold(body, {0, 1}));
    require(!oa::grow(body, {.bytes = 4097, .alignment = 1}), "held relocation refuses");
    require(std::ranges::equal(w.read().value(0), original), "refusal retained input");
    oa::release(held);
    // End all old address-qualified bindings. Re-resolve the self source from
    // coordinates after growth, not from its now potentially dangling span.
    require(bool(oa::grow(body, {.bytes = 4097, .alignment = 1})), "private growth");
    w.body = {body.address, body.size};
    require(w.append(sn::Bytes(body.address + source_offset, source_length), 16, effects) == sn::Error::none,
        "relocating self append from coordinates");
    require(std::ranges::equal(w.read().value(0), original) && std::ranges::equal(w.read().value(1), original),
        "self append exact logical outputs");
  }
  for (;;) {
    auto progress = oa::close(*pool);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    require(progress.kind() != orbital::Progress::Kind::failed, "Pool close");
    orbital::worker::poll(lane, 128); std::this_thread::yield();
  }
  require(arena->charged_bytes == 0, "settled allocation charge");
  require(bool(oa::remove_pool(*pool)), "remove pool");
  arena.reset(); orbital::worker::relinquish(lane);
}

int main() {
  histories<std::uint16_t>(); histories<std::uint32_t>(); exhaustive_reverse();
  aliases_and_effects(); continued_soft_history(); maxima_and_exact(); guarded_tails(); orbital_growth();
  std::puts("StreamPack fresh direct geometry: exact row/byte-owner oracles, mutation/soft packing, effects/refusals, bounded search and direct Orbital relocation passed");
}
