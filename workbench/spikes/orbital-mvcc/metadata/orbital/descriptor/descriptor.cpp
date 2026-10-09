#include "descriptor.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace compact_descriptor {
namespace {

struct Metadata {
  FrozenDescription description;
  std::pmr::memory_resource* resource;
  std::uint32_t references{1}, allocated_bytes;
  std::shared_ptr<const void> dependencies;
  Metadata(std::pmr::memory_resource& memory, std::uint32_t bytes,
      const RepresentationSpec& spec, std::uint32_t count)
      : resource(&memory), allocated_bytes(bytes), dependencies(spec.dependencies) {
    description.object = spec.object;
    description.version = spec.version;
    description.replica = spec.replica;
    description.format = spec.format;
    description.logical_bytes = static_cast<std::uint32_t>(spec.logical_metadata.size());
    description.interpretation_bytes = static_cast<std::uint32_t>(spec.interpretation.size());
    description.parts_offset = sizeof(Metadata);
    description.parts = count;
  }
};

std::byte* address(void* value) noexcept { return static_cast<std::byte*>(value); }
const std::byte* address(const void* value) noexcept { return static_cast<const std::byte*>(value); }

Metadata* metadata(const detail::Part* leaf) noexcept {
  auto* start = address(leaf) - leaf->metadata_distance;
  return static_cast<Metadata*>(const_cast<void*>(static_cast<const void*>(start)));
}

detail::Part* parts(Metadata* m) noexcept {
  return static_cast<detail::Part*>(static_cast<void*>(address(m) + m->description.parts_offset));
}

Description describe_metadata(Metadata* m) noexcept {
  const auto& d = m->description;
  return {d.object, d.version, d.replica, d.format, d.logical_metadata(), d.interpretation()};
}

void retain_metadata(Metadata* m) noexcept {
  assert(m->references != std::numeric_limits<std::uint32_t>::max());
  ++m->references;
}

void release_metadata(Metadata* m) noexcept {
  if (--m->references) return;
  auto* resource = m->resource;
  auto bytes = m->allocated_bytes;
  std::destroy_at(m);
  resource->deallocate(m, bytes, alignof(Metadata));
}

bool add(std::size_t& target, std::size_t extra) noexcept {
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  if (extra > maximum - target) return false;
  target += extra;
  return true;
}

bool extent(Bytes bytes, std::size_t padding = 0) noexcept {
  if (padding > std::numeric_limits<std::size_t>::max() - bytes.size()) return false;
  const auto size = bytes.size() + padding;
  return !size || (bytes.data() && size <= std::numeric_limits<std::uintptr_t>::max() - std::uintptr_t(bytes.data()));
}

bool validate(const RepresentationSpec& spec, std::span<const PartSource> sources) noexcept {
  if (!extent(spec.logical_metadata) || !extent(spec.interpretation)) return false;
  for (const auto& source : sources) {
    if (!std::has_single_bit(source.alignment) ||
        source.readable_padding > std::numeric_limits<std::uint32_t>::max() ||
        !extent(source.body, source.readable_padding) || !extent(source.interpretation))
      return false;
    if (source.body.size() || source.readable_padding) {
      if (!source.body.data() || !source.backing.use_count() ||
          std::uintptr_t(source.body.data()) % source.alignment) return false;
    }
  }
  return true;
}

bool unique(std::pmr::memory_resource& resource, std::span<const PartSource> sources) {
  if (sources.size() <= 8) {
    for (std::size_t i = 0; i < sources.size(); ++i)
      for (std::size_t j = 0; j < i; ++j)
        if (sources[i].id == sources[j].id) return false;
    return true;
  }
  std::pmr::vector<PartId> identities(&resource);
  identities.reserve(sources.size());
  for (const auto& source : sources) identities.push_back(source.id);
  std::sort(identities.begin(), identities.end());
  return std::adjacent_find(identities.begin(), identities.end()) == identities.end();
}

std::uint32_t copy(Metadata* m, std::size_t& cursor, Bytes source) noexcept {
  const auto offset = static_cast<std::uint32_t>(cursor);
  if (!source.empty()) std::memcpy(address(m) + cursor, source.data(), source.size());
  cursor += source.size();
  return offset;
}

} // namespace

struct Descriptor {
  Metadata* metadata;
  std::uint32_t references{1}, allocated_bytes;
};

struct Export {
  Metadata* metadata;
  const detail::Part* leaf;
  std::shared_ptr<const void> backing;
  std::uint32_t references{1};
};

static_assert(sizeof(Metadata) == 112 && sizeof(Descriptor) == 16 && sizeof(Export) == 40);
static_assert(sizeof(Metadata) % alignof(detail::Part) == 0);
static_assert(sizeof(Descriptor) % alignof(std::shared_ptr<const void>) == 0);

namespace {
std::shared_ptr<const void>* owners(Descriptor* d) noexcept {
  return static_cast<std::shared_ptr<const void>*>(static_cast<void*>(address(d) + sizeof(Descriptor)));
}
} // namespace

