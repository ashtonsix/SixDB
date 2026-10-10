#include "backend.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>
#include <type_traits>
#include <vector>

namespace {
namespace c = orbital_mvcc::composed;
namespace p = orbital_versioned_performance;
namespace v = orbital_bench::versioned;
namespace cs = compact_shape;
using namespace orbital_bench;
using Image = std::array<std::vector<std::byte>, 4>;
constexpr std::array<unsigned, 4> widths{64, 64, 20, 32};
struct Evidence {
  std::uint64_t bodies{}, logical_rows{}, wire_bytes{}, outcomes{}, fields{}, refusals{}, publications{}, conversions{}, point_updates{};
} evidence;

template<class T> void check(const T& value,
    std::source_location where = std::source_location::current()) {
  if (static_cast<bool>(value)) return;
  std::fprintf(stderr, "append check failed: %s:%u\n", where.file_name(), where.line());
  std::abort();
}

std::uint64_t field(const Row& row, unsigned index) {
  switch (index) {
  case 0: return row.key;
  case 1: return row.record.next;
  case 2: return row.record.value;
  case 3: return row.record.tag;
  }
  check(false); return 0;
}

// Independent fixed wire law. Includes Tuple spare nibbles and all initialized
// Series tail lanes; neither an Ikea decoder nor the construction plan is used.
Image wire_oracle(std::span<const Row> rows, v::Format format) {
  Image result;
  if (format == v::Format::rows) result[0].resize(rows.size() * 23);
  else for (unsigned index = 0; index < 4; ++index)
    result[index].resize((rows.size() / 8 + (rows.size() % 8 != 0)) * widths[index]);
  for (std::size_t row = 0; row < rows.size(); ++row)
    for (unsigned index = 0; index < 4; ++index)
      for (unsigned bit = 0; bit < widths[index]; ++bit) {
        std::size_t offset;
        unsigned shift;
        if (format == v::Format::rows) {
          constexpr std::array<unsigned, 4> first{0, 64, 128, 152};
          offset = row * 23 + (first[index] + bit) / 8;
          shift = (first[index] + bit) % 8;
        } else {
          const auto whole = widths[index] / 8, residual = widths[index] % 8;
          offset = row / 8 * widths[index] + (bit < residual ? 8 * whole + bit :
              row % 8 * whole + (bit - residual) / 8);
          shift = bit < residual ? unsigned(row % 8) : (bit - residual) % 8;
        }
        result[format == v::Format::rows ? 0 : index][offset] |=
            std::byte(unsigned((field(rows[row], index) >> bit) & 1) << shift);
      }
  return result;
}

Image snapshot(cs::Selection selected) {
  Image result;
  check(selected && selected.part_count() <= result.size());
  for (std::size_t part = 0; part < selected.part_count(); ++part) {
    const auto view = p::need(selected.part(part));
    const auto extent = p::need(cs::geometry(selected.shape(), part).extent(selected.records()));
    check(extent <= SIZE_MAX);
    const orbital::Bytes bytes{view.address(), static_cast<std::size_t>(extent)};
    result[part].assign(bytes.begin(), bytes.end());
  }
  return result;
}

// Linear logical oracle, independent of encoded binary search and native read
// endpoints. Offered rows are sorted fixture input, not a maintained key index.
Outcome oracle(std::span<const Row> rows, const Operation& operation) {
  Outcome result{};
  const auto mix = [&](std::uint64_t value) {
    result.digest ^= value + 0x9e3779b97f4a7c15ULL + (result.digest << 6) + (result.digest >> 2);
  };
  const auto consume = [&](const Row& row) {
    mix(row.key); mix(row.record.next); mix(row.record.value); mix(row.record.tag); ++result.rows;
  };
  if (operation.kind == Kind::scan) {
    unsigned used{};
    for (const auto& row : rows) if (row.key >= operation.key && used < operation.count) {
      consume(row); ++used;
    }
  } else {
    auto key = operation.key;
    const auto count = operation.kind == Kind::chain ? operation.count : 1u;
    for (unsigned step = 0; step < count; ++step) {
      const auto found = std::find_if(rows.begin(), rows.end(), [&](const Row& row) { return row.key == key; });
      if (found == rows.end()) { mix(key); mix(0); break; }
      consume(*found); key = found->record.next;
    }
  }
  return result;
}

void verify(c::Body body, std::span<const Row> expected, v::Format format, std::size_t boundary) {
  check(body.size() == expected.size() && body.selected.records() == expected.size() && body.format() == format);
  check(body.selected.part_count() == (format == v::Format::rows ? 1u : 4u));
  const auto bytes = snapshot(body.selected), wanted = wire_oracle(expected, format);
  check(bytes == wanted);
  for (const auto& part : bytes) evidence.wire_bytes += part.size();
  for (std::size_t row = 0; row < expected.size(); ++row) {
    check(body.row_unchecked(row) == expected[row]);
    check(body.get(expected[row].key) == expected[row].record);
    ++evidence.logical_rows;
  }
  check(!body.get(UINT64_MAX));
  for (const auto key : {expected.front().key, expected[boundary < expected.size() ? boundary : 0].key, UINT64_MAX}) {
    for (const auto steps : {0u, 1u, 16u}) {
      const Operation chain{.kind = Kind::chain, .key = key, .record = {}, .count = steps};
      check(p::read_operation(body, chain) == oracle(expected, chain)); ++evidence.outcomes;
    }
    for (const auto count : {0u, 1u, 7u, 16u}) {
      const Operation scan{.kind = Kind::scan, .key = key, .record = {}, .count = count};
      check(p::read_operation(body, scan) == oracle(expected, scan)); ++evidence.outcomes;
    }
  }
  // Exercise the actual generic binding at this selected count, including
  // streamed field reads spanning the old-prefix/new-tail boundary.
  const auto read = body.material->generic(body.selected);
  check(read.size() == expected.size() && read.format() == format);
  const auto first = boundary && boundary <= expected.size() ? boundary - 1 : 0;
  std::vector<std::uint64_t> values(expected.size() - first);
  for (unsigned index = 0; index < 4; ++index) {
    check(read.field(static_cast<v::Field>(index), first, values));
    for (std::size_t i = 0; i < values.size(); ++i) {
      check(values[i] == field(expected[first + i], index)); ++evidence.fields;
    }
    check(read.field(static_cast<v::Field>(index), expected.size(), {}));
    std::array<std::uint64_t, 2> refused{0x9174, 0x2965};
    const auto sentinel = refused;
    const auto status = read.field(static_cast<v::Field>(index), expected.size() - 1, refused);
    check(!status && status.error() == orbital::Error::invalid_argument && refused == sentinel);
  }
  check(!read.row(expected.size())); ++evidence.bodies;
}

std::vector<Row> tail(std::span<const Row> prefix, std::size_t count) {
  std::vector<Row> result(count);
  check(prefix.back().key < UINT64_MAX - 19 * count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto key = prefix.back().key + 19 * (i + 1);
    const auto next = i ? result[i - 1].key : prefix.front().key;
    result[i] = {key, {next, i % 3 == 0 ? value_mask : std::uint32_t(137 * i + 29),
        i % 3 == 0 ? UINT32_MAX : std::uint32_t(0x87654321u + i * 971)}};
  }
  return result;
}

template<class Backend> void refusal(Backend& owner, std::span<const Row> offered,
    v::ConstructionOptions options, orbital::Error error, bool deny_metadata = false) {
  auto protected_before = owner.read();
  const auto before = owner.body(protected_before, 0);
  const auto alternative = owner.body(protected_before, 0, true);
  const auto primary_bytes = snapshot(before.selected), alternative_bytes = snapshot(alternative.selected);
  const auto cut = owner.cut();
  const auto count = before.size();
  const auto statistics = owner.statistics();
  const auto accounting = owner.accounting();
  if (deny_metadata) owner.metadata_limit(accounting.metadata_live);
  const auto status = owner.append(offered, options);
  if (deny_metadata) owner.metadata_limit(accounting.metadata_limit);
  check(!status && status.error() == error);
  check(owner.cut() == cut && snapshot(before.selected) == primary_bytes &&
      snapshot(alternative.selected) == alternative_bytes);
  {
    auto current = owner.read();
    const auto after = owner.body(current, 0), after_alternative = owner.body(current, 0, true);
    check(after.size() == count && after.selected.realization == before.selected.realization);
    check(after_alternative.selected.realization == alternative.selected.realization);
    check(snapshot(after.selected) == primary_bytes && snapshot(after_alternative.selected) == alternative_bytes);
  }
  owner.drain(); owner.poll(65536);
  check(owner.statistics().versions == statistics.versions &&
      owner.statistics().alternative_count == statistics.alternative_count &&
      owner.accounting().active_allocations == accounting.active_allocations);
  ++evidence.refusals;
}

template<class Backend> void run(const char* name, std::size_t initial_count, std::size_t appended_count) {
  static_assert(!std::is_move_constructible_v<typename Backend::Scope>);
  p::Corpus corpus(initial_count, 1, 8197 + initial_count);
  const auto original = corpus.images[0].rows;
  const auto appended = tail(original, appended_count);
  auto expected = original;
  expected.insert(expected.end(), appended.begin(), appended.end());
  {
    Backend owner(corpus, 32 * 1024 * 1024);
    std::optional<typename Backend::Scope> early(std::in_place, owner.catalog());
    std::optional<typename Backend::Scope> late(std::in_place, owner.catalog());
    const auto initial_cut = owner.cut();
    const auto early_primary = owner.body(*early, 0);
    const auto primary_snapshot = snapshot(early_primary.selected);
    verify(early_primary, original, v::Format::rows, initial_count);
    check(owner.convert(0, v::Format::columns)); ++evidence.conversions;
    const auto retained_series = owner.body(*early, 0, true);
    const auto series_snapshot = snapshot(retained_series.selected);
    const auto* shared_columns = retained_series.selected.shape();
    verify(retained_series, original, v::Format::columns, initial_count);

    // Really replace the adopted alternative. Its prior Body is still borrowed
    // under early's actual alternative capture, not protected by a raw pointer.
    check(owner.convert(0, v::Format::rows)); ++evidence.conversions;
    check(owner.cut() == initial_cut && snapshot(retained_series.selected) == series_snapshot);
    verify(retained_series, original, v::Format::columns, initial_count);
    check(owner.convert(0, v::Format::columns)); ++evidence.conversions;
    check(owner.cut() == initial_cut);

    check(owner.append(appended)); ++evidence.publications;
    check(owner.cut() == initial_cut + 1);
    {
      auto current = owner.read();
      const auto fresh = owner.body(current, 0);
      verify(fresh, expected, v::Format::rows, initial_count);
      const auto fresh_bytes = snapshot(fresh.selected);
      check(std::equal(primary_snapshot[0].begin(), primary_snapshot[0].end(), fresh_bytes[0].begin()));
      if constexpr (std::is_same_v<Backend, c::Inline>) {
        auto fused = owner.template bodies<1>();
        const Operation operation{.kind = Kind::read, .key = appended.back().key, .record = {}, .count = 1};
        Outcome result{.digest = 0x7912, .rows = 83, .changes = 6};
        fused.consume_into(0, operation, result);
        check(result == oracle(expected, operation)); ++evidence.outcomes;
      }
      // Append is a logical publication: the previous cut's alternative is not
      // installed at this new count. It is still discoverable at the old cut.
      const auto absent = current.bind_alternative(corpus.images[0].address.object);
      check(!absent && absent.error() == orbital::Error::absent);
      const auto discovered_primary = owner.body(*late, 0);
      const auto discovered_alternative = owner.body(*late, 0, true);
      verify(discovered_primary, original, v::Format::rows, initial_count);
      verify(discovered_alternative, original, v::Format::columns, initial_count);
      check(snapshot(discovered_primary.selected) == primary_snapshot);
      check(snapshot(discovered_alternative.selected) == series_snapshot);
    }

    check(owner.convert(0, v::Format::columns)); ++evidence.conversions;
    check(owner.cut() == initial_cut + 1);
    {
      auto current = owner.read();
      const auto converted = owner.body(current, 0, true);
      verify(converted, expected, v::Format::columns, initial_count);
      check(converted.selected.shape() == shared_columns);
      check(converted.size() != retained_series.size());
      check(!cs::equivalent(converted.selected.realization, retained_series.selected.realization));
      if (initial_count == 9 && expected.size() == 16) {
        const auto converted_bytes = snapshot(converted.selected);
        std::size_t old_extent{}, new_extent{};
        for (unsigned part = 0; part < 4; ++part) {
          check(series_snapshot[part].size() == converted_bytes[part].size());
          old_extent += series_snapshot[part].size(); new_extent += converted_bytes[part].size();
        }
        check(old_extent == 360 && new_extent == 360);
        check(series_snapshot != converted_bytes);
        // Exercise the real adopter with a live current16 target and an actual
        // retained Series9 replacement. Equal occupied extents cannot authorize
        // this replacement; neither current primary nor alternative may change.
        using Store = std::remove_cvref_t<decltype(owner.catalog())>;
        using Owner = typename Store::Owner;
        const auto cut = owner.cut();
        const auto primary = owner.body(current, 0);
        const auto primary_bytes = snapshot(primary.selected);
        {
          auto pending = p::need(owner.prepare_conversion(current, 0, v::Format::columns));
          pending.replacement = Owner::retain(retained_series.selected.realization);
          const auto status = owner.adopt(pending);
          check(!status && status.error() == orbital::Error::conflict);
          check(owner.cut() == cut && snapshot(primary.selected) == primary_bytes &&
              snapshot(converted.selected) == converted_bytes);
          auto after_scope = owner.read();
          const auto after_primary = owner.body(after_scope, 0);
          const auto after_alternative = owner.body(after_scope, 0, true);
          check(after_primary.size() == 16 && after_alternative.size() == 16);
          check(after_primary.selected.realization == primary.selected.realization &&
              after_alternative.selected.realization == converted.selected.realization);
          check(snapshot(after_primary.selected) == primary_bytes &&
              snapshot(after_alternative.selected) == converted_bytes);
          ++evidence.refusals;
        } // Pending replacement/target release before later allocation checks.
      }
    }
    check(snapshot(early_primary.selected) == primary_snapshot && snapshot(retained_series.selected) == series_snapshot);
    verify(early_primary, original, v::Format::rows, initial_count);
    verify(retained_series, original, v::Format::columns, initial_count);

    const auto more = tail(expected, 3);
    auto invalid = more; invalid.back().record.value = 0x01000000u;
    refusal(owner, invalid, {}, orbital::Error::invalid_argument);
    invalid.back().record.value = value_mask + 1;
    refusal(owner, invalid, {}, orbital::Error::invalid_argument);
    refusal(owner, {}, {}, orbital::Error::invalid_argument);
    refusal(owner, more, {.fail_after_rows = 1}, orbital::Error::rejected);
    refusal(owner, more, {.fail_after_rows = 0}, orbital::Error::rejected);
    refusal(owner, more, {.fail_after_parts = 0}, orbital::Error::rejected);
    refusal(owner, more, {}, orbital::Error::capacity, true);
    {
      auto current = owner.read();
      verify(owner.body(current, 0), expected, v::Format::rows, initial_count);
      verify(owner.body(current, 0, true), expected, v::Format::columns, initial_count);
      verify(owner.body(*late, 0), original, v::Format::rows, initial_count);
      verify(retained_series, original, v::Format::columns, initial_count);
    }

    // Single-request point writes at the grown count. Series first publishes a
    // Series primary; its grown-count conversion then supplies the Tuple target.
    // The command's deliberately different next must never replace encoded next.
    for (const auto target : {v::Format::columns, v::Format::rows}) {
      if (target == v::Format::rows) {
        const auto cut = owner.cut();
        check(owner.convert(0, target)); ++evidence.conversions;
        check(owner.cut() == cut);
      }
      auto held = owner.read();
      const auto before = owner.body(held, 0, true);
      const auto before_bytes = snapshot(before.selected);
      const auto before_rows = expected;
      check(before.size() == expected.size() && before.format() == target);
      const auto key = appended.back().key;
      const auto value = target == v::Format::columns ? 0x71a35u : 0x81926u;
      const auto tag = 0xc817294bu ^ value;
      const std::array<Operation, 1> command{{{.kind = Kind::update, .key = key,
          .record = {UINT64_MAX, value, tag}, .count = 1}}};
      const std::array<std::size_t, 1> objects{0};
      std::array<Outcome, 1> output{{{.digest = 9173, .rows = 28, .changes = 63}}};
      const auto cut = owner.cut();
      check(owner.update(command, output, objects)); ++evidence.point_updates; ++evidence.publications;
      check(owner.cut() == cut + 1);
      const Outcome wanted{.digest = 0x9e3779b97f4a7c16ULL, .rows = 0, .changes = 1};
      check(output[0] == wanted); ++evidence.outcomes;
      for (auto& row : expected) if (row.key == key) { row.record.value = value; row.record.tag = tag; }
      {
        auto current = owner.read();
        verify(owner.body(current, 0), expected, target, initial_count);
      }
      check(snapshot(before.selected) == before_bytes);
      verify(before, before_rows, target, initial_count);
      check(snapshot(early_primary.selected) == primary_snapshot && snapshot(retained_series.selected) == series_snapshot);
      verify(owner.body(*late, 0), original, v::Format::rows, initial_count);
      verify(retained_series, original, v::Format::columns, initial_count);
    }
    // Current primary is Tuple again; retain the ordinary five-Part arrangement.
    const auto updated_cut = owner.cut();
    check(owner.convert(0, v::Format::columns)); ++evidence.conversions;
    check(owner.cut() == updated_cut);
    {
      auto current = owner.read();
      verify(owner.body(current, 0), expected, v::Format::rows, initial_count);
      verify(owner.body(current, 0, true), expected, v::Format::columns, initial_count);
    }

    late.reset(); early.reset();
    owner.drain(); owner.poll(65536);
    check(owner.statistics().objects == 1 && owner.statistics().versions == 1 && owner.statistics().scopes == 0);
    check(owner.accounting().active_allocations == 5); // Current Tuple plus four current Series Parts.
    std::printf("{\"backend\":\"%s\",\"initial_rows\":%zu,\"appended_rows\":%zu,"
        "\"current_rows\":%zu,\"retained_old_rows\":%zu,\"settled_versions\":1}\n",
        name, initial_count, appended_count, expected.size(), original.size());
  }
  // Actual Backend destruction destroys the catalog before Material. Material
  // asserts zero PMR charge; Runtime physically closes/polls/removes its Pool
  // and asserts zero Arena charge, after Scopes close and Body borrows end.
}

template<class Backend> void unsupported(const char* name) {
  {
    p::Corpus corpus(14, 2, 57);
    Backend owner(corpus, 32 * 1024 * 1024);
    const auto offered = tail(corpus.images[0].rows, 1);
    const auto cut = owner.cut();
    const auto status = owner.append(offered);
    check(!status && status.error() == orbital::Error::invalid_argument && owner.cut() == cut);
    auto current = owner.read();
    for (std::size_t object = 0; object < 2; ++object)
      verify(owner.body(current, object), corpus.images[object].rows, v::Format::rows, 7);
    ++evidence.refusals;
  }
  {
    p::Corpus corpus(7, 1, 61);
    Backend owner(corpus, 32 * 1024 * 1024, v::Format::columns);
    const auto offered = tail(corpus.images[0].rows, 1);
    refusal(owner, offered, {}, orbital::Error::invalid_argument);
    auto current = owner.read();
    verify(owner.body(current, 0), corpus.images[0].rows, v::Format::columns, 7);
  }
  std::printf("{\"backend\":\"%s\",\"unsupported_shapes_refused\":true}\n", name);
}
} // namespace

int main() {
  run<c::Inline>("inline16", 9, 7);
  run<c::Directory>("directory16", 9, 7);
  run<c::Inline>("inline16", 7, 2);
  run<c::Directory>("directory16", 7, 2);
  unsupported<c::Inline>("inline16");
  unsupported<c::Directory>("directory16");
  std::printf("{\"passed\":true,\"timings\":false,\"actual_teardown_checked\":true,"
      "\"bodies\":%llu,\"logical_rows\":%llu,\"wire_bytes\":%llu,\"outcomes\":%llu,"
      "\"field_values\":%llu,\"refusals\":%llu,\"publications\":%llu,\"conversions\":%llu,\"point_updates\":%llu}\n",
      static_cast<unsigned long long>(evidence.bodies), static_cast<unsigned long long>(evidence.logical_rows),
      static_cast<unsigned long long>(evidence.wire_bytes), static_cast<unsigned long long>(evidence.outcomes),
      static_cast<unsigned long long>(evidence.fields), static_cast<unsigned long long>(evidence.refusals),
      static_cast<unsigned long long>(evidence.publications), static_cast<unsigned long long>(evidence.conversions),
      static_cast<unsigned long long>(evidence.point_updates));
}
