// Same query and source bytes on base and span arms. FRAME_SPAN_INTERFACE=0
// compiles against the current API; =1 exposes the span arm as well. The
// current caller lowers to coarse SUM plus genuinely partial row masks.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ikea/frame.h>
#include <optional>
#include <span>
#include <string>
#include <vector>
#ifndef FRAME_SPAN_INTERFACE
#define FRAME_SPAN_INTERFACE 0
#endif
namespace fr = ikea::frame;
using u64 = std::uint64_t;
using u128 = fr::u128;
namespace {
volatile u64 consumed;
void check(bool ok) {
  if (!ok)
    std::abort();
}
template <class T, class E> T take(std::expected<T, E> x) {
  check(bool(x));
  return std::move(*x);
}
fr::description recipe(unsigned form, unsigned spacing) {
#if defined(__aarch64__)
  constexpr auto preset = fr::pack_preset::bulk_arm;
#else
  constexpr auto preset = fr::pack_preset::bulk_x86;
#endif
  fr::description d;
  const auto basic = [&](bool bound) {
    const auto p = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({fr::node_kind::pack, std::uint16_t(preset), 0, {}});
    if (!bound)
      return p;
    const auto b = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back(
        {fr::node_kind::bound, 0, 0, {{p, fr::coordinate::identity}}});
    return b;
  };
  d.root = basic(form != 0);
  if (form >= 2) {
    const auto fields = d.root, cp = basic(true);
    d.root = static_cast<std::uint16_t>(d.nodes.size());
    d.nodes.push_back({form == 2 ? fr::node_kind::intervals_sizes
                                 : fr::node_kind::intervals_relative_ends,
                       static_cast<std::uint16_t>(spacing),
                       0,
                       {{fields, fr::coordinate::identity},
                        {cp, fr::coordinate::checkpoints}}});
  }
  return d;
}
struct image {
  fr::built_unit bytes;
  std::vector<std::size_t> extents;
  std::vector<fr::stream_manifest> streams;
  std::vector<fr::borrowed_part> parts;
  std::vector<fr::section_key> keys;
  std::optional<fr::reader> reader;
  u64 hash = 14695981039346656037ULL;
  std::size_t payload_bytes = 0;
  image(const fr::description &d, std::span<const u64> values)
      : bytes(take(fr::build(d, values, {0, 64, 0}))) {
    for (const auto &s : bytes.streams) {
      fr::stream_manifest m{s.root_node, s.rows, {}};
      for (std::size_t g = 0; g < s.groups.size(); ++g) {
        payload_bytes += s.groups[g].size();
        for (const auto byte : s.groups[g])
          hash =
              (hash ^ std::to_integer<unsigned char>(byte)) * 1099511628211ULL;
        const auto id = static_cast<std::uint32_t>(parts.size());
        m.groups.push_back({id, 0, s.groups[g].size()});
        extents.push_back(s.groups[g].size());
        parts.push_back({id, s.groups[g]});
        keys.push_back({s.root_node, g});
      }
      streams.push_back(std::move(m));
    }
    reader.emplace(take(
        fr::bind_reader(d, {values.size(), extents, streams, parts, keys})));
  }
};
struct query {
  const char *name;
  fr::row_range span;
  ikea::bitset256 coarse;
  const fr::block_rows_set *fine = nullptr;
};
struct counters {
  unsigned calls = 0;
  std::size_t mask_stores = 0;
};
// Keep the frozen harness compilable against base's bitset surface too.
ikea::bitset256 without(ikea::bitset256 a, const ikea::bitset256 &b) {
  for (unsigned i = 0; i < 4; ++i)
    a.words[i] &= ~b.words[i];
  return a;
}
// Reusable caller scratch is allocated outside timing. Its writes and all
// per-command lowering/classification are inside timing. No prepared query.
template <bool Count = false>
u128 current(const fr::reader &r, const query &q, fr::block_rows_set &scratch,
             counters *counts = nullptr) {
  const auto n = r.size(), end = q.span.first + q.span.count;
  ikea::bitset256 full, partial;
  if (!q.span.count)
    return 0;
  const auto b0 = unsigned(q.span.first / 256),
             b1 = unsigned((end - 1) / 256 + 1);
  const auto active = without(q.coarse & ikea::bitset256::first_n(b1),
                              ikea::bitset256::first_n(b0));
  if (!q.fine) {
    full = active;
    for (unsigned b : {b0, b1 - 1}) {
      if (!active.contains(b) || partial.contains(b))
        continue;
      const auto at = std::size_t(b) * 256, limit = std::min(at + 256, n);
      const unsigned lo = unsigned(std::max(at, q.span.first) - at),
                     hi = unsigned(std::min(limit, end) - at);
      if (lo || hi != limit - at) {
        scratch[b] =
            without(ikea::bitset256::first_n(hi), ikea::bitset256::first_n(lo));
        full.set(b, false);
        partial.set(b);
        if constexpr (Count)
          counts->mask_stores += sizeof(ikea::bitset256);
      }
    }
  } else {
    active.for_each([&](unsigned b) {
      const auto at = std::size_t(b) * 256, limit = std::min(at + 256, n);
      const unsigned lo = unsigned(std::max(at, q.span.first) - at),
                     hi = unsigned(std::min(limit, end) - at);
      const auto mask = without((*q.fine)[b] & ikea::bitset256::first_n(hi),
                                ikea::bitset256::first_n(lo));
      if (mask.empty())
        return;
      if (mask == ikea::bitset256::first_n(unsigned(limit - at)))
        full.set(b);
      else {
        partial.set(b);
        scratch[b] = mask;
        if constexpr (Count)
          counts->mask_stores += sizeof(ikea::bitset256);
      }
    });
  }
  u128 result = 0;
  if (!full.empty()) {
    result += take(r.sum(ikea::intervals::selection::bits(0, full)));
    if constexpr (Count)
      ++counts->calls;
  }
  if (!partial.empty()) {
    result +=
        take(r.sum_selected(ikea::seriespack::selection::bits(
                                0, std::span<const ikea::bitset256>(scratch)),
                            ikea::intervals::selection::bits(0, partial)));
    if constexpr (Count)
      ++counts->calls;
  }
  return result;
}
u128 candidate(const fr::reader &r, const query &q) {
#if FRAME_SPAN_INTERFACE
  const auto fine = q.fine ? ikea::seriespack::selection::bits(
                                 0, std::span<const ikea::bitset256>(*q.fine))
                           : ikea::seriespack::selection::all();
  return take(
      r.sum(q.span, ikea::intervals::selection::bits(0, q.coarse), fine));
#else
  (void)r;
  (void)q;
  std::abort();
#endif
}
void run(const char *arm, bool spans, unsigned rounds, unsigned form,
         unsigned spacing, unsigned pattern, std::size_t n) {
  std::vector<u64> values(n);
  for (std::size_t row = 0; row < n; ++row) {
    if (pattern == 0)
      values[row] = (u64{1} << 40) + row / 3;
    if (pattern == 1) {
      const unsigned width = std::array{3u, 13u, 33u, 64u}[row / 256 % 4];
      const auto mask = width == 64 ? ~u64{0} : (u64{1} << width) - 1;
      values[row] = (row * 0x9e3779b97f4a7c15ULL) & mask;
    }
  }
  const auto d = recipe(form, spacing);
  const image placed(d, values);
  const auto &r = *placed.reader;
  const unsigned nb = unsigned((n + 255) / 256);
  const auto all = ikea::bitset256::first_n(nb);
  ikea::bitset256 holes;
  for (unsigned b = 0; b < nb; ++b)
    holes.set(b, (b / 16) % 2 == 0);
  fr::block_rows_set fine{}, scratch;
  for (std::size_t row = 0; row < n; ++row)
    fine[row / 256].set(unsigned(row % 256), row < n / 2 || row % 3 == 1);
  const std::array queries{query{"sum_unit", {0, n}, all},
                           query{"sum_group", {4096, 4096}, all},
                           query{"sum_cross_group_edges", {3900, 4600}, all},
                           query{"sum_short_range", {4097, 63}, all},
                           query{"sum_coarse_holes", {0, n}, holes},
                           query{"sum_span_coarse_holes", {3900, 4600}, holes},
                           query{"sum_span_fine", {3900, 4600}, all, &fine},
                           query{"sum_clustered_fine", {0, n}, all, &fine},
                           query{"sum_tail", {n - 309, 309}, all}};
  for (const auto &q : queries) {
    u128 expected = 0;
    for (std::size_t row = q.span.first; row < q.span.first + q.span.count;
         ++row)
      if (q.coarse.contains(unsigned(row / 256)) &&
          (!q.fine || (*q.fine)[row / 256].contains(unsigned(row % 256))))
        expected += values[row];
    counters c;
    check(current<true>(r, q, scratch, &c) == expected);
    if (spans)
      check(candidate(r, q) == expected);
    const unsigned repeats = q.span.count < 256 ? 512 : 64;
    for (unsigned round = 0; round < rounds; ++round) {
      u64 digest = 0x517cc1b727220a95ULL;
      const auto begin = std::chrono::steady_clock::now();
      for (unsigned i = 0; i < repeats; ++i) {
        const u128 value = spans ? candidate(r, q) : current(r, q, scratch);
        digest =
            (digest << 7 | digest >> 57) + u64(value) + u64(value >> 64) + i;
      }
      const double ns = std::chrono::duration<double, std::nano>(
                            std::chrono::steady_clock::now() - begin)
                            .count() /
                        repeats;
      consumed = digest;
      std::printf("{\"column\":\"%s_%s%u_%zu\",\"op\":\"%s\",\"arm\":\"%s\","
                  "\"conditioning\":\"repeated\","
                  "\"round\":%u,\"ns\":%.3f,\"span_rows\":%zu,\"current_"
                  "consumer_calls\":%u,\"current_mask_store_bytes\":%zu,"
                  "\"image_bytes\":%zu,\"image_hash\":\"%016llx\"}\n",
                  pattern == 0 ? "monotone" : "mixed_width",
                  form == 0   ? "pack"
                  : form == 1 ? "bound"
                  : form == 2 ? "sizes"
                              : "relative",
                  form < 2 ? 0 : spacing, n, q.name, arm, round, ns,
                  q.span.count, c.calls, c.mask_stores, placed.payload_bytes,
                  static_cast<unsigned long long>(placed.hash));
    }
  }
}
} // namespace
int main(int argc, char **argv) {
  check(argc >= 3);
  const char *arm = argv[1];
  const bool spans = std::string(argv[2]) == "span";
  check(spans || std::string(argv[2]) == "current");
  const unsigned rounds = argc > 3 ? unsigned(std::stoul(argv[3])) : 1;
  for (unsigned form : {0u, 1u, 2u, 3u})
    for (unsigned spacing : {16u, 64u}) {
      if (form < 2 && spacing != 16)
        continue;
      for (unsigned pattern : {0u, 1u})
        for (std::size_t n : {std::size_t{9001}, std::size_t{65536}})
          run(arm, spans, rounds, form, spacing, pattern, n);
    }
}
