#include "substring.h"
#include "search_reference.h"
#include <duckdb/common/types/string_type.hpp>
#include <orbital/allocation.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <thread>

namespace sn = streampack_next;
namespace oa = orbital::allocation;
using Match = std::pair<unsigned, unsigned>;
using Matches = std::vector<Match>;
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital result"); return std::move(*r); }
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
sn::Bytes bytes(const std::string& s) { return std::as_bytes(std::span(s)); }
template<class Run> double measure(Run&& run) {
  unsigned iterations = 1;
  for (;;) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) run();
    const auto ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    if (ns >= 4000000 || iterations >= 1048576) return ns / iterations;
    iterations *= 2;
  }
}

// Ready immutable schema fixture. Allocation backing is direct Orbital; oracle
// and temporary construction storage coexist, so encoded bytes are not RSS.
struct Fixture {
  unsigned capacity;
  std::vector<std::optional<std::string>> model;
  std::vector<std::uint32_t> ends, inverse_ends, inverse_rows;
  std::vector<std::uint16_t> used, holders;
  std::vector<std::uint64_t> dead;
  std::vector<sn::Prefix> prefixes;
  std::vector<duckdb::string_t> duck;
  oa::Allocation body, duck_body;
  std::size_t extent{}, outlined{};
  Fixture(oa::Pool& pool, unsigned n, unsigned width, unsigned recipe, bool periodic)
      : capacity(n), model(n), ends(n + 1), inverse_ends(n + 1), used(n), holders(n, 65535),
        dead(n / 64), prefixes(n), duck(n) {
    std::vector<std::string> suffix(n);
    std::mt19937 rng(7145);
    for (unsigned r = 0; r < n; ++r) {
      suffix[r].resize(r % 11 ? width : 0);
      for (auto& c : suffix[r]) c = periodic ? 'a' : char(rng() % 14);
      used[r] = suffix[r].size();
      ends[r + 1] = ends[r] + used[r] + (recipe == 2 ? width / 2 : 0);
    }
    extent = ends.back();
    if (extent) body = take(oa::allocate(pool, {.bytes = extent, .alignment = 64}));
    for (unsigned r = 0; r < n; ++r) {
      if (used[r]) std::memcpy(body.address + ends[r], suffix[r].data(), used[r]);
      if (ends[r + 1] > ends[r] + used[r]) std::memset(body.address + ends[r] + used[r], periodic ? 'a' : 7, ends[r + 1] - ends[r] - used[r]);
    }
    for (unsigned row = 0; row < n; ++row) {
      if (recipe && row % 13 == 0) continue;
      const auto rank = recipe ? (row * 17) % (n / 2) : row;
      holders[row] = rank;
      auto& prefix = prefixes[row]; prefix.size = recipe ? row % 5 : 0;
      std::string full;
      for (unsigned j = 0; j < prefix.size; ++j) { prefix.bytes[j] = std::byte(periodic ? 'a' : (row + j) % 14); full.push_back(char(prefix.bytes[j])); }
      full += suffix[rank]; model[row] = full;
      if (full.size() > 12) outlined += full.size();
    }
    if (outlined) duck_body = take(oa::allocate(pool, {.bytes = outlined, .alignment = 64}));
    std::size_t offset = 0;
    for (unsigned row = 0; row < n; ++row) if (model[row]) {
      const auto& full = *model[row];
      if (full.size() > 12) {
        std::memcpy(duck_body.address + offset, full.data(), full.size());
        duck[row] = duckdb::string_t(reinterpret_cast<const char*>(duck_body.address + offset), full.size()); offset += full.size();
      } else duck[row] = duckdb::string_t(full.data(), full.size());
    }
    for (unsigned r = 0; r < n; ++r) {
      for (unsigned row = 0; row < n; ++row) if (holders[row] == r) inverse_rows.push_back(row);
      inverse_ends[r + 1] = inverse_rows.size();
      if (inverse_ends[r] == inverse_ends[r + 1]) dead[r / 64] |= 1ull << (r % 64);
    }
    std::vector<std::uint8_t> scratch(n);
    require(sn::admit_schema(view(), prefixes, holders, inverse(), scratch) == sn::Error::none, "search fixture admission");
  }
  sn::Direct<std::uint32_t, true, true> view() const { return {ends, {body.address, extent}, used, dead}; }
  sn::Fanout inverse() const { return {inverse_ends, inverse_rows}; }
  Matches oracle(sn::Bytes q) const {
    Matches result;
    for (unsigned row = 0; row < capacity; ++row) if (model[row]) {
      const auto s = bytes(*model[row]);
      for (unsigned p = 0; p <= s.size(); ++p) {
        if (q.size() > s.size() - p) continue;
        bool same = true;
        for (unsigned j = 0; j < q.size(); ++j) if (s[p + j] != q[j]) { same = false; break; }
        if (same) result.emplace_back(row, p);
      }
    }
    return result;
  }
};
enum class Op { any, membership, count, ordered };
enum class Method { body, split_sz, duck_sz, duck_libc, kmp };
template<Op O> using Output = std::conditional_t<O == Op::any, bool,
    std::conditional_t<O == Op::count, std::uint64_t,
    std::conditional_t<O == Op::membership, std::array<std::uint64_t, 8>, Matches>>>;

