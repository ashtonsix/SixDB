#pragma once
#include "../../../metadata/composed/backend.h"

// Private measurement control: one append-only Tuple Object, one caller-given
// reservation. No updates, conversions, rollover, navigation or reserve policy.
namespace orbital_mvcc::composed {
class PrefixGrowth {
  Material material_;
  std::shared_ptr<oa::Allocation> backing_;
  Catalog store_;
  std::size_t reservation_, shared_owner_bytes_{};
  std::uint64_t growth_calls_{};
  using Owner = Catalog::Owner;
  using Change = Catalog::Change;

  Result<Owner> capture(cs::Shape* proof, std::size_t count) {
    const std::array<cs::BodySource, 1> sources{{{cs::geometry(proof, 0).id,
        {backing_->address, count * v::tuple_bytes}, backing_, nullptr}}};
    return store_.capture({proof, count, cs::Owners::independent_shared}, sources);
  }
  Result<void> suffix(cs::Selection selected, const v::Shape& target,
      std::span<const orbital_bench::Row> input, v::ConstructionOptions options) {
    // Keep the selected fresh-copy append's count, shape, input and native-read
    // admission. Only backing acquisition and the old-prefix copy differ.
    if (!selected) return std::unexpected(Error::absent);
    if (material_.format(selected) != v::Format::rows || target.format != v::Format::rows ||
        target.part_count != 1 || input.empty() || selected.records() > SIZE_MAX - input.size() ||
        target.records != selected.records() + input.size() || target.records > SIZE_MAX / v::tuple_bytes)
      return std::unexpected(Error::invalid_argument);
    if (target.parts[0].id != cs::geometry(selected.shape(), 0).id ||
        target.parts[0].body_bytes != target.records * v::tuple_bytes ||
        target.parts[0].alignment != 1 || target.parts[0].readable_padding != 0)
      return std::unexpected(Error::invalid_argument);
    if (input.size() > SIZE_MAX / sizeof(orbital_bench::Row)) return std::unexpected(Error::capacity);
    const auto input_begin = reinterpret_cast<std::uintptr_t>(input.data());
    const auto input_bytes = input.size() * sizeof(orbital_bench::Row);
    if (!input_begin || input_begin > UINTPTR_MAX - input_bytes) return std::unexpected(Error::invalid_argument);
    for (const auto& row : input)
      if (row.record.value > orbital_bench::value_mask) return std::unexpected(Error::invalid_argument);
    (void)material_.generic(selected);
    auto tail_shape = v::shape(corpus().schema, v::Format::rows, input.size());
    if (!tail_shape) return std::unexpected(tail_shape.error());
    const auto source = selected.part(target.parts[0].id);
    if (!source) return std::unexpected(source.error());
    const auto old_bytes = cs::geometry(selected.shape(), 0).extent(selected.records());
    const auto bytes = target.parts[0].body_bytes;
    if (!old_bytes || *old_bytes > bytes) return std::unexpected(Error::invalid_argument);
    const auto address = reinterpret_cast<std::uintptr_t>(backing_->address);
    if (source->address() != backing_->address || address > UINTPTR_MAX - bytes ||
        (input_begin < address + bytes && address < input_begin + input_bytes))
      return std::unexpected(Error::invalid_argument);
    if (bytes > reservation_) return std::unexpected(Error::conflict);
    // No speculative captured Owner or Prepared packet can escape append().
    // They end on EVERY return before another call can reuse the suffix. This
    // API takes no stale basis and exposes no other publication operation.
    costs.requested_body_bytes += bytes; ++costs.requested_parts;
    if (bytes > backing_->size) {
      auto held = oa::hold(*backing_, {0, backing_->size + backing_->readable_padding});
      if (!held) return std::unexpected(held.error());
      ++growth_calls_;
      auto grown = oa::grow(*backing_, {.bytes = bytes, .alignment = 1});
      if (!grown) return grown;
      // The Hold's lifetime includes grow; shared_ptr alone does not pin address.
    }
    const std::array<v::WritePart, 1> tail{{{tail_shape->parts[0].id,
        {backing_->address + *old_bytes, bytes - *old_bytes}}}};
    return v::construct_into(corpus().schema, *tail_shape, tail, input, costs.kernel, options);
  }
public:
  using Scope = Catalog::Scope;
  v::Costs costs;
  wb::ProbeCosts write_costs;
  PrefixGrowth(const p::Corpus& corpus, std::size_t bytes, std::size_t reservation,
      std::size_t metadata_bytes = 0)
      : material_(corpus, bytes, metadata_bytes), store_(material_.memory), reservation_(reservation) {
    if (corpus.images.size() != 1 || !reservation_)
      throw std::invalid_argument("shared-prefix requires one Object and explicit reservation");
    const auto& shape = material_.shape(v::Format::rows);
    if (reservation_ < shape.parts[0].body_bytes)
      throw std::invalid_argument("reservation is smaller than the initial Tuple");
    PrivateParts parts; parts.shape = &shape;
    parts.allocations[0] = p::need(oa::allocate(*material_.runtime.pool,
        {.bytes = shape.parts[0].body_bytes, .reserved_bytes = reservation_, .alignment = shape.parts[0].alignment,
         .readable_padding = shape.parts[0].readable_padding, .initialization = oa::Initialization::uninitialized}));
    parts.writable[0] = {shape.parts[0].id, oa::bytes(parts.allocations[0])};
    p::need(v::construct_into(corpus.schema, shape, parts.parts(), corpus.images[0].rows, costs.kernel));
    auto* proof = p::need(material_.proof(parts));
    const auto before = material_.memory.live;
    backing_ = std::allocate_shared<oa::Allocation>(
        std::pmr::polymorphic_allocator<oa::Allocation>(&material_.memory), std::move(parts.allocations[0]));
    shared_owner_bytes_ = material_.memory.live - before;
    parts.writable = {}; // Initial mutable placements end before capture.
    {
      Catalog::Prepared accepted(store_);
      {
        Scope basis(store_);
        const std::array<Change, 1> changes{{{corpus.images[0].address.object, p::need(capture(proof, shape.records))}}};
        p::need(store_.prepare(basis, changes, accepted));
      }
      p::need(store_.install(accepted));
    }
    store_.drain(); costs = {}; write_costs = {};
  }
  PrefixGrowth(const PrefixGrowth&) = delete;
  const p::Corpus& corpus() const { return material_.corpus(); }
  const Catalog& catalog() const { return store_; }
  Scope read() const { return Scope{store_}; }
  std::uint64_t cut() const { return store_.borrow().cut; }
  auto statistics() const { return InlineReadFusion::statistics(store_, material_.memory); }
  Accounting accounting() const { return material_.accounting(); }
  void metadata_limit(std::size_t bytes) { material_.memory.limit = bytes; }
  std::size_t drain() { return store_.drain(); }
  void poll(std::size_t count) { orbital::worker::poll(material_.runtime.lane, count); }
  std::size_t shared_owner_bytes() const { return shared_owner_bytes_; }
  std::uint64_t growth_calls() const { return growth_calls_; }
  Body body(const Scope& scope, std::size_t object) const {
    if (object) throw std::invalid_argument("one Object only");
    auto selected = scope.bind(corpus().images[0].address.object);
    if (!selected) throw std::invalid_argument("absent prefix");
    return {&material_, selected};
  }
  Result<void> append(std::span<const orbital_bench::Row> input, v::ConstructionOptions options = {}) {
    if (input.empty()) return std::unexpected(Error::invalid_argument);
    {
      Catalog::Prepared accepted(store_);
      {
        Scope basis(store_);
        auto selected = basis.bind(corpus().images[0].address.object);
        if (!selected) return std::unexpected(Error::absent);
        if (selected.records() > SIZE_MAX - input.size()) return std::unexpected(Error::capacity);
        auto shaped = v::shape(corpus().schema, v::Format::rows, selected.records() + input.size());
        if (!shaped) return std::unexpected(shaped.error());
        const auto target_shape = std::move(*shaped);
        auto written = suffix(selected, target_shape, input, options);
        if (!written) return written;
        auto owner = capture(const_cast<cs::Shape*>(selected.shape()), target_shape.records);
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
};
} // namespace orbital_mvcc::composed
