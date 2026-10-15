#include "locality.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace loom_locality;

void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "locality_check: %s\n", message);
    std::abort();
  }
}

constexpr Tag owner{41, 7, 301, 2, 11};
constexpr Policy policy{60, 8, 2, 4};
constexpr Paths legal{true, true, 12, 16};
constexpr Observation cheap{Path::direct, 1000, 20, 0, 1};
constexpr Observation expensive{Path::direct, 4000, 20, 0, 1};

void observe(Record& record, Tag tag, Epoch now, Observation sample = cheap,
    Policy selected = policy) {
  require(record.observe(tag, now, sample, selected), "accepted serial completion sample");
}

Path path(const Record& record, Tag tag, Epoch now, Paths paths = legal,
    Policy selected = policy, Control control = Control::adaptive) {
  return decide(record, tag, now, paths, selected, control).path;
}

// Synthetic timings below check the decision laws only. The complete-operation
// consumer owns actual sampling, phase comparisons and performance evidence.
void completion_evidence() {
  Record record;
  require(path(record, owner, 10) == Path::ring, "unknown independent work can interleave");
  observe(record, owner, 10);
  require(path(record, owner, 10) == Path::ring, "one cheap sample is not qualified confidence");
  observe(record, owner, 11);
  require(path(record, owner, 11) == Path::direct, "qualified cheap serial sample selects direct");
  require(record.inspect(owner, 11, policy).ns_per_request == 50, "normalized complete serial cost");

  require(!record.observe(owner, 18, {Path::direct, 0, 0, 200000, 1}, policy),
      "hint issuance supplies no completed demand sample");
  require(path(record, owner, 20) == Path::ring, "hints do not extend completed-demand freshness");

  Record overlap;
  require(!overlap.observe(owner, 10, {Path::ring, 100, 20, 40, 16}, policy),
      "fast Ring throughput is not cheap serial evidence");
  require(!overlap.observe(owner, 10, {Path::direct, 100, 20, 0, 16}, policy),
      "pipelined direct throughput is not cheap serial evidence");
  require(path(overlap, owner, 10) == Path::ring, "parallel observations cannot invent serial confidence");

  observe(record, owner, 21, expensive);
  require(path(record, owner, 21) == Path::ring, "expensive actual serial completion selects Ring");
  require(record.inspect(owner, 21, policy).kind == EvidenceKind::expensive_serial,
      "slow completion is not warmth merely because it is recent");
  observe(record, owner, 22);
  require(path(record, owner, 22) == Path::ring, "slow sample resets confidence sequence");
  observe(record, owner, 23);
  require(path(record, owner, 23) == Path::direct, "subsequent cheap serial samples can recover");
}

void identities_and_lifetimes() {
  Record record;
  observe(record, owner, 10);
  observe(record, owner, 11);
  auto changed = owner;
  changed.owner_incarnation = 42;
  require(path(record, changed, 11) == Path::ring, "address reuse/new owner cannot alias evidence");
  changed = owner; ++changed.generation;
  require(path(record, changed, 11) == Path::ring, "publication or rebinding invalidates prior generation");
  changed = owner; ++changed.access_population;
  require(path(record, changed, 11) == Path::ring, "one query subset does not warm an entire table");
  changed = owner; ++changed.worker;
  require(path(record, changed, 11) == Path::ring, "worker migration rejects private-cache evidence");
  changed = owner; ++changed.cache_domain;
  require(path(record, changed, 11) == Path::ring, "cache-domain migration rejects prior evidence");
  observe(record, changed, 12);
  require(path(record, changed, 12) == Path::ring, "first sample in new placement starts over");
  observe(record, changed, 13);
  require(path(record, changed, 13) == Path::direct, "new placement can acquire its own confidence");
  require(path(record, owner, 13) == Path::ring, "new placement sample does not retain old placement record");
  record.invalidate();
  require(path(record, changed, 13) == Path::ring, "explicit retirement invalidates retained confidence");

  Tag unqualified{};
  require(!record.observe(unqualified, 1, cheap, policy), "missing identity/placement cannot qualify sample");
  require(path(record, unqualified, 1) == Path::ring, "unqualified state stays unknown");
}