template<Op O, Method M>
__attribute__((noinline)) void search(const Fixture& f, sn::Bytes q, const sn::Kmp& prepared, Output<O>& out) {
  if constexpr (O == Op::ordered) out.clear(); else out = {};
  const auto emit = [&](unsigned row, unsigned position) {
    if constexpr (O == Op::any) { out = true; return false; }
    if constexpr (O == Op::membership) out[row / 64] |= 1ull << (row % 64);
    if constexpr (O == Op::count) ++out;
    if constexpr (O == Op::ordered) out.emplace_back(row, position);
    return true;
  };
  if constexpr (M == Method::body) {
    sn::schema_body_search(f.view(), f.prefixes, f.holders, f.inverse(), q, emit);
    if constexpr (O == Op::ordered) std::sort(out.begin(), out.end());
  } else {
    for (unsigned row = 0; row < f.capacity; ++row) if (f.holders[row] != 65535) {
      // Exists/membership stop at the first occurrence *of this row*. The next
      // row is still processed for membership; no forced occurrence expansion.
      const auto row_emit = [&](auto p) { emit(row, p); return O == Op::count || O == Op::ordered; };
      if constexpr (M == Method::kmp) {
        const auto& p = f.prefixes[row];
        prepared.scan_parts({p.bytes.data(), p.size}, f.view().value(f.holders[row]), row_emit);
      } else if constexpr (M == Method::split_sz) {
        const auto& p = f.prefixes[row]; const sn::Bytes prefix{p.bytes.data(), p.size};
        const auto suffix = f.view().value(f.holders[row]);
        bool keep = true;
        for (unsigned at = 0; at < prefix.size(); ++at)
          if (sn::seam_equal(prefix, suffix, at, q) && !row_emit(at)) { keep = false; break; }
        if (keep) sn::overlapping_find(suffix, q, sn::stringzilla_find, [&](auto pos) { return row_emit(pos + prefix.size()); });
      } else {
        const auto& s = f.duck[row]; const sn::Bytes full{reinterpret_cast<const std::byte*>(s.GetData()), s.GetSize()};
        if constexpr (M == Method::duck_sz) sn::overlapping_find(full, q, sn::stringzilla_find, row_emit);
        else sn::overlapping_find(full, q, [](sn::Bytes h, sn::Bytes n) {
          return static_cast<const std::byte*>(memmem(h.data(), h.size(), n.data(), n.size()));
        }, row_emit);
      }
      if constexpr (O == Op::any) if (out) return;
    }
  }
}