Result<Descriptor*> capture(std::pmr::memory_resource& resource,
    const RepresentationSpec& spec, std::span<const PartSource> sources) noexcept {
  if (!validate(spec, sources)) return std::unexpected(Error::invalid_argument);
  auto bytes = sizeof(Metadata);
  if (sources.size() > (std::numeric_limits<std::uint32_t>::max() - bytes) / sizeof(detail::Part) ||
      !add(bytes, sources.size() * sizeof(detail::Part)) ||
      !add(bytes, spec.logical_metadata.size()) || !add(bytes, spec.interpretation.size()))
    return std::unexpected(Error::capacity);
  for (const auto& source : sources)
    if (!add(bytes, source.interpretation.size())) return std::unexpected(Error::capacity);

  Metadata* m = nullptr;
  try {
    if (!unique(resource, sources)) return std::unexpected(Error::invalid_argument);
    auto* slab = resource.allocate(bytes, alignof(Metadata));
    m = std::construct_at(static_cast<Metadata*>(slab), resource,
        static_cast<std::uint32_t>(bytes), spec, static_cast<std::uint32_t>(sources.size()));

    auto cursor = sizeof(Metadata) + sources.size() * sizeof(detail::Part);
    m->description.logical_offset = copy(m, cursor, spec.logical_metadata);
    m->description.interpretation_offset = copy(m, cursor, spec.interpretation);
    for (std::size_t i = 0; i < sources.size(); ++i) {
      const auto& source = sources[i];
      std::construct_at(parts(m) + i, detail::Part{source.id, source.body.data(), source.body.size(),
          static_cast<std::uint32_t>(sizeof(Metadata) + i * sizeof(detail::Part)),
          copy(m, cursor, source.interpretation), static_cast<std::uint32_t>(source.interpretation.size()),
          static_cast<std::uint32_t>(source.readable_padding),
          static_cast<std::uint8_t>(std::countr_zero(source.alignment))});
    }
    assert(cursor == bytes);

    const auto owner_bytes = sizeof(Descriptor) + sources.size() * sizeof(std::shared_ptr<const void>);
    auto* d = std::construct_at(static_cast<Descriptor*>(resource.allocate(owner_bytes, alignof(Descriptor))),
        Descriptor{m, 1, static_cast<std::uint32_t>(owner_bytes)});
    for (std::size_t i = 0; i < sources.size(); ++i)
      std::construct_at(owners(d) + i, sources[i].backing);
    return d;
  } catch (const std::bad_alloc&) {
    if (m) release_metadata(m);
    return std::unexpected(Error::capacity);
  }
}

void retain(Descriptor* d) noexcept {
  if (!d) return;
  assert(d->references != std::numeric_limits<std::uint32_t>::max());
  ++d->references;
}

void release(Descriptor* d) noexcept {
  if (!d || --d->references) return;
  auto* m = d->metadata;
  auto* resource = m->resource;
  auto bytes = d->allocated_bytes;
  for (std::size_t i = 0; i < m->description.parts; ++i) std::destroy_at(owners(d) + i);
  std::destroy_at(d);
  resource->deallocate(d, bytes, alignof(Descriptor));
  release_metadata(m);
}

std::size_t Selection::part_count() const noexcept {
  return descriptor ? descriptor->metadata->description.parts : 0;
}

Result<View> Selection::part(std::size_t index) const noexcept {
  if (!descriptor || index >= descriptor->metadata->description.parts) return std::unexpected(Error::absent);
  return View{parts(descriptor->metadata) + index};
}

Result<View> Selection::part(PartId id) const noexcept {
  if (!descriptor) return std::unexpected(Error::absent);
  auto* m = descriptor->metadata;
  for (std::size_t i = 0; i < m->description.parts; ++i)
    if (parts(m)[i].id == id) return View{parts(m) + i};
  return std::unexpected(Error::absent);
}

Description describe(Descriptor* d) noexcept { return describe_metadata(d->metadata); }
Description describe(View v) noexcept { return describe_metadata(metadata(v.leaf)); }
const FrozenDescription& description(Selection selection) noexcept { return selection.descriptor->metadata->description; }
ObjectId object(Descriptor* d) noexcept { return d->metadata->description.object; }
std::uint64_t version(Descriptor* d) noexcept { return d->metadata->description.version; }

bool same_logical_state(Descriptor* a, Descriptor* b) noexcept {
  auto* x = a->metadata;
  auto* y = b->metadata;
  const auto& xd = x->description;
  const auto& yd = y->description;
  if (xd.object != yd.object || xd.version != yd.version || xd.logical_bytes != yd.logical_bytes) return false;
  return std::equal(address(x) + xd.logical_offset, address(x) + xd.logical_offset + xd.logical_bytes,
      address(y) + yd.logical_offset);
}

Result<Export*> retain_part(Selection selection, std::size_t index) noexcept {
  auto part = selection.part(index);
  if (!part) return std::unexpected(part.error());
  auto* d = selection.descriptor;
  auto* m = d->metadata;
  try {
    auto* result = std::construct_at(static_cast<Export*>(m->resource->allocate(sizeof(Export), alignof(Export))),
        Export{m, part->leaf, owners(d)[index], 1});
    retain_metadata(m);
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

void retain(Export* e) noexcept {
  if (!e) return;
  assert(e->references != std::numeric_limits<std::uint32_t>::max());
  ++e->references;
}

void release(Export* e) noexcept {
  if (!e || --e->references) return;
  auto* m = e->metadata;
  std::destroy_at(e);
  m->resource->deallocate(e, sizeof(Export), alignof(Export));
  release_metadata(m);
}

View view(Export* e) noexcept { return {e ? e->leaf : nullptr}; }

Footprint footprint(Descriptor* d) noexcept {
  auto* m = d->metadata;
  const auto directory = m->description.parts * sizeof(detail::Part);
  const auto copied_bytes = m->allocated_bytes - sizeof(Metadata) - directory;
  return {sizeof(Descriptor), m->description.parts * sizeof(std::shared_ptr<const void>), sizeof(Metadata),
      directory, copied_bytes, 0, m->allocated_bytes + d->allocated_bytes};
}

std::size_t export_bytes() noexcept { return sizeof(Export); }

} // namespace compact_descriptor
