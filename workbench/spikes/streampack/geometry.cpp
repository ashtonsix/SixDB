#include "grouped.h"
#include "byte_search.h"
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace sn = streampack_next;
namespace oa = orbital::allocation;
void require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital result"); return std::move(*r); }
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
template<class Run> double measure(Run&& run) {
  unsigned iterations = 1;
  for (;;) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) run();
    const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    if (ns >= 8000000 || iterations >= 1048576) return ns / iterations;
    iterations *= 2;
  }
}

template<class Width, bool Relative> struct Encoded {
  std::array<Width, 512> entries{};
  std::array<std::uint32_t, 33> bases{};
  std::uint16_t count{};
  template<class View> auto encode(const View& source) {
    sn::GroupStorage<Width, Relative> into{entries, bases, count}; ikea::no_coverage ignored;
    return into.encode(source, ignored);
  }
  template<bool Used, bool Dead, class View> sn::Grouped<Width, Relative, Used, Dead> view(const View& source) const {
    const auto groups = (count + 15u) / 16;
    return {{entries.data(), groups * 16}, {bases.data(), groups + 1}, source.body, source.used, source.tombs, count};
  }
};

// Independent physical oracle: widths, used lengths and dead state, never
// decoded candidate geometry. Expanded owners are used for modest extents.
template<class View> void equal(const View& v, const std::vector<unsigned>& widths,
    const std::vector<std::uint16_t>& used, const std::vector<bool>& dead) {
  require(v.admit() == sn::Error::none && v.size() == widths.size(), "geometry admission/count");
  std::uint32_t offset = 0;
  for (unsigned r = 0; r < widths.size(); ++r) {
    require(v.start(r) == offset && v.reserved(r) == widths[r] && v.length(r) == used[r] && v.live(r) != dead[r], "exact forward geometry");
    for (unsigned j = 0; j < widths[r]; ++j) {
      if (widths[r] > 512 && j > 1 && j + 2 < widths[r] && j != widths[r] / 2) continue;
      const auto owner = v.reverse(offset + j);
      require(owner && owner->rank == r && owner->tombstone == dead[r], "independent byte ownership");
      if constexpr (requires { v.reverse_native(0); }) {
        const auto native = v.reverse_native(offset + j);
        require(native && native->rank == r && native->tombstone == dead[r], "independent native byte ownership");
      }
    }
    offset += widths[r];
  }
  require(v.extent() == offset && !v.reverse(offset) && !v.reverse(offset + 1), "no owner beyond extent");
  if constexpr (requires { v.reverse_native(0); }) {
    require(!v.reverse_native(offset) && !v.reverse_native(offset + 1) &&
        !v.reverse_native(std::numeric_limits<std::uint32_t>::max()), "native no owner at or beyond extent, including empty body");
  }
  const auto n = unsigned(widths.size());
  if (offset <= 65536 && (n <= 17 || std::has_single_bit(n) || n % 64 <= 1 || n % 64 == 63 || n == 49))
    for (const auto needle : {std::byte{0}, std::byte{7}, std::byte{255}}) {
    using Hit = std::pair<unsigned, unsigned>;
    std::vector<Hit> expected, actual;
    unsigned begin = 0;
    for (unsigned r = 0; r < widths.size(); ++r) {
      if (!dead[r]) for (unsigned j = 0; j < used[r]; ++j)
        if (v.body[begin + j] == needle) expected.emplace_back(r, j);
      begin += widths[r];
    }
    sn::byte_sparse(v, needle, [&](unsigned r, unsigned j) { actual.emplace_back(r, j); return true; });
    require(actual == expected, "grouped sparse scan exact rank-position output"); actual.clear();
    sn::byte_segmented(v, needle, [&](auto r, auto base, auto start, auto mask) {
      while (mask.bits) { actual.emplace_back(r, base + mask.first() - start); mask.drop_first(); } return true;
    });
    require(actual == expected, "grouped segmented scan exact rank-position output");
    unsigned calls = 0;
    const auto continued = sn::byte_segmented(v, needle, [&](auto, auto, auto, auto) { ++calls; return false; });
    require(calls == unsigned(!expected.empty()) && continued == expected.empty(), "grouped scan early stop");
  }
}

