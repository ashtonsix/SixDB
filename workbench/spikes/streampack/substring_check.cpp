#include "substring.h"
#include "search_reference.h"
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace sn = streampack_next;
using Match = std::pair<unsigned, unsigned>;
using Matches = std::vector<Match>;
using Model = std::vector<std::optional<std::string>>;
void require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
sn::Bytes bytes(const std::string& s) { return std::as_bytes(std::span(s)); }
Matches oracle(const Model& model, const std::string& q) {
  Matches result;
  for (unsigned r = 0; r < model.size(); ++r) if (model[r]) {
    const auto& s = *model[r];
    for (unsigned p = 0; p <= s.size(); ++p) {
      if (q.size() > s.size() - p) continue;
      bool same = true;
      for (unsigned j = 0; j < q.size(); ++j) if (s[p + j] != q[j]) { same = false; break; }
      if (same) result.emplace_back(r, p);
    }
  }
  return result;
}

void complete_schema(unsigned capacity, unsigned width, bool shared, bool split, bool periodic) {
  std::vector<std::uint32_t> ends(capacity + 1);
  std::vector<std::uint16_t> used(capacity), holders(capacity, 65535);
  std::vector<std::uint64_t> dead(capacity / 64);
  std::vector<std::byte> body(capacity * (width + 9));
  std::vector<std::string> suffix(capacity);
  std::vector<sn::Prefix> prefixes(capacity);
  std::vector<unsigned> refcounts(capacity);
  std::uint16_t count{};
  sn::MutableDirect<std::uint32_t> w{ends, body, used, dead, count}; ikea::no_effects effects;
  require(w.initialize(effects) == sn::Error::none, "initialize schema");
  std::mt19937 rng(7145);
  for (unsigned r = 0; r < capacity; ++r) {
    auto& s = suffix[r]; s.resize(r % 11 == 0 ? 0 : width);
    for (auto& c : s) c = periodic ? 'a' : char(rng() % 9);
    require(w.append(bytes(s), s.size() + r % 9, effects) == sn::Error::none, "schema suffix append");
  }
  Model model(capacity);
  for (unsigned row = 0; row < capacity; ++row) {
    if (row % 13 == 0) continue;
    const unsigned rank = shared ? (row * 17) % (capacity / 2) : row;
    holders[row] = rank; ++refcounts[rank];
    auto& p = prefixes[row]; p.size = split ? row % 5 : 0;
    std::string full;
    for (unsigned j = 0; j < p.size; ++j) {
      p.bytes[j] = std::byte(periodic ? 'a' : (row + j) % 9);
      full.push_back(char(p.bytes[j]));
    }
    full += suffix[rank]; model[row] = full;
  }
  for (unsigned r = 0; r < capacity; ++r) if (!refcounts[r])
    require(w.retire(r, effects) == sn::Error::none, "unused suffix retirement");
  std::vector<std::uint32_t> inverse_ends(capacity + 1), inverse_rows;
  for (unsigned r = 0; r < capacity; ++r) {
    for (unsigned row = 0; row < capacity; ++row) if (holders[row] == r) inverse_rows.push_back(row);
    inverse_ends[r + 1] = inverse_rows.size();
  }
  const auto view = w.read(); const sn::Fanout inverse{inverse_ends, inverse_rows};
  std::vector<std::uint8_t> admission_scratch(capacity);
  require(sn::admit_schema(view, prefixes, holders, inverse, admission_scratch) == sn::Error::none, "coherent schema admission");
  const auto saved = holders[1]; holders[1] = holders[2];
  require(sn::admit_schema(view, prefixes, holders, inverse, admission_scratch) != sn::Error::none, "retarget invalidates inverse");
  holders[1] = saved;
  std::vector<std::string> queries{"", "a", "aa", "aaa", "missing", std::string(1, '\0'),
      std::string(9, 'a'), std::string(65, 'a'), std::string(257, 'a')};
  // Prefix-only, crossing and suffix-only probes, with positions independently
  // selected in logical full strings. Include needles longer than every slot.
  for (unsigned r : {1, 7, 15, 33}) if (model[r]) {
    const auto& s = *model[r];
    for (unsigned p = 0; p < std::min<std::size_t>(7, s.size()); ++p)
      for (unsigned n : {1, 2, 3, 4, 8, 9, 16, 63, 64, 65, 255, 256, 257})
        queries.push_back(s.substr(p, n));
  }
  std::sort(queries.begin(), queries.end());
  queries.erase(std::unique(queries.begin(), queries.end()), queries.end());
  for (const auto& q : queries) {
    const auto expected = oracle(model, q);
    Matches actual;
    sn::schema_body_search(view, prefixes, holders, inverse, bytes(q),
        [&](auto r, auto p) { actual.emplace_back(r, p); return true; });
    std::sort(actual.begin(), actual.end());
    require(actual == expected, "complete shared/prefix body search exact ordered output");
    std::array<std::uint64_t, 8> membership{}, expected_membership{};
    std::uint64_t total = 0;
    sn::schema_body_search(view, prefixes, holders, inverse, bytes(q), [&](auto r, auto) {
      membership[r / 64] |= 1ull << (r % 64); ++total; return true;
    });
    for (const auto& [r, p] : expected) { (void)p; expected_membership[r / 64] |= 1ull << (r % 64); }
    require(membership == expected_membership && total == expected.size(), "complete membership/multiplicity");
    bool any = false;
    sn::schema_body_search(view, prefixes, holders, inverse, bytes(q), [&](auto, auto) { any = true; return false; });
    require(any == !expected.empty(), "complete any");
    const sn::Kmp prepared(bytes(q));
    actual.clear();
    for (unsigned row = 0; row < capacity; ++row) if (holders[row] != 65535) {
      const auto& p = prefixes[row];
      prepared.scan_parts({p.bytes.data(), p.size}, view.value(holders[row]),
          [&](auto pos) { actual.emplace_back(row, pos); return true; });
    }
    require(actual == expected, "two-span prepared linear overlapping control");
    actual.clear();
    for (unsigned row = 0; row < capacity; ++row) if (model[row])
      sn::overlapping_find(bytes(*model[row]), bytes(q), sn::stringzilla_find,
          [&](auto pos) { actual.emplace_back(row, pos); return true; });
    require(actual == expected, "pinned native StringZilla overlap adapter");
  }
}

