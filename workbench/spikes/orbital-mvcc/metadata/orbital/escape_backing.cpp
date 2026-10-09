// Requested escape state and actual sibling-owner release, outside any clock.
#include "orbital/src/object/internal.h"
#include "candidates/candidate_store.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace baseline = orbital::object;
namespace candidate = compact_history;
namespace physical = compact_descriptor;

template<class T> T need(orbital::Result<T> result) {
  if (!result) std::abort();
  return std::move(*result);
}

void need(orbital::Result<void> result) { if (!result) std::abort(); }

template<class Source> std::vector<Source> sources(std::size_t parts,
    std::vector<std::weak_ptr<const void>>& witnesses) {
  static const std::array<std::uint64_t, 1> metadata{55};
  std::vector<Source> result;
  for (std::size_t part = 0; part < parts; ++part) {
    auto body = std::make_shared<const std::uint64_t>(100 + part);
    witnesses.emplace_back(body);
    result.push_back({{0, part}, std::as_bytes(std::span(body.get(), 1)),
        std::as_bytes(std::span(metadata)), 0, 8, std::move(body)});
  }
  return result;
}

template<class View> void check(View view,
    const std::vector<std::weak_ptr<const void>>& witnesses) {
  std::uint64_t value{};
  std::memcpy(&value, view.body().data(), sizeof(value));
  if (value != 100 || witnesses[0].expired()) std::abort();
  for (std::size_t i = 1; i < witnesses.size(); ++i)
    if (!witnesses[i].expired()) std::abort();
}

void baseline_escape(orbital::worker::Lane& lane, std::size_t parts) {
  auto store = need(baseline::Store::open(lane, 1 << 24));
  auto* domain = baseline::detail::Api::catalog(store)->domain;
  const auto setup = domain->charged;
  std::vector<std::weak_ptr<const void>> witnesses;
  auto input = sources<baseline::PartSource>(parts, witnesses);
  const std::array<std::uint64_t, 1> metadata{55};
  const auto description = std::as_bytes(std::span(metadata));
  auto owner = need(store.capture({{0, 1}, 1, {}, {}, description, description, {}}, input));
  input.clear();
  const auto captured = domain->charged;
  auto escaped = baseline::retain(need(owner.part(std::size_t{0})));
  const auto exported = domain->charged;
  owner = {};
  while (store.drain(4096)) {}
  const auto representation_dropped = domain->charged;
  store = {};
  check(escaped.view(), witnesses);
  const auto store_dropped = domain->charged;
  std::printf("{\"candidate\":\"frozen88\",\"parts\":%zu,\"setup\":%zu,"
      "\"captured\":%zu,\"exported\":%zu,\"representation_dropped\":%zu,"
      "\"store_dropped\":%zu,\"peak\":%zu,\"caller_resource_bytes\":0,"
      "\"allocation_calls\":null,\"surviving_external_body_bytes\":8}\n",
      parts, setup, captured, exported, representation_dropped, store_dropped, domain->peak);
  escaped = {};
  if (!witnesses[0].expired()) std::abort();
}

template<class Store> void compact_escape(orbital::worker::Lane&, std::size_t parts, const char* name) {
  candidate::Memory memory;
  Store store(memory);
  const auto setup = memory.live;
  std::vector<std::weak_ptr<const void>> witnesses;
  auto input = sources<physical::PartSource>(parts, witnesses);
  const std::array<std::uint64_t, 1> metadata{55};
  const auto description = std::as_bytes(std::span(metadata));
  auto owner = need(store.capture({{0, 1}, 1, {}, {}, description, description, {}}, input));
  input.clear();
  const auto captured = memory.live;
  auto* escaped = need(physical::retain_part(owner.selection(), 0));
  const auto exported = memory.live;
  owner = {};
  store.drain();
  const auto representation_dropped = memory.live;
  store = {};
  check(physical::view(escaped), witnesses);
  std::printf("{\"candidate\":\"%s\",\"parts\":%zu,\"setup\":%zu,"
      "\"captured\":%zu,\"exported\":%zu,\"representation_dropped\":%zu,"
      "\"store_dropped\":%zu,\"peak\":%zu,\"caller_resource_bytes\":%zu,"
      "\"allocation_calls\":%zu,\"surviving_external_body_bytes\":8}\n",
      name, parts, setup, captured, exported, representation_dropped, memory.live, memory.peak,
      sizeof(memory), memory.calls);
  physical::release(escaped);
  if (!witnesses[0].expired() || memory.live != 0) std::abort();
}

int main() {
  orbital::worker::Lane lane;
  need(orbital::worker::adopt(lane));
  for (auto parts : {1u, 4u, 17u}) {
    baseline_escape(lane, parts);
    compact_escape<candidate::Store32>(lane, parts, "direct32");
    compact_escape<candidate::Store16>(lane, parts, "indexed16");
    compact_escape<candidate::Store8>(lane, parts, "chunked8");
  }
  orbital::worker::relinquish(lane);
}