template<class Width, bool Used, bool Dead> void native_mutation(unsigned cap) {
  std::vector<Width> storage(cap, Width(123)); std::vector<std::uint32_t> bases(cap / 16 + 1, 0xabababab);
  std::vector<std::uint16_t> lengths(cap); std::vector<std::uint64_t> tombs(cap / 64);
  std::vector<std::byte> payload(cap * 40 + 65535, std::byte{0xcd}); std::uint16_t count = 0;
  sn::MutableWidths<Width, Used, Dead> dst{storage, bases, payload,
      Used ? std::span(lengths) : std::span<std::uint16_t>{}, Dead ? std::span(tombs) : std::span<std::uint64_t>{}, count};
  std::vector<unsigned> widths; std::vector<std::uint16_t> used; std::vector<bool> dead;
  std::vector<std::vector<std::byte>> values;
  std::array<ikea::owner_write, 2048> journal{};
  auto planes = [&] { return std::array<sn::Bytes, 6>{payload, std::as_bytes(std::span(storage)), std::as_bytes(std::span(lengths)),
      std::as_bytes(std::span(tombs)), std::as_bytes(std::span(&count, 1)), std::as_bytes(std::span(bases))}; };
  using Shadow = std::array<std::vector<std::byte>, 6>;
  auto snapshot = [&] { Shadow result; const auto source = planes();
    for (unsigned p = 0; p < source.size(); ++p) result[p].assign(source[p].begin(), source[p].end()); return result; };
  bool missing_pack_checkpoint_caught = false, missing_group_padding_caught = false;
  auto replay = [&](const Shadow& before, const auto& effects, bool packing = false, bool appending = false) {
    const auto after = planes();
    const auto apply = [&](std::optional<unsigned> omitted) {
      auto shadow = before; unsigned i = 0;
      for (const auto& e : effects.entries()) {
        require(e.source == &dst && e.bytes.plane < after.size(), "native issued source and plane identity");
        const auto p = e.bytes.plane; const auto offset = e.bytes.offset, n = e.bytes.size;
        if constexpr (!Used) require(p != 2, "omitted used plane issues no writes");
        if constexpr (!Dead) require(p != 3, "omitted dead plane issues no writes");
        require(offset <= after[p].size() && n <= after[p].size() - offset, "native issued extent");
        if (!omitted || *omitted != i) std::copy_n(after[p].begin() + offset, n, shadow[p].begin() + offset);
        ++i;
      }
      for (unsigned p = 0; p < after.size(); ++p)
        if (!std::equal(after[p].begin(), after[p].end(), shadow[p].begin())) return false;
      return true;
    };
    require(apply(std::nullopt), "six-plane afterimage replay exactly reconstructs native mutation");
    unsigned i = 0;
    for (const auto& e : effects.entries()) {
      const bool pack_candidate = packing && !missing_pack_checkpoint_caught && e.bytes.plane == 5;
      const bool padding_candidate = appending && !missing_group_padding_caught && e.bytes.plane == 1 && e.bytes.size == 16 * sizeof(Width);
      if (pack_candidate || padding_candidate) {
        const auto p = e.bytes.plane; const auto offset = e.bytes.offset, n = e.bytes.size;
        if (!std::equal(after[p].begin() + offset, after[p].begin() + offset + n, before[p].begin() + offset)) {
          require(!apply(i), "omitted changed checkpoint/padding afterimage breaks reconstruction");
          if (pack_candidate) missing_pack_checkpoint_caught = true;
          if (padding_candidate) missing_group_padding_caught = true;
        }
      }
      ++i;
    }
  };
  auto verify = [&] {
    const auto view = dst.read(); equal(view, widths, used, dead);
    for (unsigned r = 0; r < count; ++r) {
      const auto actual = view.value(r);
      require(actual.size() == values[r].size() && std::equal(actual.begin(), actual.end(), values[r].begin()), "native width writer exact payload");
    }
  };
  const auto initial = snapshot();
  ikea::source_write_journal initialize{journal}; require(dst.initialize(initialize) == sn::Error::none, "native widths initialize"); replay(initial, initialize);
  verify();
  for (unsigned r = 0; r < cap; ++r) {
    if (r == 17) {
      // A partial last group, positive live slack and non-shrinking packing.
      // Initialize distinctive slack as fixture preparation, then require its
      // exact survival in moved live reservations, including later appends.
      std::vector<std::vector<std::byte>> reservations; unsigned offset = 0;
      for (unsigned row = 0; row < count; ++row) {
        std::fill(payload.begin() + offset + used[row], payload.begin() + offset + widths[row], std::byte{0x9a});
        reservations.emplace_back(payload.begin() + offset, payload.begin() + offset + widths[row]); offset += widths[row];
      }
      if constexpr (Dead) for (unsigned row : {2u, 9u}) {
        const auto before = snapshot(); ikea::source_write_journal effects{journal};
        require(dst.retire(row, effects) == sn::Error::none, "partial-group retirement"); replay(before, effects); dead[row] = true;
      }
      const auto before = snapshot(); ikea::source_write_journal effects{journal};
      require(sn::soft_pack(dst, false, effects) == sn::Error::none, "partial-group reservation-preserving pack"); replay(before, effects, true);
      offset = 0;
      for (unsigned row = 0; row < count; ++row) {
        if (dead[row]) { widths[row] = used[row] = 0; values[row].clear(); }
        else {
          require(std::equal(reservations[row].begin(), reservations[row].end(), payload.begin() + offset), "live slack survives stable movement");
          offset += widths[row];
        }
      }
      verify();
    }
    if (r == cap / 2) {
      for (unsigned row = 0; row < count; ++row) {
        if (dead[row]) continue;
        const auto before = snapshot();
        ikea::source_write_journal effects{journal};
        if constexpr (Dead) if (row % 3 == 0) {
          require(dst.retire(row, effects) == sn::Error::none, "native retirement"); dead[row] = true;
          replay(before, effects);
          continue;
        }
        auto replacement = values[row];
        if constexpr (Used) replacement.resize(replacement.size() / 2);
        if (!replacement.empty()) replacement[0] = std::byte{0x5e};
        require(dst.template replace<true>(row, replacement, effects) == sn::Error::none, "native same-reservation replacement");
        replay(before, effects);
        used[row] = replacement.size(); values[row] = std::move(replacement);
      }
      verify(); const auto before = snapshot(); ikea::source_write_journal packing{journal};
      require(sn::soft_pack(dst, true, packing) == sn::Error::none, "native checkpoint soft packing");
      replay(before, packing, true);
      for (unsigned row = 0; row < count; ++row) {
        if (dead[row]) { widths[row] = used[row] = 0; values[row].clear(); }
        else widths[row] = used[row];
      }
      verify();
    }
    const unsigned reservation = r == 17 ? std::numeric_limits<Width>::max() : r % 19;
    const unsigned n = Used ? reservation / 2 : reservation;
    std::vector<std::byte> input(n, std::byte(r & 255));
    const auto before = snapshot();
    if (r == 17) {
      ikea::source_write_journal empty{std::span<ikea::owner_write>{}};
      require(dst.append(input, reservation, empty) == sn::Error::effects && snapshot() == before && empty.entries().empty(),
          "insufficient append journal preserves every plane and count");
    }
    ikea::source_write_journal effects{journal};
    const auto before_count = count;
    require(dst.append(input, reservation, effects) == sn::Error::none, "native checkpoint append through group boundary");
    replay(before, effects, false, true);
    bool checkpoint = false;
    for (const auto& e : effects.entries()) {
      require(e.source == &dst, "native width source-qualified issued writes");
      checkpoint |= e.bytes.plane == 5 && e.bytes.offset == (before_count / 16 + 1) * 4 && e.bytes.size == 4;
    }
    require(checkpoint, "append publishes checkpoint extent");
    widths.push_back(reservation); used.push_back(n); dead.push_back(false); values.push_back(std::move(input)); verify();
    if (r == 18) {
      const auto all_before = snapshot(); ikea::source_write_journal empty{std::span<ikea::owner_write>{}};
      require(dst.replace(r, values[r], empty) == sn::Error::effects && snapshot() == all_before && empty.entries().empty(),
          "insufficient replacement journal preserves every plane and count");
    }
  }
  const auto all_before = snapshot();
  ikea::source_write_journal effects{journal};
  require(dst.append({}, 0, effects) == sn::Error::capacity && effects.entries().empty(), "native full-rank refusal");
  ikea::source_write_journal empty{std::span<ikea::owner_write>{}};
  require(sn::soft_pack(dst, false, empty) == sn::Error::effects && snapshot() == all_before && empty.entries().empty(),
      "native soft journal refusal is atomic");
  const unsigned row = cap - 2; // positive live reservation, never touched by earlier retirement
  auto self = dst.read().value(row);
  require(dst.replace(row, self, effects) == sn::Error::none, "native self-body replacement"); verify();
  const auto alias = std::as_bytes(std::span(bases)).first(1);
  require(dst.replace(row, alias, effects) == (Used ? sn::Error::alias : sn::Error::length), "native metadata alias refuses");
  require(missing_group_padding_caught, "changed append padding is a tested publication requirement");
  if constexpr (Used || Dead) require(missing_pack_checkpoint_caught, "changed compacted checkpoint is a tested publication requirement");
}

