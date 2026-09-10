#include "../algebra.h"
#include "../analyse.h"
#include "../metadata_point.h"
#include "../selection.h"
#include "../storage_decision.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

using namespace ikea::heterogeneous;
namespace {
using Window = std::array<PlainBlock, 256>;
using Result = std::array<std::uint8_t, 8192>;
constexpr std::array kinds{MetadataKind::direct32, MetadataKind::local16,
                           MetadataKind::scan128};
constexpr std::array operations{SetOperation::intersection, SetOperation::set_union};
constexpr std::array resolutions{Resolution::point, Resolution::cached16};

void require(bool okay, const char* what) {
  if (!okay) {
    std::fprintf(stderr, "Operation check failed: %s\n", what);
    std::abort();
  }
}
std::uint64_t random(std::uint64_t& state) {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * UINT64_C(2685821657736338717);
}
Window make_window(unsigned seed) {
  Window result{};
  std::uint64_t state = UINT64_C(0x41bec65536256) + seed;
  for (unsigned i = 0; i < 256; ++i) {
    unsigned pop = (i * (seed ? 151 : 73) + (seed ? 256 : 19)) % 257;
    // Terminals on both sides of the selection and metadata boundaries.
    if (i % 64 == 0 || i == 15 || i == 254) pop = seed ? 256 : 0;
    if (i % 64 == 63 || i == 16 || i == 255) pop = seed ? 0 : 256;
    std::array<unsigned, 256> positions;
    for (unsigned j = 0; j < 256; ++j) positions[j] = j;
    for (unsigned j = 255; j; --j)
      std::swap(positions[j], positions[random(state) % (j + 1)]);
    for (unsigned j = 0; j < pop; ++j)
      result[i][positions[j] / 8] |= std::uint8_t(1u << (positions[j] % 8));
  }
  return result;
}
struct Sources {
  std::shared_ptr<AlignedBytes> plain = std::make_shared<AlignedBytes>(8192);
  EncodedSource encoded;
  std::array<std::shared_ptr<MetadataOwner>, 3> metadata;
  std::array<AdmittedBitset, 4> admitted; // Plain, Direct, Local, Scan.
  explicit Sources(const Window& values) : encoded(encode_source(values)) {
    for (unsigned i = 0; i < 256; ++i)
      std::memcpy(plain->data() + 32 * i, values[i].data(), 32);
    require(admit_plain(plain, admitted[0]) == AdmissionError::none, "plain admission");
    for (unsigned k = 0; k < 3; ++k) {
      metadata[k] = std::make_shared<MetadataOwner>(encoded, kinds[k]);
      require(admit_bec(metadata[k], encoded.body(), admitted[k + 1]) == AdmissionError::none,
              "BEC admission");
    }
  }
};
bool selected(const SliceMask& mask, unsigned i) {
  return (mask[i / 64] >> (i % 64)) & 1;
}
void select(SliceMask& mask, unsigned i) { mask[i / 64] |= UINT64_C(1) << (i % 64); }
SliceMask selection(std::initializer_list<unsigned> positions) {
  SliceMask mask{};
  for (auto i : positions) select(mask, i);
  return mask;
}
std::vector<SliceMask> masks() {
  std::vector<SliceMask> result{{}, {~UINT64_C(0), ~UINT64_C(0), ~UINT64_C(0), ~UINT64_C(0)}};
  for (unsigned i = 0; i < 256; ++i) result.push_back(selection({i}));
  for (unsigned boundary : {16, 64, 128, 192, 255}) {
    result.push_back(selection({boundary - 1, boundary}));
    result.push_back(selection({boundary - 2, boundary - 1, boundary}));
  }
  result.push_back({UINT64_C(0x5555555555555555), UINT64_C(0x5555555555555555),
                    UINT64_C(0x5555555555555555), UINT64_C(0x5555555555555555)});
  result.push_back({UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0xaaaaaaaaaaaaaaaa),
                    UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0xaaaaaaaaaaaaaaaa)});
  // Equal cardinalities with different metadata and decoder-pair reuse.
  for (unsigned count : {8, 16, 32, 64}) {
    SliceMask clustered{}, dispersed{};
    for (unsigned i = 0; i < count; ++i) {
      select(clustered, 61 + i);
      select(dispersed, (i * 251 + 7) % 256);
    }
    result.push_back(clustered);
    result.push_back(dispersed);
  }
  std::uint64_t state = UINT64_C(0xabc576);
  for (unsigned n = 0; n < 24; ++n) {
    SliceMask mask{};
    for (auto& word : mask) {
      word = random(state);
      if (n % 3 == 0) word &= random(state) & random(state);
      if (n % 3 == 1) word |= random(state);
    }
    result.push_back(mask);
  }
  return result;
}
Result oracle(const Window& left, const Window& right, SetOperation operation) {
  Result result;
  for (unsigned i = 0; i < 256; ++i)
    for (unsigned j = 0; j < 32; ++j)
      result[32 * i + j] = operation == SetOperation::intersection
          ? left[i][j] & right[i][j] : left[i][j] | right[i][j];
  return result;
}
void compare(const AlignedBytes& output, const Result& values, const SliceMask& mask,
             std::uint8_t inactive, const char* what) {
  for (unsigned i = 0; i < 8192; ++i) {
    const auto expected = selected(mask, i / 32) ? values[i] : inactive;
    if (output.data()[i] != expected) {
      std::fprintf(stderr, "%s at slice %u byte %u: got %u expected %u\n",
                   what, i / 32, i % 32, output.data()[i], expected);
      std::abort();
    }
  }
}
void check_selection(const std::vector<SliceMask>& cases) {
  for (const auto& mask : cases) {
    SelectedSlices cursor(mask);
    for (unsigned i = 0; i < 256; ++i) if (selected(mask, i)) {
      require(cursor.peek() == i && cursor.peek() == i, "selection peek is stable");
      require(cursor.next() == i, "selection retains source ordinal");
    }
    require(cursor.peek() == 256 && cursor.next() == 256 && cursor.next() == 256,
            "selection exhaustion, including singleton 255");
  }
}
std::uint32_t point(MetadataKind kind, const std::uint8_t* bytes, unsigned i) {
  switch (kind) {
  case MetadataKind::direct32: return read_metadata_point<MetadataKind::direct32>(bytes, i);
  case MetadataKind::local16: return read_metadata_point<MetadataKind::local16>(bytes, i);
  case MetadataKind::scan128: return read_metadata_point<MetadataKind::scan128>(bytes, i);
  }
  std::abort();
}
void check_points(const Sources& source) {
  for (unsigned k = 0; k < 3; ++k) {
    const auto& metadata = *source.metadata[k];
    const auto* bytes = metadata.bytes().data();
    for (unsigned i = 0; i < 256; ++i) {
      const auto e = read_entry_reference(kinds[k], {bytes, metadata.bytes().size()}, 256, i);
      require(e == source.encoded.entries()[i], "reference metadata and source association");
      const auto expected = (unsigned(e.offset) << 16) | e.population;
      require(point(kinds[k], bytes, i) == expected &&
                  bind_resolver(kinds[k], Resolution::point).point(bytes, i) == expected,
              "point metadata fields");
#if __has_feature(address_sanitizer)
      __asan_poison_memory_region(bytes, metadata.bytes().size());
      const auto expose = [&](unsigned start, unsigned length) {
        // ASan has eight-byte shadow granularity. Expose enclosing cells, not
        // a claim that every byte within them is semantically required.
        const auto end = (start + length + 7) & ~7u;
        __asan_unpoison_memory_region(bytes + (start & ~7u), end - (start & ~7u));
      };
      if (kinds[k] == MetadataKind::direct32) expose(4 * i, 4);
      else if (kinds[k] == MetadataKind::local16) expose(32 * (i / 16), 32);
      else {
        expose(2 * (i / 16), 2);
        expose(32 + 18 * (i / 16), 18);
        // The selected prefix genuinely depends on preceding lengths. Keep
        // the length resource readable; exclude unused population packets.
        expose(320, 192);
      }
      const auto actual = bind_resolver(kinds[k], Resolution::point).point(bytes, i);
      __asan_unpoison_memory_region(bytes, metadata.bytes().size());
      require(actual == expected, "point access stays in exposed metadata resources");
#endif
    }
  }
  require(source.encoded.entries()[255].offset > source.encoded.entries()[240].offset &&
              source.encoded.entries()[15].offset > 0,
          "skipped predecessors have nonzero encoded lengths");
}
void poison_plain(const Sources& source, const SliceMask& mask, bool poison) {
#if __has_feature(address_sanitizer)
  for (unsigned i = 0; i < 256; ++i) if (!selected(mask, i)) {
    if (poison) __asan_poison_memory_region(source.plain->data() + 32 * i, 32);
    else __asan_unpoison_memory_region(source.plain->data() + 32 * i, 32);
  }
#else
  (void)source; (void)mask; (void)poison;
#endif
}
std::uint64_t check_matrix(const Window& a, const Window& b, const Sources& left,
                           const Sources& right, const std::vector<SliceMask>& cases) {
  auto output = std::make_shared<AlignedBytes>(8192);
  std::uint64_t count = 0;
  for (auto operation : operations) {
    const auto values = oracle(a, b, operation);
    for (unsigned l = 0; l < 4; ++l) for (unsigned r = 0; r < 4; ++r)
      for (auto left_resolution : resolutions) for (auto right_resolution : resolutions) {
        PreparedAlgebra prepared;
        require(prepare_algebra(left.admitted[l], right.admitted[r], output, operation,
            left_resolution, right_resolution, AlgebraExecution::factored, prepared) == AlgebraError::none,
            "all representation pairs prepare");
        for (const auto& mask : cases) {
          std::memset(output->data(), 0xa5, 8192);
          prepared.apply(mask);
          compare(*output, values, mask, 0, "complete result");
          std::memset(output->data(), 0xa5, 8192);
          prepared.apply_selected(mask);
          compare(*output, values, mask, 0xa5, "active-only result");
          ++count;
        }
        for (const auto mask : {selection({255}), selection({15, 16}),
                                selection({63, 64}), selection({1, 18, 129, 254})}) {
          poison_plain(left, mask, true);
          poison_plain(right, mask, true);
          prepared.apply(mask);
          compare(*output, values, mask, 0, "inactive plain slices are unread");
          std::memset(output->data(), 0xa5, 8192);
          prepared.apply_selected(mask);
          compare(*output, values, mask, 0xa5, "active-only guarded result");
          poison_plain(left, mask, false);
          poison_plain(right, mask, false);
        }
      }
    PreparedAlgebra inlined;
    require(prepare_algebra(left.admitted[2], right.admitted[2], output, operation,
        Resolution::cached16, AlgebraExecution::local_inline, inlined) == AlgebraError::none,
        "Local/Local inline preparation");
    for (const auto& mask : cases) {
      inlined.apply(mask);
      compare(*output, values, mask, 0, "Local/Local inline complete result");
      std::memset(output->data(), 0xa5, 8192);
      inlined.apply_selected(mask);
      compare(*output, values, mask, 0xa5, "Local/Local inline active-only result");
      ++count;
    }
  }
  return count;
}
void check_admission(const Sources& left, const Sources& right, const Window& values) {
  AdmittedBitset invalid;
  for (unsigned size : {0, 8191, 8193})
    require(admit_plain(std::make_shared<AlignedBytes>(size), invalid) == AdmissionError::query_extent,
            "wrong plain extent refused");
  require(admit_plain({}, invalid) == AdmissionError::query_extent, "null plain refused");
  for (unsigned k = 0; k < 3; ++k) {
    require(admit_bec(left.metadata[k], right.encoded.body(), invalid) == AdmissionError::association,
            "foreign metadata/body association refused");
    require(admit_bec({}, left.encoded.body(), invalid) == AdmissionError::association,
            "null metadata refused");
  }
  for (unsigned count : {0, 1, 255}) {
    const auto source = encode_source(std::span(values).first(count));
    auto metadata = std::make_shared<MetadataOwner>(source, MetadataKind::local16);
    require(admit_bec(metadata, source.body(), invalid) == AdmissionError::range,
            "non-256 source cardinality refused");
  }
  auto output = std::make_shared<AlignedBytes>(8192);
  PreparedAlgebra prepared;
  for (bool bad_left : {false, true})
    require(prepare_algebra(bad_left ? invalid : left.admitted[0],
        bad_left ? right.admitted[0] : invalid, output, SetOperation::intersection,
        Resolution::point, AlgebraExecution::factored, prepared) == AlgebraError::source,
        "unadmitted source refused");
  for (unsigned size : {0, 8191, 8193})
    require(prepare_algebra(left.admitted[0], right.admitted[0],
        std::make_shared<AlignedBytes>(size), SetOperation::intersection, Resolution::point,
        AlgebraExecution::factored, prepared) == AlgebraError::output_extent,
        "wrong output extent refused");
  require(prepare_algebra(left.admitted[0], right.admitted[0], {},
      SetOperation::intersection, Resolution::point, AlgebraExecution::factored, prepared) ==
      AlgebraError::output_extent, "null output refused");
  for (auto alias : {left.plain, right.plain})
    require(prepare_algebra(left.admitted[0], right.admitted[0], alias,
        SetOperation::intersection, Resolution::point, AlgebraExecution::factored, prepared) ==
        AlgebraError::overlap, "output aliases either source");
  // Exercise the compressed-body overlap check beyond the earlier extent
  // rejection: 254 bodies of 32 bytes plus the owner suffix occupy 8,192 bytes.
  unsigned specimen = 0;
  while (specimen < 256 && left.encoded.entries()[specimen].length != 32) ++specimen;
  require(specimen < 256, "fixture supplies a 32-byte BEC body");
  Window alias_values{};
  for (unsigned i = 0; i < 254; ++i) alias_values[i] = values[specimen];
  Sources body_alias(alias_values);
  require(body_alias.encoded.body()->bytes.size() == 8192, "body alias has valid output extent");
  auto alias = std::shared_ptr<AlignedBytes>(body_alias.encoded.body(),
      const_cast<AlignedBytes*>(&body_alias.encoded.body()->bytes));
  for (unsigned k = 1; k < 4; ++k)
    require(prepare_algebra(body_alias.admitted[k], right.admitted[0], alias,
        SetOperation::intersection, Resolution::point, AlgebraExecution::factored, prepared) ==
        AlgebraError::overlap, "output aliases compressed body");
  for (unsigned l = 0; l < 4; ++l) for (unsigned r = 0; r < 4; ++r)
    for (auto left_resolution : resolutions) for (auto right_resolution : resolutions) {
      const auto error = prepare_algebra(left.admitted[l], right.admitted[r], output,
          SetOperation::intersection, left_resolution, right_resolution,
          AlgebraExecution::local_inline, prepared);
      require(error == (l == 2 && r == 2 && left_resolution == Resolution::cached16 &&
          right_resolution == Resolution::cached16
          ? AlgebraError::none : AlgebraError::unsupported), "exact inline applicability");
    }
}
void check_terminals(const Window& other) {
  const SliceMask all{~UINT64_C(0), ~UINT64_C(0), ~UINT64_C(0), ~UINT64_C(0)};
  Sources plain_source(other);
  for (auto operation : operations) {
    Window constants;
    const std::uint8_t value = operation == SetOperation::intersection ? 0 : 255;
    for (auto& block : constants) block.fill(value);
    Sources terminal_source(constants);
    require(terminal_source.encoded.body()->logical_bytes == 0, "terminal bodies occupy no bytes");
    auto output = std::make_shared<AlignedBytes>(8192);
    const auto expected = oracle(constants, other, operation);
    for (unsigned k = 1; k < 4; ++k) for (auto resolution : resolutions) {
      PreparedAlgebra prepared;
      require(prepare_algebra(terminal_source.admitted[k], plain_source.admitted[0], output,
          operation, resolution, AlgebraExecution::factored, prepared) == AlgebraError::none,
          "terminal preparation");
#if __has_feature(address_sanitizer)
      __asan_poison_memory_region(terminal_source.encoded.body()->bytes.data(), 64);
      __asan_poison_memory_region(plain_source.plain->data(), 8192);
#endif
      prepared.apply(all);
#if __has_feature(address_sanitizer)
      __asan_unpoison_memory_region(terminal_source.encoded.body()->bytes.data(), 64);
      __asan_unpoison_memory_region(plain_source.plain->data(), 8192);
#endif
      compare(*output, expected, all, 0, "annihilator skips both payloads");
    }
  }
  for (unsigned pa : {0, 256}) for (unsigned pb : {0, 256}) {
    Window a, b;
    for (auto& block : a) block.fill(pa ? 255 : 0);
    for (auto& block : b) block.fill(pb ? 255 : 0);
    Sources left(a), right(b);
    auto output = std::make_shared<AlignedBytes>(8192);
    for (auto operation : operations) {
      PreparedAlgebra prepared;
      require(prepare_algebra(left.admitted[2], right.admitted[2], output, operation,
          Resolution::cached16, AlgebraExecution::local_inline, prepared) == AlgebraError::none,
          "terminal inline preparation");
#if __has_feature(address_sanitizer)
      __asan_poison_memory_region(left.encoded.body()->bytes.data(), 64);
      __asan_poison_memory_region(right.encoded.body()->bytes.data(), 64);
#endif
      prepared.apply(all);
#if __has_feature(address_sanitizer)
      __asan_unpoison_memory_region(left.encoded.body()->bytes.data(), 64);
      __asan_unpoison_memory_region(right.encoded.body()->bytes.data(), 64);
#endif
      compare(*output, oracle(a, b, operation), all, 0, "all terminal pairs skip payloads");
    }
  }
}
void check_lifetime() {
  auto output = std::make_shared<AlignedBytes>(8192);
  PreparedAlgebra prepared;
  Result expected;
  const auto mask = selection({1, 15, 16, 63, 64, 127, 128, 255});
  std::weak_ptr<const BodyOwner> retained_body;
  std::weak_ptr<AlignedBytes> retained_plain;
  {
    auto a = make_window(17), b = make_window(33);
    Sources left(a), right(b);
    retained_body = left.encoded.body();
    retained_plain = right.plain;
    expected = oracle(a, b, SetOperation::set_union);
    require(prepare_algebra(left.admitted[3], right.admitted[0], output,
        SetOperation::set_union, Resolution::cached16, AlgebraExecution::factored, prepared) ==
        AlgebraError::none, "retained preparation");
  }
  require(!retained_body.expired() && !retained_plain.expired(), "prepared sources retain owners");
  prepared.apply(mask);
  compare(*output, expected, mask, 0, "execution after builders die");
  prepared = PreparedAlgebra{};
  require(retained_body.expired() && retained_plain.expired(), "prepared owner release");
}
} // namespace

int main() {
  require(check_algebra_native() == 0, "native slice algebra");
  require(ikea::heterogeneous::check_analyser() == 0, "analyser");
  const auto a = make_window(0), b = make_window(1);
  Sources left(a), right(b);
  const auto cases = masks();
  check_selection(cases);
  check_points(left);
  check_points(right);
  const auto count = check_matrix(a, b, left, right, cases);
  check_admission(left, right, a);
  check_terminals(b);
  check_lifetime();
  std::printf("Masked algebra: %llu operation/mask cases (%zu masks), all 16 representation "
              "pairs, four independent resolution choices and Local/Local inline passed; admission, lifetime, "
              "terminal and guarded access checks passed.\n",
              static_cast<unsigned long long>(count), cases.size());
}