void guarded_reference() {
  const auto page = std::size_t(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  require(memory != MAP_FAILED && mprotect(memory + page, page, PROT_READ | PROT_WRITE) == 0, "reference guards");
  auto* needle_memory = static_cast<char*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  require(needle_memory != MAP_FAILED && mprotect(needle_memory + page, page, PROT_READ | PROT_WRITE) == 0, "needle guards");
  for (unsigned width : {0, 1, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257})
    for (unsigned n : {0, 1, 2, 8, 9, 16, 31, 32, 63, 64, 65, 255, 256, 257}) {
      auto* begin = memory + page * 2 - width;
      std::fill(begin, begin + width, 'a');
      auto* needle = needle_memory + page * 2 - n; std::fill(needle, needle + n, 'a');
      const sn::Bytes q{reinterpret_cast<const std::byte*>(needle), n}; std::vector<unsigned> got;
      sn::overlapping_find({reinterpret_cast<const std::byte*>(begin), width}, q, sn::stringzilla_find,
          [&](auto p) { got.push_back(p); return true; });
      const auto expected = n <= width ? width - n + 1 : 0;
      require(got.size() == expected, "guarded native baseline matches");
      for (unsigned i = 0; i < got.size(); ++i) require(got[i] == i, "guarded ordered overlaps");
      const std::array<std::uint32_t, 2> ends{0, width};
      const sn::Direct<std::uint32_t> view{ends, {reinterpret_cast<const std::byte*>(begin), width}, {}, {}};
      got.clear();
      sn::substring_body(view, q, [&](auto rank, auto p) { require(rank == 0, "guarded substring rank"); got.push_back(p); return true; });
      require(got.size() == expected, "guarded actual substring consumer");
      for (unsigned i = 0; i < got.size(); ++i) require(got[i] == i, "guarded consumer ordered overlaps");
    }
  require(munmap(memory, page * 3) == 0, "unmap guards");
  require(munmap(needle_memory, page * 3) == 0, "unmap needle guards");
}
int main() {
  for (unsigned capacity : {128, 256, 512}) for (unsigned width : {0, 4, 32, 257})
    for (bool shared : {false, true}) for (bool split : {false, true}) for (bool periodic : {false, true})
      complete_schema(capacity, width, shared, split, periodic);
  guarded_reference();
  std::printf("StreamPack complete prefix/shared/absent search and %s guard/overlap parity passed\n", sn::stringzilla_backend());
}
