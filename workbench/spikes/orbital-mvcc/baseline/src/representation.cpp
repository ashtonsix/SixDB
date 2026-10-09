// Immutable descriptions and Part backing; private Allocation-backed writes.

#include "internal.h"
#include <bit>
#include <stdexcept>

namespace orbital::object::detail {

Info::Info(Domain& domain, const RepresentationSpec& spec)
    : Base(domain), logical(&domain), interpretation(&domain), dependencies(spec.dependencies) {
  if (!spec.logical_metadata.empty())
    logical.assign(spec.logical_metadata.begin(), spec.logical_metadata.end());
  if (!spec.interpretation.empty())
    interpretation.assign(spec.interpretation.begin(), spec.interpretation.end());
  description = {spec.object, spec.version, spec.replica, spec.format, logical, interpretation};
}

Part::Part(Domain& domain, Ref<Info> description, PartId id, Bytes metadata)
    : Base(domain), info(std::move(description)), interpretation(&domain) {
  if (!metadata.empty()) interpretation.assign(metadata.begin(), metadata.end());
  leaf.description = &info->description;
  leaf.id = id;
  leaf.interpretation = interpretation;
  leaf.owner = this;
}

Rep::Rep(Domain& domain, Ref<Info> description)
    : Base(domain), info(std::move(description)), parts(&domain), leaves(&domain), lookup(&domain) {}

void retain_ref(Info& value) noexcept { retain_base(value); }

void release_ref(Info& value) noexcept { release_base(value); }

void retain_ref(Part& value) noexcept { retain_base(value); }

void release_ref(Part& value) noexcept { release_base(value); }

void retain_ref(Rep& value) noexcept { retain_base(value); }

void release_ref(Rep& value) noexcept { release_base(value); }

void retain_ref(Build& value) noexcept { retain_base(value); }

void release_ref(Build& value) noexcept { release_base(value); }

} // namespace orbital::object::detail

