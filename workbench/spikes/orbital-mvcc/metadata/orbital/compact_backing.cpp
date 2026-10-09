// Complete requested-state accounting; decoded fixture words are not timings.
#include <orbital/object.h>
#include "../../baseline/src/internal.h"
#include "candidates/candidate_store.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <ctime>

namespace baseline = orbital::object;
namespace candidate = compact_history;
namespace descriptor = compact_descriptor;
using orbital::ObjectId;

template<class T> T need(orbital::Result<T> result) {
  if (!result) std::abort();
  return std::move(*result);
}

void need(orbital::Result<void> result) { if (!result) std::abort(); }

std::uint64_t value(std::uint64_t cut, std::size_t object, std::size_t part) {
  return cut * 100000 + object * 100 + part;
}

std::uint64_t cpu_ns() {
  timespec time{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) std::abort();
  return std::uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}

template<class Source> std::vector<Source> sources(
    std::size_t count, std::uint64_t cut, std::size_t object) {
  static const std::array<std::uint64_t, 1> metadata{55};
  std::vector<Source> result;
  for (std::size_t part = 0; part < count; ++part) {
    auto body = std::make_shared<const std::uint64_t>(value(cut, object, part));
    result.push_back({{0, part}, std::as_bytes(std::span(body.get(), 1)),
        std::as_bytes(std::span(metadata)), 0, 8, std::move(body)});
  }
  return result;
}

struct Baseline {
  using Scope = baseline::ReadContext;
  baseline::Store store;
  std::uint64_t cut{};
  std::size_t copied_bytes{}, maintenance_work{}, maintenance_cpu_ns{};
  static constexpr const char* name = "frozen88";
  static constexpr std::size_t resource_bytes = 0;
  explicit Baseline(orbital::worker::Lane& lane)
      : store(need(baseline::Store::open(lane, 1 << 28))) {}
  void enroll(std::optional<Scope>& scope) { scope.emplace(need(store.read())); }
  std::size_t bytes() const { return store.statistics().metadata_bytes; }
  std::size_t peak() const { return store.statistics().metadata_peak; }
  std::size_t versions() const { return store.statistics().versions; }
  void drain() {
    const auto start = cpu_ns();
    while (auto work = store.drain(65536)) maintenance_work += work;
    maintenance_cpu_ns += cpu_ns() - start;
  }
  std::string allocation_calls() const { return "null"; }
  std::size_t caller_changes() const { return 0; }
  std::size_t history_capacity() const { return versions() * sizeof(baseline::detail::Version); }
  std::size_t directory_capacity() const { return 0; }
  std::size_t sweep_scratch() const { return 0; }
  void publish(std::size_t parts, std::size_t first, std::size_t width) {
    const std::array<std::uint64_t, 1> metadata{55};
    const auto description = std::as_bytes(std::span(metadata));
    auto draft = need(store.edit());
    for (std::size_t object = first; object < first + width; ++object) {
      auto parts_source = sources<baseline::PartSource>(parts, cut + 1, object);
      auto representation = need(store.capture(
          {{0, object + 1}, cut + 1, {}, {}, description, description, {}}, parts_source));
      need(draft.put(std::move(representation)));
    }
    auto prepared = need(store.prepare(draft));
    need(store.install(prepared));
    ++cut;
    copied_bytes += width * (16 + parts * 8);
  }
  std::uint64_t read(const Scope& scope, std::size_t object, std::size_t part) {
    auto selected = need(baseline::select(scope, {0, object + 1}));
    auto view = need(selected.part(part));
    std::uint64_t result;
    std::memcpy(&result, view.body().data(), sizeof(result));
    return result;
  }
};

template<class StoreType> struct Compact {
  using Scope = typename StoreType::Scope;
  candidate::Memory memory;
  StoreType store{memory};
  std::uint64_t cut{};
  std::size_t copied_bytes{}, maintenance_work{}, change_capacity{}, maintenance_cpu_ns{};
  static constexpr std::size_t resource_bytes = sizeof(candidate::Memory);
  explicit Compact(orbital::worker::Lane&) {}
  void enroll(std::optional<Scope>& scope) { scope.emplace(store); }
  std::size_t bytes() const { return memory.live; }
  std::size_t peak() const { return memory.peak; }
  std::size_t versions() const { return store.statistics().versions; }
  void drain() {
    const auto start = cpu_ns();
    maintenance_work += store.drain();
    maintenance_cpu_ns += cpu_ns() - start;
  }
  std::string allocation_calls() const { return std::to_string(memory.calls); }
  std::size_t caller_changes() const { return change_capacity; }
  std::size_t history_capacity() const { return store.statistics().history_capacity_bytes; }
  std::size_t directory_capacity() const { return store.statistics().descriptor_directory_capacity_bytes; }
  std::size_t sweep_scratch() const { return store.statistics().last_sweep_bytes; }
  void publish(std::size_t parts, std::size_t first, std::size_t width) {
    const std::array<std::uint64_t, 1> metadata{55};
    const auto description = std::as_bytes(std::span(metadata));
    Scope basis(store);
    std::vector<candidate::Change> changes;
    for (std::size_t object = first; object < first + width; ++object) {
      auto parts_source = sources<descriptor::PartSource>(parts, cut + 1, object);
      auto representation = need(store.capture(
          {{0, object + 1}, cut + 1, {}, {}, description, description, {}}, parts_source));
      changes.push_back({{0, object + 1}, std::move(representation)});
    }
    typename StoreType::Prepared prepared(store);
    need(store.prepare(basis, changes, prepared));
    need(store.install(prepared));
    ++cut;
    copied_bytes += width * (16 + parts * 8);
    change_capacity = std::max(change_capacity, changes.capacity() * sizeof(candidate::Change));
  }
  std::uint64_t read(const Scope& scope, std::size_t object, std::size_t part) {
    auto selected = scope.bind({0, object + 1});
    if (!selected) std::abort();
    auto view = need(selected.part(part));
    std::uint64_t result;
    std::memcpy(&result, view.body().data(), sizeof(result));
    return result;
  }
};