template<class Width> void maximum_body(unsigned cap) {
  constexpr unsigned maximum = std::numeric_limits<Width>::max();
  std::vector<Width> widths(cap); std::vector<std::uint32_t> bases(cap / 16 + 1);
  std::vector<std::byte> body(std::size_t(cap) * maximum), input(maximum, std::byte{0xa7});
  std::vector<std::uint16_t> used(cap); std::vector<std::uint64_t> tombs(cap / 64); std::uint16_t count = 0;
  sn::MutableWidths<Width> w{widths, bases, body, used, tombs, count}; ikea::no_coverage no;
  require(w.initialize(no) == sn::Error::none, "maximum-body initialize");
  for (unsigned r = 0; r < cap; ++r) require(w.append(input, maximum, no) == sn::Error::none, "maximum-width append to full rank capacity");
  for (unsigned phase = 0; phase < 2; ++phase) {
    const auto v = w.read(); require(v.admit() == sn::Error::none, "maximum-body admission");
    unsigned offset = 0;
    for (unsigned r = 0; r < cap; ++r) {
      const bool dead = phase && r % 3 == 0; const auto n = dead ? 0 : maximum;
      require(v.start(r) == offset && v.reserved(r) == n && v.length(r) == n && v.live(r) != dead, "wide checkpoint sums and preserved ranks");
      if (n) {
        const auto first = v.reverse(offset), last = v.reverse(offset + n - 1);
        require(first && last && first->rank == r && last->rank == r && !first->tombstone && !last->tombstone, "maximum-width owner boundaries");
        require(std::all_of(v.value(r).begin(), v.value(r).end(), [](auto x) { return x == std::byte{0xa7}; }), "maximum-body copied bytes");
      }
      offset += n;
    }
    require(v.extent() == offset && !v.reverse(offset), "maximum-body final extent");
    if (phase == 0) {
      for (unsigned r = 0; r < cap; r += 3) require(w.retire(r, no) == sn::Error::none, "maximum-body retire");
      require(sn::soft_pack(w, false, no) == sn::Error::none, "maximum-body soft pack");
    }
  }
}

