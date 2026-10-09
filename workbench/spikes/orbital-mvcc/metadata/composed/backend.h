#pragma once
#include "material.h"
#include "../../performance/request_cache.h"
#include <algorithm>

namespace orbital_mvcc::composed {
template<class Store> class Backend {
  Material material_;
  Store store_;
  using Owner = typename Store::Owner;
  using Change = typename Store::Change;
  Result<Owner> capture(PrivateParts& parts) {
    auto proof = material_.proof(parts);
    if (!proof) return std::unexpected(proof.error());
    std::array<cs::BodySource, 4> sources;
    for (unsigned i = 0; i < parts.shape->part_count; ++i)
      sources[i] = {parts.shape->parts[i].id, parts.writable[i].bytes, {}, &parts.allocations[i]};
    return store_.capture({*proof, parts.shape->records, cs::Owners::independent_allocation},
        std::span(sources).first(parts.shape->part_count));
  }
public:
  using Scope = typename Store::Scope;
  struct Conversion {
    typename Store::ConversionTarget target;
    Owner replacement;
  };
  v::Costs costs;
  wb::ProbeCosts write_costs;
  Backend(const p::Corpus& corpus, std::size_t bytes, v::Format initial = v::Format::rows,
      std::size_t metadata_bytes = 0) : material_(corpus, bytes, metadata_bytes), store_(material_.memory) {
    {
      std::optional<Scope> basis(std::in_place, store_);
      std::pmr::vector<Change> changes(&material_.memory);
      changes.reserve(corpus.images.size());
      for (const auto& image : corpus.images) {
        auto parts = p::need(material_.construct(image.rows, initial, costs));
        changes.push_back({image.address.object, p::need(capture(parts))});
      }
      auto accepted = p::need(store_.prepare(*basis, changes));
      basis.reset(); p::need(store_.install(accepted));
    }
    store_.drain(); costs = {}; write_costs = {};
  }
  Backend(const Backend&) = delete;
  const p::Corpus& corpus() const { return material_.corpus(); }
  const Store& catalog() const { return store_; }
  Scope read() const { return Scope{store_}; }
  std::uint64_t cut() const { return store_.borrow().cut; }
  auto statistics() const { return store_.statistics(); }
  Accounting accounting() const { return material_.accounting(); }
  void metadata_limit(std::size_t bytes) { material_.memory.limit = bytes; }
  std::size_t drain() { return store_.drain(); }
  void poll(std::size_t count) { orbital::worker::poll(material_.runtime.lane, count); }

  Result<cs::Selection> select(const Scope& scope, std::size_t object, bool alternative = false) const {
    if (object >= corpus().images.size()) return std::unexpected(Error::invalid_argument);
    const auto id = corpus().images[object].address.object;
    if (alternative) {
      auto result = scope.bind_alternative(id);
      if (result || result.error() != Error::absent) return result;
    }
    auto selected = scope.bind(id);
    if (!selected) return std::unexpected(Error::absent);
    return selected;
  }
  Body body(const Scope& scope, std::size_t object, bool alternative = false) const {
    return {&material_, p::need(select(scope, object, alternative))};
  }

  // Successful outcomes are emitted only after actual multi-Object install.
  // Clone before key resolution: refusal may consume copies, and capacity can
  // precede absence. The installed state and caller Outcomes remain intact.
  Result<void> update(std::span<const orbital_bench::Operation> input,
      std::span<orbital_bench::Outcome> output, std::span<const std::size_t> objects) {
    using namespace orbital_bench;
    if (input.empty() || input.size() > 64 || output.size() != input.size() || objects.size() != input.size())
      return std::unexpected(Error::invalid_argument);
    for (std::size_t i = 0; i < input.size(); ++i)
      if (objects[i] >= corpus().images.size() || input[i].kind != Kind::update || input[i].record.value > value_mask)
        return std::unexpected(Error::invalid_argument);
    {
      std::optional<Scope> basis(std::in_place, store_);
      std::array<Change, 64> changes;
      std::array<pw::Command, 64> commands;
      std::size_t changed{};
      for (std::size_t offered = 0; offered < objects.size(); ++offered) {
        const auto object = objects[offered];
        if (std::find(objects.begin(), objects.begin() + offered, object) != objects.begin() + offered) continue;
        auto selected = select(*basis, object, true);
        if (!selected) return std::unexpected(selected.error());
        std::size_t used{};
        for (std::size_t i = 0; i < input.size(); ++i) if (objects[i] == object)
          commands[used++] = {input[i].key, input[i].record.value, input[i].record.tag};
        auto parts = material_.clone(*selected, costs);
        if (!parts) return std::unexpected(parts.error());
        auto written = material_.apply(*selected, *parts,
            std::span(commands).first(used), costs.kernel, write_costs);
        if (!written) return written;
        auto owner = capture(*parts);
        if (!owner) return std::unexpected(owner.error());
        changes[changed++] = {corpus().images[object].address.object, std::move(*owner)};
      }
      auto accepted = store_.prepare(*basis, std::span(changes).first(changed));
      if (!accepted) return std::unexpected(accepted.error());
      basis.reset();
      auto installed = store_.install(*accepted);
      if (!installed) return installed;
    }
    store_.drain(); poll(128);
    for (auto& outcome : output) { outcome = {}; outcome.changes = 1; detail::mix(outcome.digest, 1); }
    return {};
  }
  Result<Conversion> prepare_conversion(const Scope& scope, std::size_t object,
      v::Format format, v::ConstructionOptions options = {}) {
    auto selected = select(scope, object, true);
    if (!selected) return std::unexpected(selected.error());
    auto target = store_.conversion(scope, corpus().images[object].address.object);
    if (!target) return std::unexpected(target.error());
    auto parts = material_.convert(*selected, format, costs, options);
    if (!parts) return std::unexpected(parts.error());
    auto owner = capture(*parts);
    if (!owner) return std::unexpected(owner.error());
    return Conversion{std::move(*target), std::move(*owner)};
  }
  Result<void> adopt(const Conversion& conversion) {
    return store_.adopt(conversion.target, conversion.replacement);
  }
  Result<void> convert(std::size_t object, v::Format format, v::ConstructionOptions options = {}) {
    {
      Scope basis(store_);
      auto prepared = prepare_conversion(basis, object, format, options);
      if (!prepared) return std::unexpected(prepared.error());
      auto accepted = adopt(*prepared);
      if (!accepted) return accepted;
    }
    store_.drain(); poll(128); return {};
  }

  template<std::size_t Capacity = 64> class Bodies {
    const Backend& owner_;
    Scope scope_;
    p::RequestCache<cs::Selection, Capacity> cache_;
    bool alternative_;
  public:
    std::uint64_t bindings{};
    explicit Bodies(const Backend& owner, bool alternative = false)
        : owner_(owner), scope_(owner.catalog()), alternative_(alternative) {}
    Body body(std::size_t object) {
      auto selected = cache_.get(object, [&] { return p::need(owner_.select(scope_, object, alternative_)); }, bindings);
      return {&owner_.material_, selected};
    }
  };
  template<std::size_t Capacity = 64> Bodies<Capacity> bodies(bool alternative = false) const {
    return Bodies<Capacity>{*this, alternative};
  }
  static constexpr std::size_t write_caller_arrays = sizeof(std::array<Change, 64>) + sizeof(std::array<pw::Command, 64>);
};
using Inline = Backend<compact_history::InlineShapeStore16>;
using Directory = Backend<compact_history::CompactShapeStore16>;
} // namespace orbital_mvcc::composed