template<class Backend> void matrix(orbital::worker::Lane& lane, const char* name,
    std::size_t parts, std::size_t objects, bool dense, std::size_t publications) {
  Backend backend(lane);
  const auto setup = backend.bytes();
  backend.publish(parts, 0, objects);
  backend.drain();
  const auto initial = backend.bytes();
  std::optional<typename Backend::Scope> old;
  backend.enroll(old);
  const auto enrolled = backend.bytes();
  std::vector<std::uint64_t> newest(objects, 1);
  for (std::size_t publication = 1; publication < publications; ++publication) {
    const auto first = dense ? 0 : (publication - 1) % objects;
    const auto width = dense ? objects : 1;
    backend.publish(parts, first, width);
    for (std::size_t object = first; object < first + width; ++object)
      newest[object] = publication + 1;
    backend.drain();
  }
  const auto held = backend.bytes();
  const auto held_versions = backend.versions();
  const auto held_allocations = backend.allocation_calls();
  const auto work_held = backend.maintenance_work;
  const auto cpu_held = backend.maintenance_cpu_ns;
  const auto scratch_held = backend.sweep_scratch();
  {
    std::optional<typename Backend::Scope> current;
    backend.enroll(current);
    for (std::size_t object = 0; object < objects; ++object) {
      for (std::size_t part = 0; part < parts; ++part) {
        if (backend.read(*old, object, part) != value(1, object, part) ||
            backend.read(*current, object, part) != value(newest[object], object, part))
          std::abort();
      }
    }
  }
  old.reset();
  backend.drain();
  const auto released = backend.bytes();
  const auto released_versions = backend.versions();
  std::printf("{\"candidate\":\"%s\",\"parts\":%zu,\"objects\":%zu,"
      "\"publication_width\":%zu,\"publications\":%zu,\"setup\":%zu,"
      "\"initial\":%zu,\"enrolled\":%zu,\"held\":%zu,\"released\":%zu,"
      "\"peak\":%zu,\"held_versions\":%zu,\"released_versions\":%zu,"
      "\"allocation_calls_held\":%s,\"allocation_calls_released\":%s,"
      "\"copied_descriptor_bytes\":%zu,\"maintenance_work_held\":%zu,"
      "\"maintenance_work_released\":%zu,\"caller_change_capacity_bytes\":%zu,"
      "\"history_capacity_bytes_released\":%zu,\"descriptor_directory_capacity_bytes_released\":%zu,"
      "\"sweep_scratch_bytes_held\":%zu,\"sweep_cpu_ns_held\":%zu,\"sweep_cpu_ns_released\":%zu,"
      "\"caller_resource_bytes\":%zu,\"caller_scope_bytes\":%zu,"
      "\"caller_scope_storage_bytes\":%zu,"
      "\"external_body_bytes_held\":%zu}\n", name, parts, objects,
      dense ? objects : 1, publications, setup, initial, enrolled, held, released,
      backend.peak(), held_versions, released_versions, held_allocations.c_str(),
      backend.allocation_calls().c_str(), backend.copied_bytes, work_held,
      backend.maintenance_work - work_held, backend.caller_changes(),
      backend.history_capacity(), backend.directory_capacity(), scratch_held, cpu_held,
      backend.maintenance_cpu_ns - cpu_held, Backend::resource_bytes,
      sizeof(typename Backend::Scope), sizeof(old),
      held_versions * parts * sizeof(std::uint64_t));
}

int main() {
  orbital::worker::Lane lane;
  need(orbital::worker::adopt(lane));
  for (auto parts : {1u, 4u}) {
    for (auto objects : {1u, 64u}) {
      for (auto publications : {1u, 8u}) {
        for (auto dense : {false, true}) {
          if (objects == 1 && dense) continue;
          if (publications == 1 && dense) continue;
          matrix<Baseline>(lane, "frozen88", parts, objects, dense, publications);
          matrix<Compact<candidate::Store32>>(lane, "direct32", parts, objects, dense, publications);
          matrix<Compact<candidate::Store16>>(lane, "indexed16", parts, objects, dense, publications);
          matrix<Compact<candidate::Store8>>(lane, "chunked8", parts, objects, dense, publications);
          using Exact16 = candidate::Store<candidate::Slab<candidate::Version16>,
              compact_retention::DistinctRetention, candidate::DescriptorPhysical>;
          matrix<Compact<Exact16>>(lane, "indexed16-live-cuts", parts, objects, dense, publications);
        }
      }
    }
  }
  orbital::worker::relinquish(lane);
}