void correctness() {
  for (unsigned cap : {128, 256, 512}) {
    maximum_body<std::uint8_t>(cap); maximum_body<std::uint16_t>(cap);
    native_mutation<std::uint8_t, true, true>(cap);
    native_mutation<std::uint16_t, true, true>(cap);
    native_mutation<std::uint8_t, false, false>(cap);
    native_mutation<std::uint8_t, true, false>(cap);
    native_mutation<std::uint8_t, false, true>(cap);
  }
  for (unsigned pattern : {0, 1, 2}) {
    // Deliberately unaligned starts and maximum unsigned lanes expose incorrect
    // signed multiply-add reductions, byte overflow and masking of later lanes.
    std::array<std::uint8_t, 17> small{}; std::array<std::uint16_t, 17> wide{};
    for (unsigned i = 0; i < 16; ++i) {
      small[i + 1] = pattern == 0 ? 255 : pattern == 1 ? i * 17 : i % 3 ? 0 : 253;
      wide[i + 1] = pattern == 0 ? 65535 : pattern == 1 ? i * 4369 : i % 3 ? 0 : 65497;
    }
    unsigned sum8 = 0, sum16 = 0;
    for (unsigned n = 0; n <= 16; ++n) {
      require(sn::prefix16_native(small.data() + 1, n) == sum8 && sn::prefix16_native(wide.data() + 1, n) == sum16,
          "widened masked prefix with full unsigned range");
      if (n < 16) { sum8 += small[n + 1]; sum16 += wide[n + 1]; }
    }
    for (unsigned x = 0; x <= sum8; ++x) {
      unsigned rank = 0, end = 0;
      for (; rank < 16; ++rank) { end += small[rank + 1]; if (x < end) break; }
      require(sn::owner16_native(small.data() + 1, x) == rank, "all offsets through widened native inclusive prefixes");
    }
  }
  for (unsigned n : {0, 1, 15, 16, 17, 63, 64, 65, 127, 128, 255, 256, 511, 512})
    for (unsigned population : {0, 1, 2, 3}) {
      std::vector<unsigned> widths(n); std::vector<std::uint16_t> used(n);
      std::vector<bool> dead(n); std::vector<std::uint64_t> tombs((n + 63) / 64), present((n + 63) / 64);
      std::vector<std::uint32_t> ends{0};
      for (unsigned r = 0; r < n; ++r) {
        widths[r] = population == 0 ? r % 32 : population == 1 ? (r % 19 ? 4095 : 0) : population == 2 ? 32 : (r % 3 ? 32 : 0);
        used[r] = widths[r] ? widths[r] - r % 2 : 0; dead[r] = r % 5 == 0;
        ends.push_back(ends.back() + widths[r]);
        if (dead[r]) tombs[r / 64] |= 1ull << (r % 64);
        if (widths[r]) present[r / 64] |= 1ull << (r % 64);
      }
      std::vector<std::byte> body(ends.back());
      for (unsigned i = 0; i < body.size(); ++i) body[i] = std::byte(i % 13);
      const sn::Direct<std::uint32_t, true, true> direct{ends, body, used, tombs};
      equal(direct, widths, used, dead);
      Encoded<std::uint8_t, false> w8;
      if (population != 1) { require(w8.encode(direct) == sn::Error::none, "u8 build"); equal(w8.view<true, true>(direct), widths, used, dead); }
      Encoded<std::uint16_t, false> w16; Encoded<std::uint16_t, true> relative;
      require(w16.encode(direct) == sn::Error::none && relative.encode(direct) == sn::Error::none, "grouped build");
      equal(w16.view<true, true>(direct), widths, used, dead); equal(relative.view<true, true>(direct), widths, used, dead);
      if (population == 2) equal(sn::Implicit<false, true, true>{std::uint16_t(n), 32, {}, tombs, used, body}, widths, used, dead);
      if (population == 3) equal(sn::Implicit<true, true, true>{std::uint16_t(n), 32, present, tombs, used, body}, widths, used, dead);
    }
  // Largest legal relative endpoint, then overflow within a group and success
  // at the next group. Refusal preserves destination bytes/count/effect output.
  std::array<std::uint32_t, 18> ends{};
  for (unsigned i = 1; i <= 16; ++i) ends[i] = 65535;
  ends[17] = 65536;
  std::vector<std::byte> body(65536);
  sn::Direct<std::uint32_t> source{ends, body, {}, {}};
  Encoded<std::uint16_t, true> relative;
  require(relative.encode(source) == sn::Error::none && relative.view<false, false>(source).start(16) == 65535, "new group after max extent");
  ends[2] = 65536; for (unsigned i = 3; i <= 17; ++i) ends[i] = 65536;
  const auto before = relative;
  require(relative.encode(source) == sn::Error::length && relative.entries == before.entries && relative.bases == before.bases && relative.count == before.count,
      "relative overflow refusal is atomic");
  {
    std::vector<unsigned> widths(49, 4); std::vector<std::uint16_t> used(49, 4); std::vector<bool> dead(49);
    for (unsigned r = 16; r < 32; ++r) widths[r] = used[r] = 0;
    std::vector<std::uint32_t> ends{0}; for (auto n : widths) ends.push_back(ends.back() + n);
    std::vector<std::byte> body(ends.back()); std::array<std::uint64_t, 1> tombs{};
    sn::Direct<std::uint32_t, true, true> source{ends, body, used, tombs};
    Encoded<std::uint8_t, false> widths8; Encoded<std::uint16_t, true> relative;
    require(widths8.encode(source) == sn::Error::none && relative.encode(source) == sn::Error::none, "empty internal group build");
    std::fill(widths8.entries.begin() + 49, widths8.entries.begin() + 64, 255);
    std::fill(relative.entries.begin() + 49, relative.entries.begin() + 64, 65535);
    equal(widths8.view<true, true>(source), widths, used, dead); equal(relative.view<true, true>(source), widths, used, dead);
    const auto before = widths8; std::array<ikea::owner_write, 2> records{}; ikea::source_write_journal effects{records};
    sn::GroupStorage<std::uint8_t, false> output{widths8.entries, widths8.bases, widths8.count};
    require(output.encode(source, effects) == sn::Error::effects && effects.entries().empty() && widths8.entries == before.entries &&
        widths8.bases == before.bases && widths8.count == before.count, "effect-capacity refusal preserves all destination planes");
  }
  // Prescribed source mutation: append through a checkpoint, shorten, retire,
  // pack then continue append. Ordinary edits need no geometry re-encoding.
  for (unsigned capacity : {128, 256, 512}) {
    std::vector<std::uint32_t> e(capacity + 1); std::vector<std::uint16_t> u(capacity);
    std::vector<std::uint64_t> t(capacity / 64); std::vector<std::byte> b(capacity * 32);
    std::uint16_t count{}; sn::MutableDirect<std::uint32_t> w{e, b, u, t, count}; ikea::no_coverage no;
    require(w.initialize(no) == sn::Error::none, "history initialize");
    std::vector<unsigned> widths; std::vector<std::uint16_t> lengths; std::vector<bool> dead;
    Encoded<std::uint8_t, false> packed;
    std::array<std::byte, 9> bytes{};
    for (unsigned r = 0; r < 18; ++r) {
      require(w.append(bytes, 16, no) == sn::Error::none, "history append"); widths.push_back(16); lengths.push_back(9); dead.push_back(false);
      require(packed.encode(w.read()) == sn::Error::none, "tail geometry build"); equal(packed.view<true, true>(w.read()), widths, lengths, dead);
    }
    const auto numeric = packed;
    require(w.replace(3, {}, no) == sn::Error::none && w.retire(15, no) == sn::Error::none, "history edits"); lengths[3] = 0; dead[15] = true;
    equal(packed.view<true, true>(w.read()), widths, lengths, dead);
    require(packed.entries == numeric.entries && packed.bases == numeric.bases, "unchanged geometry after same reservation edits");
    require(sn::soft_pack(w, false, no) == sn::Error::none, "history soft pack"); widths[15] = lengths[15] = 0;
    require(packed.encode(w.read()) == sn::Error::none, "compacted geometry build"); equal(packed.view<true, true>(w.read()), widths, lengths, dead);
    require(w.append(bytes, 16, no) == sn::Error::none, "append after pack"); widths.push_back(16); lengths.push_back(9); dead.push_back(false);
    require(packed.encode(w.read()) == sn::Error::none, "continued geometry build"); equal(packed.view<true, true>(w.read()), widths, lengths, dead);
  }
}