namespace orbital::object {
namespace {

bool valid_span(Bytes value) noexcept {
  if (value.empty()) return true;
  if (!value.data()) return false;
  const auto address = reinterpret_cast<std::uintptr_t>(value.data());
  return value.size() <= std::numeric_limits<std::uintptr_t>::max() - address;
}

bool valid_spec(const RepresentationSpec& spec) noexcept {
  return valid_span(spec.logical_metadata) && valid_span(spec.interpretation);
}

template<class Part>
bool unique_parts(detail::Domain& domain, std::span<const Part> parts) {
  if (parts.size() <= 16) {
    for (std::size_t i = 0; i < parts.size(); ++i)
      for (std::size_t j = 0; j < i; ++j)
        if (parts[i].id == parts[j].id) return false;
    return true;
  }
  std::pmr::vector<PartId> ids(&domain);
  ids.reserve(parts.size());
  for (const auto& part : parts) ids.push_back(part.id);
  std::sort(ids.begin(), ids.end());
  return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

bool valid_source(const PartSource& part) noexcept {
  if (!std::has_single_bit(part.alignment) || !valid_span(part.body) ||
      !valid_span(part.interpretation) ||
      part.body.size() > std::numeric_limits<std::size_t>::max() - part.readable_padding)
    return false;
  const auto extent = part.body.size() + part.readable_padding;
  const auto address = reinterpret_cast<std::uintptr_t>(part.body.data());
  if (address & (part.alignment - 1)) return false;
  if (!extent) return true;
  return part.body.data() && part.backing.use_count() &&
      extent <= std::numeric_limits<std::uintptr_t>::max() - address;
}

bool valid_plan(const PartPlan& plan) noexcept {
  const auto& request = plan.allocation;
  if (!valid_span(plan.interpretation) || !std::has_single_bit(request.alignment) ||
      request.bytes > std::numeric_limits<std::size_t>::max() - request.readable_padding)
    return false;
  return !request.reserved_bytes ||
      request.reserved_bytes >= request.bytes + request.readable_padding;
}

void finish(detail::Rep& rep) {
  if (rep.leaves.size() > 16) {
    rep.lookup.assign(rep.leaves.begin(), rep.leaves.end());
    std::sort(rep.lookup.begin(), rep.lookup.end(), [](const auto* a, const auto* b) {
      return a->id < b->id;
    });
  }
  rep.finish();
}

void close_writer(Ref<detail::Build>& build, std::size_t index) noexcept {
  if (!build) return;
  build->domain->owner();
  assert(index < build->active.size() && build->active[index] && build->writers);
  build->active[index] = false;
  --build->writers;
  build.reset();
}

void abandon(Ref<detail::Build>& build) noexcept {
  if (!build) return;
  build->domain->owner();
  build->revoked = true;
  build.reset();
}

} // namespace

const Description& Representation::description() const noexcept {
  assert(rep_);
  rep_->domain->owner();
  return rep_->info->description;
}

std::size_t Representation::part_count() const noexcept {
  if (!rep_) return 0;
  rep_->domain->owner();
  return rep_->parts.size();
}

Result<View> Representation::part(std::size_t index) const & noexcept {
  if (!rep_) return std::unexpected(Error::absent);
  rep_->domain->owner();
  if (index >= rep_->parts.size()) return std::unexpected(Error::absent);
  return detail::Api::view(&rep_->parts[index]->leaf);
}

const Description& Selection::description() const noexcept {
  assert(access_);
  access_->owner->domain->owner();
  return access_->owner->info->description;
}

Result<View> Selection::part(PartId id) const noexcept {
  if (!access_) return std::unexpected(Error::absent);
  access_->owner->domain->owner();
  const auto& lookup = access_->owner->lookup;
  if (!lookup.empty()) {
    const auto found = std::lower_bound(lookup.begin(), lookup.end(), id,
        [](const auto* leaf, PartId key) { return leaf->id < key; });
    if (found != lookup.end() && (*found)->id == id) return detail::Api::view(*found);
    return std::unexpected(Error::absent);
  }
  for (const auto* leaf : access_->parts)
    if (leaf->id == id) return detail::Api::view(leaf);
  return std::unexpected(Error::absent);
}

RetainedView retain(View view) noexcept {
  auto* part = detail::Api::part(view);
  if (!part) return {};
  part->domain->owner();
  return detail::Api::retained(Ref<detail::Part>::retain(*part));
}

RetainedSelection retain(Selection selection) noexcept {
  auto* version = detail::Api::version(selection);
  auto* rep = detail::Api::rep(selection);
  if (!version || !rep) return {};
  rep->domain->owner();
  return detail::Api::retained(Ref<detail::Version>::retain(*version), Ref<detail::Rep>::retain(*rep));
}

View RetainedView::view() const & noexcept {
  if (!part_) return {};
  part_->domain->owner();
  return detail::Api::view(&part_->leaf);
}

Selection RetainedSelection::selection() const & noexcept {
  if (!version_ || !rep_) return {};
  rep_->domain->owner();
  return detail::Api::selection(*version_, *rep_);
}

Result<Representation> Store::capture(const RepresentationSpec& spec,
    std::span<const PartSource> parts) const {
  if (!catalog_) return std::unexpected(Error::unavailable);
  auto& domain = *catalog_->domain;
  domain.owner();
  if (!valid_spec(spec) ||
      !std::all_of(parts.begin(), parts.end(), valid_source))
    return std::unexpected(Error::invalid_argument);
  try {
    if (!unique_parts(domain, parts)) return std::unexpected(Error::invalid_argument);
    auto info = detail::make<detail::Info>(domain, spec);
    auto rep = detail::make<detail::Rep>(domain, std::move(info));
    rep->parts.reserve(parts.size());
    rep->leaves.reserve(parts.size());
    for (const auto& source : parts) {
      auto part = detail::make<detail::Part>(domain, rep->info, source.id, source.interpretation);
      part->external = source.backing;
      part->leaf.body = source.body;
      part->leaf.readable_padding = source.readable_padding;
      part->leaf.alignment = source.alignment;
      rep->leaves.push_back(&part->leaf);
      rep->parts.push_back(std::move(part));
    }
    finish(*rep);
    return detail::Api::representation(std::move(rep));
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::capacity);
  } catch (const std::length_error&) {
    return std::unexpected(Error::capacity);
  }
}

Result<Builder> Store::build(const RepresentationSpec& spec, allocation::Pool& pool,
    std::span<const PartPlan> plans) const {
  if (!catalog_) return std::unexpected(Error::unavailable);
  auto& domain = *catalog_->domain;
  domain.owner();
  if (pool.lane != domain.lane || !valid_spec(spec))
    return std::unexpected(Error::invalid_argument);
  if (!std::all_of(plans.begin(), plans.end(), valid_plan))
    return std::unexpected(Error::invalid_argument);
  try {
    if (!unique_parts(domain, plans)) return std::unexpected(Error::invalid_argument);
    auto info = detail::make<detail::Info>(domain, spec);
    auto rep = detail::make<detail::Rep>(domain, std::move(info));
    rep->parts.reserve(plans.size());
    rep->leaves.reserve(plans.size());
    auto build = detail::make<detail::Build>(domain);
    build->active.resize(plans.size(), false);
    build->rep = std::move(rep);
    for (const auto& plan : plans) {
      auto part = detail::make<detail::Part>(domain, build->rep->info, plan.id, plan.interpretation);
      auto request = plan.allocation;
      if (request.bytes || request.readable_padding) {
        if (!request.bytes) {
          // The allocator owns the physical footprint; the Part still exposes
          // no logical body, and its whole footprint is initialized padding.
          request.bytes = request.readable_padding;
          request.readable_padding = 0;
          request.initialization = allocation::Initialization::zero;
        }
        auto storage = allocation::allocate(pool, request);
        if (!storage) return std::unexpected(storage.error());
        part->storage = std::move(*storage);
      }
      part->leaf.body = {part->storage.address, plan.allocation.bytes};
      part->leaf.readable_padding = plan.allocation.readable_padding;
      part->leaf.alignment = plan.allocation.alignment;
      build->rep->leaves.push_back(&part->leaf);
      build->rep->parts.push_back(std::move(part));
    }
    finish(*build->rep);
    return detail::Api::builder(std::move(build));
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::capacity);
  } catch (const std::length_error&) {
    return std::unexpected(Error::capacity);
  }
}

