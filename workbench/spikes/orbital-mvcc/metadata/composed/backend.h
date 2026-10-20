#pragma once
#include "material.h"
#include "read_fusion.h"
#include "request_cache.h"
#include <algorithm>

namespace orbital_mvcc::composed {
template<class Store> class Backend {
  friend InlineReadFusion;
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
      typename Store::Prepared accepted(store_);
      {
        Scope basis(store_);
        std::pmr::vector<Change> changes(&material_.memory);
        changes.reserve(corpus.images.size());
        for (const auto& image : corpus.images) {
          auto parts = p::need(material_.construct(image.rows, initial, costs));
          changes.push_back({image.address.object, p::need(capture(parts))});
        }
        p::need(store_.prepare(basis, changes, accepted));
      }
      p::need(store_.install(accepted));
    }
    store_.drain(); costs = {}; write_costs = {};
  }
  Backend(const Backend&) = delete;
  const p::Corpus& corpus() const { return material_.corpus(); }
  const Store& catalog() const { return store_; }
  Scope read() const { return Scope{store_}; }
  std::uint64_t cut() const { return store_.borrow().cut; }
  auto statistics() const {
    if constexpr (std::is_same_v<Store, Catalog>) return InlineReadFusion::statistics(store_, material_.memory);
    else return store_.statistics();
  }
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
      typename Store::Prepared accepted(store_);
      {
        Scope basis(store_);
        std::array<Change, 64> changes;
        std::array<pw::Command, 64> commands;
        std::size_t changed{};
        for (std::size_t offered = 0; offered < objects.size(); ++offered) {
          const auto object = objects[offered];
          if (std::find(objects.begin(), objects.begin() + offered, object) != objects.begin() + offered) continue;
          auto selected = select(basis, object, true);
          if (!selected) return std::unexpected(selected.error());
          std::size_t used{};
          for (std::size_t i = 0; i < input.size(); ++i) if (objects[i] == object)
            commands[used++] = {input[i].key, input[i].record.value, input[i].record.tag};
          const auto& initial_shape = material_.shape(material_.format(*selected));
          v::Shape current_shape;
          const auto* target_shape = &initial_shape;
          if (selected->records() != initial_shape.records) {
            auto shaped = v::shape(corpus().schema, initial_shape.format, selected->records());
            if (!shaped) return std::unexpected(shaped.error());
            current_shape = std::move(*shaped); target_shape = &current_shape;
          }
          auto parts = material_.clone(*selected, *target_shape, costs);
          if (!parts) return std::unexpected(parts.error());
          auto written = material_.apply(*selected, *parts,
              std::span(commands).first(used), costs.kernel, write_costs);
          if (!written) return written;
          auto owner = capture(*parts);
          if (!owner) return std::unexpected(owner.error());
          changes[changed++] = {corpus().images[object].address.object, std::move(*owner)};
        }
        auto status = store_.prepare(basis, std::span(changes).first(changed), accepted);
        if (!status) return status;
      }
      auto installed = store_.install(accepted);
      if (!installed) return installed;
    }
    store_.drain(); poll(128);
    for (auto& outcome : output) { outcome = {}; outcome.changes = 1; detail::mix(outcome.digest, 1); }
    return {};
  }
  // Disposable one-Object Tuple stream. Row order is supplied by the caller;
  // the fixture's binary-search readers still require sorted keys. Injection
  // limits/counters describe the tail; refused private work may copy the prefix.
  Result<void> append(std::span<const orbital_bench::Row> input, v::ConstructionOptions options = {}) {
    if (corpus().images.size() != 1 || input.empty()) return std::unexpected(Error::invalid_argument);
    {
      typename Store::Prepared accepted(store_);
      {
        Scope basis(store_);
        auto selected = select(basis, 0); // Primary Tuple, not a replaceable alternative.
        if (!selected) return std::unexpected(selected.error());
        if (selected->records() > SIZE_MAX - input.size()) return std::unexpected(Error::capacity);
        auto shaped = v::shape(corpus().schema, v::Format::rows, selected->records() + input.size());
        if (!shaped) return std::unexpected(shaped.error());
        const auto target_shape = std::move(*shaped); // Stationary until capture completes.
        auto parts = material_.append(*selected, target_shape, input, costs, options);
        if (!parts) return std::unexpected(parts.error());
        auto owner = capture(*parts);
        if (!owner) return std::unexpected(owner.error());
        const std::array<Change, 1> changes{{{corpus().images[0].address.object, std::move(*owner)}}};
        const auto prepared = store_.prepare(basis, changes, accepted);
        if (!prepared) return prepared;
      }
      const auto installed = store_.install(accepted);
      if (!installed) return installed;
    }
    store_.drain(); poll(128); return {};
  }
  Result<Conversion> prepare_conversion(const Scope& scope, std::size_t object,
      v::Format format, v::ConstructionOptions options = {}) {
    auto selected = select(scope, object, true);
    if (!selected) return std::unexpected(selected.error());
    auto target = store_.conversion(scope, corpus().images[object].address.object);
    if (!target) return std::unexpected(target.error());
    const auto& initial_shape = material_.shape(format);
    v::Shape current_shape;
    const auto* target_shape = &initial_shape;
    if (selected->records() != initial_shape.records) {
      auto shaped = v::shape(corpus().schema, format, selected->records());
      if (!shaped) return std::unexpected(shaped.error());
      current_shape = std::move(*shaped); target_shape = &current_shape;
    }
    auto parts = material_.convert(*selected, *target_shape, costs, options);
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
    // One-shot scalar trial seam. body() returns a borrow protected by this
    // Scope, including a captured alternative; owning Part export is separate.
    [[gnu::always_inline]] void consume_into(std::size_t object, const orbital_bench::Operation& op,
        orbital_bench::Outcome& output)
        requires(Capacity == 1 && std::is_same_v<Store, Catalog>) {
      if (alternative_) { output = body(object).consume(op); return; }
      if (object >= owner_.corpus().images.size())
        p::need(Result<void>{std::unexpected(Error::invalid_argument)});
      const auto status = InlineReadFusion::consume(owner_, scope_,
          owner_.corpus().images[object].address.object, op, output);
      if (!status) p::need(status);
      ++bindings;
    }
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
using Inline = Backend<Catalog>;
} // namespace orbital_mvcc::composed