template<class View> __attribute__((noinline)) void dependent(const View& v, std::span<const unsigned> next,
    std::span<std::uint32_t> output) {
  unsigned r = 0;
  for (auto& into : output) {
    const auto value = v.value(r); const auto tag = value.empty() ? 0 : std::to_integer<unsigned>(value[0]);
    into = unsigned(value.size()) | (tag << 16); r = next[r] ^ tag;
  }
}
template<class View> __attribute__((noinline)) void reverse(const View& v, std::span<const std::uint32_t> offsets,
    std::span<std::uint32_t> output) {
  for (unsigned i = 0; i < offsets.size(); ++i) {
    auto o = v.reverse(offsets[i]); output[i] = o ? o->rank | (unsigned(o->tombstone) << 16) : ~0u;
  }
}
template<class View> __attribute__((noinline)) void contiguous(const View& v, std::span<std::uint32_t> ends,
    std::span<std::byte> projected) {
  // Full-reservation projection: lengths/ends are the output, bytes are copied
  // once. Widths use a running sum; explicit endpoints do not reconstruct one.
  if constexpr (requires { v.ends; }) {
    for (unsigned r = 0; r < v.size(); ++r) ends[r] = v.ends[r + 1];
  } else if constexpr (requires { v.bases; } && requires { View::group; }) {
    if constexpr (View::relative) {
      for (unsigned first = 0; first < v.size(); first += 16)
        for (unsigned r = first; r < std::min<unsigned>(v.size(), first + 16); ++r) ends[r] = v.bases[first / 16] + v.values[r];
    } else {
      std::uint32_t offset = 0;
      for (unsigned r = 0; r < v.size(); ++r) { offset += v.reserved(r); ends[r] = offset; }
    }
  } else {
    std::uint32_t offset = 0;
    for (unsigned r = 0; r < v.size(); ++r) { offset += v.reserved(r); ends[r] = offset; }
  }
  if (v.extent()) std::memcpy(projected.data(), v.body.data(), v.extent());
}
template<class View> __attribute__((noinline)) std::uint64_t monotone(const View& v, std::byte needle) {
  std::uint64_t count = 0;
  {
    unsigned r = 0; std::uint32_t group_base = 0;
    const auto next_end = [&](unsigned rank, unsigned previous) -> unsigned {
      if constexpr (requires { v.ends; }) return v.ends[rank + 1];
      else if constexpr (requires { View::relative; }) {
        if constexpr (View::relative) {
          if (rank % 16 == 0) group_base = v.bases[rank / 16];
          return group_base + v.values[rank];
        } else return previous + v.reserved(rank);
      } else return previous + v.reserved(rank);
    };
    std::uint32_t end = v.size() ? next_end(0, 0) : 0;
    sn::byte_masks(v.body.first(v.extent()), needle, [&](auto base, auto mask, auto) {
      while (mask.bits) {
        const auto offset = base + mask.first(); mask.drop_first();
        while (end <= offset) { ++r; end = next_end(r, end); }
        if (v.live(r)) ++count;
      }
      return true;
    });
  }
  return count;
}

