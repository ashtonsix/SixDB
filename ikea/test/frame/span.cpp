// Independent original-value witnesses for span execution.
// Deliberately uses the checked consumer API, not stage/image internals.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ikea/frame.h>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace fr = ikea::frame;
using u64 = std::uint64_t;
using u128 = fr::u128;
using coarse_selection = ikea::intervals::selection;
using fine_selection = ikea::seriespack::selection;
namespace {
std::size_t checks;
void check(bool ok, const char *why) {
  ++checks;
  if (!ok) {
    std::fprintf(stderr, "span_check: %s\n", why);
    std::abort();
  }
}
template <class T, class E> T take(std::expected<T, E> x) {
  check(bool(x), "unexpected refusal");
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
struct fixture {
  fr::description desc;
  std::vector<u64> values;
  fr::built_unit image;
  std::vector<std::size_t> extents;
  std::vector<fr::stream_manifest> streams;
  std::vector<fr::borrowed_part> parts;
  std::vector<fr::section_key> keys;
  fixture(unsigned form, unsigned spacing, unsigned pattern,
          std::size_t n = 9001)
      : desc(recipe(form, spacing)), values(n) {
    for (std::size_t r = 0; r < n; ++r) {
      if (pattern == 0)
        values[r] = (u64{1} << 40) + r / 3; // monotone, repeated boundaries
      if (pattern == 1)
        values[r] =
            (r / 256 % 4) * 1024 + r % 256; // sorted blocks, overlapping joins
      if (pattern == 3)
        values[r] = (r < 16 ? 10 : 100) + r % 16;
      if (pattern == 2)
        values[r] = (r % 257 == 0) ? ~u64{0} : u64(r * 0x9e3779b97f4a7c15ULL);
    }
    image = take(fr::build(desc, values, {0, 64, 0}));
    for (const auto &s : image.streams) {
      fr::stream_manifest m{s.root_node, s.rows, {}};
      for (std::size_t g = 0; g < s.groups.size(); ++g) {
        const auto id = static_cast<std::uint32_t>(parts.size());
        m.groups.push_back({id, 0, s.groups[g].size()});
        extents.push_back(s.groups[g].size());
        parts.push_back({id, s.groups[g]});
        keys.push_back({s.root_node, g});
      }
      streams.push_back(std::move(m));
    }
  }
  fr::reader bind(std::span<const fr::section_key> available) const {
    return take(fr::bind_reader(
        desc, {values.size(), extents, streams, parts, available}));
  }
  fr::reader bind() const { return bind(keys); }
};

std::expected<u128, fr::error> span_sum(const fr::reader &r, fr::row_range span,
                                        coarse_selection coarse,
                                        fine_selection fine) {
  return r.sum(span, coarse, fine);
}
bool meets(ikea::comparison op, u64 v, u64 t) {
  switch (op) {
  case ikea::comparison::equal:
    return v == t;
  case ikea::comparison::less:
    return v < t;
  case ikea::comparison::less_equal:
    return v <= t;
  case ikea::comparison::greater:
    return v > t;
  case ikea::comparison::greater_equal:
    return v >= t;
  }
  std::abort();
}

void answers(const fixture &f, const fr::reader &r, fr::row_range range,
             const ikea::bitset256 &coarse, const fr::block_rows_set *fine) {
  const auto blocks = coarse_selection::bits(0, coarse);
  const auto rows =
      fine ? fine_selection::bits(0, std::span<const ikea::bitset256>(*fine))
           : fine_selection::all();
  fr::summary want;
  std::vector<u64> gathered;
  for (std::size_t row = range.first; row < range.first + range.count; ++row)
    if (coarse.contains(unsigned(row / 256)) &&
        (!fine || (*fine)[row / 256].contains(unsigned(row % 256)))) {
      const auto v = f.values[row];
      want.sum += v;
      ++want.count;
      want.min = std::min(want.min, v);
      want.max = std::max(want.max, v);
      gathered.push_back(v);
    }
  check(take(span_sum(r, range, blocks, rows)) == want.sum,
        "range/coarse/fine SUM disagrees with original values");
  if (!fine) {
    for (const auto op :
         {ikea::comparison::equal, ikea::comparison::less,
          ikea::comparison::less_equal, ikea::comparison::greater,
          ikea::comparison::greater_equal})
      for (u64 t : {u64{0}, u64{1000}, u64{4096}, u64{1} << 40, ~u64{0}}) {
        u64 count = 0;
        for (const auto v : gathered)
          count += meets(op, v, t);
        check(take(r.count(op, t, range, blocks)) == count,
              "span COUNT disagrees with original values");
      }
  }
}
void all_shapes(unsigned form, unsigned spacing, unsigned pattern) {
  fixture f(form, spacing, pattern);
  const auto r = f.bind();
  const auto nb = unsigned((f.values.size() + 255) / 256);
  const auto all = ikea::bitset256::first_n(nb);
  ikea::bitset256 alt, outside_middle;
  for (unsigned b = 0; b < nb; ++b) {
    alt.set(b, b % 3 != 1);
    outside_middle.set(b, b / 16 != 1);
  }
  fr::block_rows_set periodic{}, sparse{};
  for (std::size_t row = 0; row < f.values.size(); ++row) {
    periodic[row / 256].set(unsigned(row % 256), row % 3 == 1);
    sparse[row / 256].set(unsigned(row % 256), row % 257 == 0);
  }
  sparse[0].set(0);
  sparse[0].set(1);
  sparse[0].set(3);
  sparse[0].set(16);
  // High padding bits must not become logical rows in the partial tail.
  periodic[nb - 1].set(255);
  sparse[nb - 1].set(255);
  const std::array ranges{fr::row_range{0, 0},
                          fr::row_range{f.values.size(), 0},
                          fr::row_range{0, f.values.size()},
                          fr::row_range{1, 1},
                          fr::row_range{15, 2},
                          fr::row_range{63, 2},
                          fr::row_range{255, 2},
                          fr::row_range{4095, 2},
                          fr::row_range{3900, 4600},
                          fr::row_range{4096, 4096},
                          fr::row_range{8190, f.values.size() - 8190},
                          fr::row_range{f.values.size() - 1, 1}};
  for (auto range : ranges)
    for (auto coarse : {all, alt, outside_middle, ikea::bitset256{}})
      for (const auto *fine : std::array<const fr::block_rows_set *, 3>{
               nullptr, &periodic, &sparse})
        answers(f, r, range, coarse, fine);
  const auto invalid = span_sum(r, {std::numeric_limits<std::size_t>::max(), 2},
                                coarse_selection::all(), fine_selection::all());
  check(!invalid && invalid.error() == fr::error::range,
        "overflowing range did not refuse");
}

void weighted_witness(unsigned form) {
  fixture f(form, 16, 3, 32);
  const auto r = f.bind();
  ikea::bitset256 selected;
  for (unsigned i : {0u, 1u, 3u, 16u})
    selected.set(i);
  check(
      take(r.sum(fr::row_range{0, 32}, coarse_selection::all(),
                 fine_selection::bits(0, selected))) == 134,
      "checkpoint selection collapsed multiplicities (expected 134, not 110)");
}
void borrowed_origin(unsigned form, unsigned spacing) {
  fixture f(form, spacing, 0);
  const auto r = f.bind();
  ikea::bitset256 rows;
  u128 want = 0;
  for (unsigned i = 0; i < 63; ++i)
    if (i % 3 == 1) {
      rows.set(i);
      want += f.values[4096 + i];
    }
  check(take(r.sum(fr::row_range{4096, 63}, coarse_selection::all(),
                   fine_selection::bits(4096, rows))) == want,
        "span required fine backing outside its intersecting coarse blocks");
}

// A coarse hole can make a one-block fine source sufficient for a span
// covering many blocks. The borrow fast path must prove both endpoints.
void short_borrow_with_coarse_holes() {
  fixture f(0, 16, 0);
  const auto r = f.bind();
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto *mapping = static_cast<std::byte *>(
      mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(mapping != MAP_FAILED, "short borrow guard mapping failed");
  check(mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0,
        "short borrow guard protection failed");
  auto *mask = std::construct_at(reinterpret_cast<ikea::bitset256 *>(
      mapping + 2 * page - sizeof(ikea::bitset256)));
  u128 want = 0;
  for (unsigned row = 0; row < 256; ++row)
    if (row % 3 == 1) {
      mask->set(row);
      want += f.values[row];
    }
  const auto coarse = ikea::bitset256::first_n(1);
  check(take(r.sum(fr::row_range{0, f.values.size()},
                   coarse_selection::bits(0, coarse),
                   fine_selection::bits(
                       0, std::span<const ikea::bitset256>(mask, 1)))) == want,
        "coarse holes did not permit a short borrowed fine source");
  std::destroy_at(mask);
  check(munmap(mapping, page * 3) == 0, "short borrow guard unmap failed");
}

void narrow_fine_window() {
  fixture f(0, 16, 0);
  const auto r = f.bind();
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto *mapping = static_cast<std::byte *>(
      mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  check(mapping != MAP_FAILED, "fine guard mapping failed");
  check(mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0,
        "fine guard protection failed");
  auto *word = reinterpret_cast<std::uint16_t *>(mapping + page);
  *word = 0xa55a;
  u128 want = 0;
  for (unsigned row = 17; row < 31; ++row)
    if ((*word >> (row - 16)) & 1)
      want += f.values[row];
  check(take(r.sum(fr::row_range{17, 14}, coarse_selection::all(),
                   fine_selection::bits(
                       16, std::span<const std::uint16_t>(word, 1)))) == want,
        "fine origin was rounded backward to an ungranted mask word");
  check(munmap(mapping, page * 3) == 0, "fine guard unmap failed");
}

void missing_middle(unsigned form, unsigned spacing) {
  fixture f(form, spacing, 0);
  std::size_t denied = 0;
  for (const auto &s : f.streams)
    if (s.root_node == f.desc.root)
      denied = s.groups[1].owner_part;
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  const auto extent = (f.extents[denied] + page - 1) / page * page;
  auto *guard =
      mmap(nullptr, extent, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  check(guard != MAP_FAILED, "guard mapping failed");
  // An unavailable section supplies no readable address. A mistaken middle
  // group touch faults, even though its placement metadata is still present.
  f.parts[denied].bytes = {static_cast<const std::byte *>(guard),
                           f.extents[denied]};
  auto keys = f.keys;
  keys.erase(
      std::remove(keys.begin(), keys.end(), fr::section_key{f.desc.root, 1}),
      keys.end());
  {
    const auto r = f.bind(keys);
    const auto nb = unsigned((f.values.size() + 255) / 256);
    ikea::bitset256 selected;
    for (unsigned b = 0; b < nb; ++b)
      selected.set(b, b / 16 != 1);
    answers(f, r, {3900, 4600}, selected, nullptr);
    constexpr u64 sentinel = 0x123456789abcdef0ULL;
    std::vector<u64> out(f.values.size(), sentinel);
    const auto refused = r.read(3900, std::span(out).first(4600));
    check(!refused && refused.error() == fr::error::unavailable,
          "read across missing group did not refuse");
    check(std::all_of(out.begin(), out.end(),
                      [&](u64 v) { return v == sentinel; }),
          "read wrote first group before discovering unavailable later group");
  }
  check(munmap(guard, extent) == 0, "guard unmap failed");
}

} // namespace
int main() {
  narrow_fine_window();
  short_borrow_with_coarse_holes();
  for (unsigned form : {0u, 1u, 2u, 3u})
    for (unsigned spacing : {16u, 64u}) {
      if (form < 2 && spacing != 16)
        continue;
      for (unsigned pattern : {0u, 1u, 2u})
        all_shapes(form, spacing, pattern);
      missing_middle(form, spacing);
      borrowed_origin(form, spacing);
      if (form >= 2 && spacing == 16)
        weighted_witness(form);
    }
  std::printf("span_check: %zu independent checks passed\n", checks);
}
