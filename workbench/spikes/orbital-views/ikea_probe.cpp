#include "prototype.h"
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>

namespace tp = ikea::tuplepack;
using namespace views;
namespace {
void check(bool ok, std::source_location at = std::source_location::current()) {
  if (!ok) { std::fprintf(stderr, "Ikea composition check failed at %u\n", at.line()); std::abort(); }
}
template<class T, class E> T get(std::expected<T, E> result) {
  check(result.has_value()); return std::move(*result);
}
template<class E> void done(std::expected<void, E> result) { check(result.has_value()); }
template<class T, class E> void fails(const std::expected<T, E>& result, E error) {
  check(!result && result.error() == error);
}
const std::array<std::size_t, 1> pages{0};
constexpr std::array<std::size_t, 2> bases{128, 256}, offsets{17, 11};
constexpr std::size_t extent = 31, stride = 3;

struct Mapping {
  const void* source;
  Draft* draft;
  std::size_t base, size;
  unsigned generation;
};
struct Resolved { Draft* draft; Range bytes; };
enum class ResolveError { foreign, stale, bounds };
std::expected<Resolved, ResolveError> resolve(ikea::owner_write effect,
    const std::array<Mapping, 2>& mappings, unsigned generation) {
  for (const auto& mapping : mappings) {
    if (effect.source != mapping.source) continue;
    if (mapping.generation != generation) return std::unexpected(ResolveError::stale);
    if (effect.bytes.plane != 0 || effect.bytes.offset > mapping.size ||
        effect.bytes.size > mapping.size - effect.bytes.offset)
      return std::unexpected(ResolveError::bounds);
    return Resolved{mapping.draft, {mapping.base + effect.bytes.offset, effect.bytes.size}};
  }
  return std::unexpected(ResolveError::foreign);
}

// A pre-admitted, no-fail observer. It sees bytes before each leaf's store;
// recording a range at this hook is not itself a stable afterimage capture.
struct ObserveIssued {
  ikea::source_write_journal& journal;
  std::array<const tp::view*, 2> sources;
  std::array<std::uint8_t, 2> before_bytes;
  unsigned observed{};
  void before(const tp::view& source, ikea::byte_write write) noexcept {
    unsigned index = source.storage().data() == sources[0]->storage().data() ? 0 : 1;
    check(&source == sources[index] && write.plane == 0 && write.offset == offsets[index]);
    check(write.size == 1 && source.storage()[write.offset] == before_bytes[index]);
    ++observed;
    journal.before(source, write);
  }
};
using Deliveries = std::array<std::shared_ptr<Delivery>, 2>;
Deliveries frontier(Draft& a, Draft& b, const tp::layout& format, const tp::writer<8>& writer,
                   unsigned generation, std::array<std::uint8_t, 2> old_bytes,
                   std::array<std::uint64_t, 2> values, bool reject_short_journal) {
  auto wa = get(a.access(pages)), wb = get(b.access(pages));
  auto va = get(tp::view::bind(format, {wa.writable() + bases[0], extent}, 2, stride, offsets[0]));
  auto vb = get(tp::view::bind(format, {wb.writable() + bases[1], extent}, 2, stride, offsets[1]));
  auto oa = get(tp::bind_writer(writer, va)), ob = get(tp::bind_writer(writer, vb));
  auto group = get(tp::composition::bind_group(oa, ob));
  const decltype(group)::input_type input{values[0], values[1]};
  if (reject_short_journal) {
    std::array<ikea::owner_write, 1> too_short{};
    ikea::source_write_journal insufficient{too_short};
    fails(group.set(0, input, insufficient), tp::error::capacity);
    check(insufficient.used == 0);
    for (std::size_t i = 0; i < wa.size(); ++i) check(wa.data()[i] == 0xa5);
    for (std::size_t i = 0; i < wb.size(); ++i) check(wb.data()[i] == 0x5a);
  }
  std::array<ikea::owner_write, 2> entries{};
  ikea::source_write_journal journal{entries};
  ObserveIssued observer{journal, {&va, &vb}, old_bytes};
  done(group.admit(0, {&input, 1}, tp::selection::all(), journal.remaining()));
  group.replace_unchecked(0, {&input, 1}, tp::selection::all(), observer);
  check(observer.observed == 2 && journal.used == 2);
  check(entries[0].source == &va && entries[1].source == &vb);
  check(entries[0].bytes.size == 1 && entries[1].bytes.size == 1);
  // This owner table is only valid while these named views and mappings live.
  const std::array<Mapping, 2> mappings{{
    {&va, &a, bases[0], extent, generation}, {&vb, &b, bases[1], extent, generation}}};
  fails(resolve(entries[0], mappings, generation + 1), ResolveError::stale);
  fails(resolve({&group, entries[0].bytes}, mappings, generation), ResolveError::foreign);
  fails(resolve({&va, {1, offsets[0], 1}}, mappings, generation), ResolveError::bounds);
  fails(resolve({&va, {0, extent, 1}}, mappings, generation), ResolveError::bounds);
  const std::array<Resolved, 2> resolved{
    get(resolve(entries[0], mappings, generation)), get(resolve(entries[1], mappings, generation))};
  check(resolved[0].bytes.offset == bases[0] + offsets[0]);
  check(resolved[1].bytes.offset == bases[1] + offsets[1]);
  fails(a.stage(std::span(&resolved[0].bytes, 1)), Error::busy);
  wa.close(); wb.close();
  // The local call has finished, overlapping writer leases have ended, and the
  // source identities are resolved. Orbital now owns stable payloads.
  return {get(resolved[0].draft->stage(std::span(&resolved[0].bytes, 1))),
          get(resolved[1].draft->stage(std::span(&resolved[1].bytes, 1)))};
}
void payload(const std::shared_ptr<Delivery>& delivery, std::size_t offset, std::uint8_t byte) {
  check(delivery->patches.size() == 1);
  check(delivery->patches[0].first.offset == offset && delivery->patches[0].first.size == 1);
  check(delivery->patches[0].second == Bytes{byte});
}
void logical(const Lease& lease, const tp::layout& format, const tp::reader<8>& reader,
             unsigned index, std::uint64_t row0, std::uint64_t row1) {
  auto source = get(tp::const_view::bind(format, {lease.data() + bases[index], extent},
                                      2, stride, offsets[index]));
  auto read = get(tp::bind_reader(reader, source));
  check(get(read.get(0)) == row0 && get(read.get(1)) == row1);
}
}
int main() {
  const auto page = page_size();
  const std::array<tp::code, 1> codes{{{0, 1, 3}}};
  const std::array<tp::byte, 1> ranks{0};
  const auto format = get(tp::layout::make(1, codes));
  const auto writer = get(tp::writer<8>::make(format, ranks));
  const auto reader = get(tp::reader<8>::make(format, ranks));
  const Bytes initial_a(page, 0xa5), initial_b(page, 0x5a);
  Part a(initial_a), b(initial_b);
  auto da = get(a.begin(Method::cow, pages)), db = get(b.begin(Method::copy, pages));
  auto first = frontier(da, db, format, writer, 7, {0xa5, 0x5a}, {2, 1}, true);
  // A's value is identical: its issued byte still includes preserved neighbours.
  payload(first[0], bases[0] + offsets[0], 0xa5);
  payload(first[1], bases[1] + offsets[1], 0x52);
  auto later = frontier(da, db, format, writer, 8, {0xa5, 0x52}, {7, 3}, false);
  // Named sources, operations and both journals have expired; first capture has
  // neither borrowed pointers nor a deferred read of the overwritten draft.
  payload(first[0], bases[0] + offsets[0], 0xa5);
  payload(first[1], bases[1] + offsets[1], 0x52);
  payload(later[0], bases[0] + offsets[0], 0xaf);
  payload(later[1], bases[1] + offsets[1], 0x56);
  later[1]->complete = true; later[0]->complete = true;
  fails(da.prepare(2), Error::pending); fails(db.prepare(2), Error::pending);
  first[1]->complete = true;
  fails(da.prepare(2), Error::pending);
  first[0]->complete = true;
  auto pa = get(da.prepare(2)), pb = get(db.prepare(2));
  Bytes expected_a = initial_a, expected_b = initial_b;
  expected_a[bases[0] + offsets[0]] = 0xaf;
  expected_b[bases[1] + offsets[1]] = 0x56;
  check(pa.bytes() == expected_a && pb.bytes() == expected_b);
  // This probe uses the Part publication mechanism. Whole-object journal
  // visibility/recovery is exercised separately by embedded_probe.cpp.
  done(pa.install()); done(pb.install());
  a.evict(); b.evict();
  auto old_a = get(a.open(1, pages)), old_b = get(b.open(1, pages));
  auto new_a = get(a.open(2, pages)), new_b = get(b.open(2, pages));
  check(std::memcmp(old_a.data(), initial_a.data(), page) == 0);
  check(std::memcmp(old_b.data(), initial_b.data(), page) == 0);
  check(std::memcmp(new_a.data(), expected_a.data(), page) == 0);
  check(std::memcmp(new_b.data(), expected_b.data(), page) == 0);
  logical(old_a, format, reader, 0, 2, 2); logical(old_b, format, reader, 1, 5, 5);
  logical(new_a, format, reader, 0, 7, 2); logical(new_b, format, reader, 1, 3, 5);
  check(a.counts().staged == 2 && b.counts().staged == 2);
  std::puts("{\"result\":\"pass\",\"cases\":[\"real-ikea-two-leaf-source-offsets\","
    "\"hook-before-local-store\",\"issued-preserved-neighbours-and-unchanged-store\","
    "\"short-effect-capacity-no-side-effects\",\"foreign-stale-owner-map-rejected\","
    "\"stable-capture-after-local-frontier\",\"overwritten-source-and-out-of-order-delivery\","
    "\"historical-and-latest-logical-reopen\"]}");
}