void pilot(oa::Pool& pool, unsigned repeats) {
  std::puts("population,capacity,operation,method,repetition,ns_per_item,items,geometry_bytes,body_bytes");
  for (unsigned capacity : {128, 256, 512}) for (unsigned population : {0, 1, 2, 3}) {
    std::vector<unsigned> widths(capacity), next(capacity), cycle(capacity);
    std::vector<std::uint32_t> ends{0}; std::vector<std::uint16_t> ends16{0};
    std::vector<std::uint64_t> dead(capacity / 64), present(capacity / 64);
    for (unsigned r = 0; r < capacity; ++r) {
      widths[r] = population == 0 ? r % 64 : population == 1 ? 2048 + r % 2048 : population == 2 ? 32 : (r % 3 ? 32 : 0);
      ends.push_back(ends.back() + widths[r]); ends16.push_back(ends.back());
      if (r % 5 == 0) dead[r / 64] |= 1ull << (r % 64);
      if (widths[r]) present[r / 64] |= 1ull << (r % 64);
    }
    auto allocation = take(oa::allocate(pool, {.bytes = ends.back(), .alignment = 64}));
    const std::span<std::byte> body{allocation.address, ends.back()}; std::mt19937 rng(6341);
    for (auto& b : body) b = std::byte(rng() % 64);
    const sn::Direct<std::uint32_t, false, true> direct{ends, body, {}, dead};
    Encoded<std::uint8_t, false> w8; Encoded<std::uint16_t, false> w16; Encoded<std::uint16_t, true> rel;
    require(w16.encode(direct) == sn::Error::none && rel.encode(direct) == sn::Error::none, "benchmark encode");
    std::iota(cycle.begin(), cycle.end(), 0); std::shuffle(cycle.begin() + 1, cycle.end(), rng);
    for (unsigned i = 0; i < capacity; ++i) { const auto r = cycle[i]; next[r] = cycle[(i + 1) % capacity] ^ (widths[r] ? std::to_integer<unsigned>(body[ends[r]]) : 0); }
    std::vector<std::uint32_t> offsets(4096), reverse_expected(4096), dependent_expected(4096), output(4096);
    unsigned row = 0;
    for (unsigned i = 0; i < offsets.size(); ++i) {
      offsets[i] = i % 17 ? rng() % ends.back() : ends.back();
      unsigned r = 0, end = 0; for (; r < capacity; ++r) { end += widths[r]; if (offsets[i] < end) break; }
      reverse_expected[i] = r == capacity ? ~0u : r | (unsigned(r % 5 == 0) << 16);
      dependent_expected[i] = widths[row] | (widths[row] ? std::to_integer<unsigned>(body[ends[row]]) << 16 : 0);
      row = cycle[(i + 1) % capacity];
    }
    std::vector<std::byte> projected(body.size());
    const char* name = population == 0 ? "tiny-variable" : population == 1 ? "over64k-variable" : population == 2 ? "fixed32" : "packed-optional32";
    const auto method = [&](const auto& view, const char* method_name, std::size_t geometry_bytes) {
      require(view.admit() == sn::Error::none, "benchmark geometry admission");
      const auto trial = [&](const char* op, unsigned items, auto run, auto verify) {
        run(); verify();
        for (unsigned rep = 0; rep < repeats; ++rep) {
          const auto ns = measure(run); verify();
          std::printf("%s,%u,%s,%s,%u,%.3f,%u,%zu,%zu\n", name, capacity, op, method_name, rep, ns / items, items, geometry_bytes, body.size());
        }
      };
      trial("dependent-span", 4096, [&] { dependent(view, next, output); escape(output.data()); }, [&] { require(output == dependent_expected, "dependent exact order"); });
      trial("random-reverse", 4096, [&] { reverse(view, offsets, output); escape(output.data()); }, [&] { require(output == reverse_expected, "reverse exact pairs"); });
      trial("contiguous-project", capacity, [&] { contiguous(view, {output.data(), capacity}, projected); escape(output.data()); escape(projected.data()); }, [&] {
        require(std::equal(output.begin(), output.begin() + capacity, ends.begin() + 1) && std::equal(projected.begin(), projected.end(), body.begin()), "range exact ends/bytes");
      });
      for (auto q : {std::byte{7}, std::byte{255}}) {
        std::uint64_t expected = 0, actual = 0;
        for (unsigned r = 0; r < capacity; ++r) if (r % 5) for (auto b : body.subspan(ends[r], widths[r])) expected += b == q;
        trial(q == std::byte{7} ? "monotone-one-in64" : "monotone-miss", 1, [&] { actual = monotone(view, q); escape(&actual); }, [&] { require(actual == expected, "monotone exact count"); });
      }
    };
    method(direct, "direct32", ends.size() * 4);
    if (ends.back() <= 65535) method(sn::Direct<std::uint16_t, false, true>{ends16, body, {}, dead}, "direct16", ends16.size() * 2);
    method(w16.view<false, true>(direct), "width16-g16", capacity * 2 + (capacity / 16 + 1) * 4);
    method(rel.view<false, true>(direct), "relative16-g16", capacity * 2 + (capacity / 16 + 1) * 4);
    if (population != 1) { require(w8.encode(direct) == sn::Error::none, "byte width encode"); method(w8.view<false, true>(direct), "width8-g16", capacity + (capacity / 16 + 1) * 4); }
    if (population == 2) method(sn::Implicit<false, false, true>{std::uint16_t(capacity), 32, {}, dead, {}, body}, "implicit32", 2);
    if (population == 3) method(sn::Implicit<true, false, true>{std::uint16_t(capacity), 32, present, dead, {}, body}, "presence32", present.size() * 8 + 2);
  }
}
int main(int argc, char** argv) {
  correctness();
  if (argc < 2 || std::string(argv[1]) == "check") { std::puts("StreamPack grouped/implicit exact geometry, boundaries, refusal and mutation-derived histories passed"); return 0; }
  const unsigned repeats = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 3; require(repeats > 0, "repetitions");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt");
  auto arena = take(oa::open_arena(64u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  pilot(*pool, repeats);
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "geometry backing refund"); require(bool(oa::remove_pool(*pool)), "remove pool");
  arena.reset(); orbital::worker::relinquish(lane);
}