WriteView::WriteView(WriteView&& other) noexcept
    : build_(std::move(other.build_)), index_(std::exchange(other.index_, 0)) {
  if (build_) build_->domain->owner();
}

WriteView& WriteView::operator=(WriteView&& other) noexcept {
  if (this != &other) {
    close_writer(build_, index_);
    if (other.build_) other.build_->domain->owner();
    build_ = std::move(other.build_);
    index_ = std::exchange(other.index_, 0);
  }
  return *this;
}

WriteView::~WriteView() { close_writer(build_, index_); }

Result<MutableBytes> WriteView::bytes() const & noexcept {
  if (!build_) return std::unexpected(Error::stale);
  build_->domain->owner();
  if (build_->revoked || build_->frozen) return std::unexpected(Error::stale);
  assert(index_ < build_->active.size() && build_->active[index_]);
  const auto& part = *build_->rep->parts[index_];
  return MutableBytes{part.storage.address, part.leaf.body.size()};
}

Builder::Builder(Builder&& other) noexcept : build_(std::move(other.build_)) {
  if (build_) build_->domain->owner();
}

Builder& Builder::operator=(Builder&& other) noexcept {
  if (this != &other) {
    abandon(build_);
    if (other.build_) other.build_->domain->owner();
    build_ = std::move(other.build_);
  }
  return *this;
}

Builder::~Builder() { abandon(build_); }

Result<WriteView> Builder::write(std::size_t index) {
  if (!build_) return std::unexpected(Error::stale);
  build_->domain->owner();
  if (build_->revoked || build_->frozen) return std::unexpected(Error::stale);
  if (index >= build_->active.size()) return std::unexpected(Error::absent);
  if (build_->active[index]) return std::unexpected(Error::conflict);
  build_->active[index] = true;
  ++build_->writers;
  return detail::Api::writer(build_, index);
}

Result<Representation> Builder::freeze() {
  if (!build_) return std::unexpected(Error::stale);
  build_->domain->owner();
  if (build_->revoked || build_->frozen) return std::unexpected(Error::stale);
  if (build_->writers) return std::unexpected(Error::conflict);
  build_->frozen = true;
  auto result = detail::Api::representation(std::move(build_->rep));
  build_.reset();
  return result;
}

} // namespace orbital::object