template<Op O> void trial(const Fixture& f, const char* dataset, const char* query, sn::Bytes q, unsigned repeats) {
  const auto matches = f.oracle(q); Output<O> expected{};
  if constexpr (O == Op::any) expected = !matches.empty();
  if constexpr (O == Op::count) expected = matches.size();
  if constexpr (O == Op::ordered) expected = matches;
  if constexpr (O == Op::membership) for (auto [r, p] : matches) { (void)p; expected[r / 64] |= 1ull << (r % 64); }
  const sn::Kmp prepared(q);
  const auto method = [&]<Method M>(const char* name) {
    Output<O> out{};
    if constexpr (O == Op::ordered) out.reserve(matches.size()); // output capacity supplied by caller, no timed allocation
    const auto run = [&] { search<O, M>(f, q, prepared, out); escape(&out); if constexpr (O == Op::ordered) escape(out.data()); };
    run(); require(out == expected, "complete search exact natural output");
    for (unsigned rep = 0; rep < repeats; ++rep) {
      const double ns = measure(run); require(out == expected, "post timing exact search output");
      const char* op = O == Op::any ? "any" : O == Op::count ? "occurrences" : O == Op::membership ? "membership" : "ordered";
      std::printf("%s,%u,%s,%zu,%s,%s,%u,%.3f,%zu,%zu\n", dataset, f.capacity, query, q.size(), op, name, rep, ns, matches.size(), f.extent);
    }
  };
  method.template operator()<Method::body>("body-sparse");
  method.template operator()<Method::split_sz>("split-StringZilla-restart");
  method.template operator()<Method::duck_sz>("DuckDB-StringZilla-restart");
  method.template operator()<Method::duck_libc>("DuckDB-libc-restart");
  method.template operator()<Method::kmp>("split-KMP-prepared");
}

int main(int argc, char** argv) {
  const unsigned repeats = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 3;
  require(repeats > 0, "repetitions");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt");
  auto arena = take(oa::open_arena(128u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  std::fprintf(stderr, "Native reference: %s; ready queries, reused inverse and prepared KMP; ordered output capacity supplied.\n", sn::stringzilla_backend());
  std::puts("dataset,capacity,query,needle_bytes,operation,method,repetition,ns_per_query,matches,fixture_streampack_extent");
  for (unsigned n : {128, 256, 512}) for (unsigned width : {4, 32, 256})
    for (unsigned recipe : {0, 1, 2}) for (bool periodic : {false, true}) {
      Fixture f(*pool, n, width, recipe, periodic);
      const std::string dataset = "w" + std::to_string(width) + (recipe == 0 ? "-identity" : recipe == 1 ? "-prefix-shared" : "-prefix-shared-waste") + (periodic ? "-periodic" : "-binary");
      std::vector<std::pair<std::string, std::string>> queries{{"empty", ""}, {"absent", std::string(1, char(255))},
          {"dense", periodic ? "a" : std::string(1, char(7))}, {"pair", periodic ? "aa" : std::string(2, char(7))},
          {"periodic-miss", std::string(31, periodic ? 'a' : char(7)) + 'z'},
          {"long", std::string(65, periodic ? 'a' : char(7))}};
      if (f.model[7] && !f.model[7]->empty()) queries.emplace_back("sample-seam", f.model[7]->substr(0, 8));
      for (const auto& [name, q] : queries) {
        trial<Op::any>(f, dataset.c_str(), name.c_str(), bytes(q), repeats);
        trial<Op::membership>(f, dataset.c_str(), name.c_str(), bytes(q), repeats);
        trial<Op::count>(f, dataset.c_str(), name.c_str(), bytes(q), repeats);
        // Dense ordered materialization is substantial; include it for one
        // capacity, holding value count fixed across every competitor.
        if (n == 256) trial<Op::ordered>(f, dataset.c_str(), name.c_str(), bytes(q), repeats);
      }
    }
  for (;;) {
    auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close"); orbital::worker::poll(lane, 128); std::this_thread::yield();
  }
  require(arena->charged_bytes == 0, "search backing refund");
  require(bool(oa::remove_pool(*pool)), "remove pool"); arena.reset(); orbital::worker::relinquish(lane);
}