void epochs_and_policy() {
  Record record;
  observe(record, owner, 100);
  observe(record, owner, 101);
  require(path(record, owner, 109) == Path::direct, "horizon includes its final epoch");
  require(path(record, owner, 110) == Path::ring, "expired demand sample becomes unknown");
  require(path(record, owner, 100) == Path::ring, "backward epoch cannot appear fresh");
  require(path(record, owner, (Epoch(1) << 32) + 101) == Path::ring,
      "low-bit timestamp wrap after long idle cannot alias freshness");
  observe(record, owner, (Epoch(1) << 32) + 101);
  require(path(record, owner, (Epoch(1) << 32) + 101) == Path::ring,
      "post-idle observation restarts confidence");

  Record wrap;
  constexpr auto last = std::numeric_limits<Epoch>::max();
  observe(wrap, owner, last - 1);
  observe(wrap, owner, last);
  require(path(wrap, owner, last) == Path::direct, "full epoch maximum remains representable");
  require(path(wrap, owner, 0) == Path::ring, "full epoch wrap is unknown rather than recent");
  observe(wrap, owner, 0);
  require(path(wrap, owner, 0) == Path::ring, "full wrap sample starts a new confidence sequence");

  auto tighter = policy; tighter.cheap_ns_per_request = 40;
  require(path(record, owner, (Epoch(1) << 32) + 101, legal, tighter) == Path::ring,
      "policy cutoff changes cannot reuse old confidence");
  observe(record, owner, (Epoch(1) << 32) + 102, cheap, tighter);
  require(record.inspect(owner, (Epoch(1) << 32) + 102, tighter).kind == EvidenceKind::expensive_serial,
      "new tighter cutoff applies to measured serial cost");

  Record fractional;
  observe(fractional, owner, 1, {Path::direct, 1201, 20, 0, 1});
  require(fractional.inspect(owner, 1, policy).kind == EvidenceKind::expensive_serial,
      "fraction above cutoff is not rounded down to cheap");
  Policy largest{std::numeric_limits<std::uint32_t>::max(), 8, 1, 1};
  observe(fractional, owner, 2, {Path::direct, std::numeric_limits<std::uint64_t>::max(), 1, 0, 1}, largest);
  require(fractional.inspect(owner, 2, largest).kind == EvidenceKind::expensive_serial,
      "cost saturation does not fabricate cheap evidence");
}

void controls_and_legal_paths() {
  Record record;
  require(path(record, owner, 1, legal, policy, Control::fixed_direct) == Path::direct,
      "instrumentation-disabled direct control ignores history");
  require(path(record, owner, 1, legal, policy, Control::fixed_ring) == Path::ring,
      "instrumentation-disabled Ring control ignores history");
  require(path(record, owner, 1, legal, policy, Control::caller_known_direct) == Path::direct,
      "caller-known control is explicit prior rather than observed confidence");
  require(record.inspect(owner, 1, policy).kind == EvidenceKind::unknown,
      "caller-known decision does not seed evidence");
  require(!record.observe(owner, 1, cheap, policy, false), "disabled instrumentation accepts no sample");
  require(path(record, owner, 1) == Path::ring, "disabled observation does not seed confidence");

  require(path(record, owner, 1, {true, true, 1, 16}) == Path::direct,
      "serial work lacks useful independent overlap");
  require(path(record, owner, 1, {true, true, 20, 2}) == Path::direct,
      "bank width can bound available independent overlap");
  require(path(record, owner, 1, {true, false, 20, 16}) == Path::direct,
      "caller legal direct-only path is honored");
  require(path(record, owner, 1, {false, true, 1, 1}) == Path::ring,
      "only legal Ring path remains available without direct authority");
  require(path(record, owner, 1, {false, false, 20, 16}) == Path::unavailable,
      "no legal path returns refusal");
  require(path(record, owner, 1, {false, true, 20, 16}, policy, Control::fixed_direct) == Path::unavailable,
      "fixed control does not create access authority");
  require(path(record, owner, 1, {true, true, 20, 0}, policy, Control::fixed_ring) == Path::unavailable,
      "fixed Ring control still needs actual bank inventory");
  require(path(record, owner, 1, {true, true, 20, 0}) == Path::direct,
      "adaptive decision cannot select absent Ring inventory");
}
} // namespace

int main() {
  completion_evidence();
  identities_and_lifetimes();
  epochs_and_policy();
  controls_and_legal_paths();
  std::printf("locality_check: passed; tag_bytes=%zu,record_bytes=%zu\n", sizeof(Tag), sizeof(Record));
}
