#include "detail.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace selected {
namespace {
bool power_of_two(std::size_t n) { return n && !(n & (n - 1)); }
bool valid(Key key, Id realization, Id format) { return key.object && key.version && realization && format; }
}

Description describe(const Representation& rep) noexcept {
  const auto& d = *rep.data;
  return {d.key, d.realization, d.format, d.parts.size(), d.interpretation};
}

Result<Bytes> part_interpretation(const Representation& rep, Id id) {
  auto* part = detail::find(rep, id);
  if (!part) return std::unexpected(Error::absent);
  return part->interpretation;
}

Builder::Builder(std::shared_ptr<detail::Builder> value) : impl_(std::move(value)) {}
Builder::Builder(Builder&&) noexcept = default;
Builder& Builder::operator=(Builder&&) noexcept = default;
Builder::~Builder() = default;

Result<Builder> Builder::make(orbital::allocation::Pool& pool, Key key, Id realization,
    Id format, Bytes interpretation, std::span<const PartInput> inputs) {
  if (!valid(key, realization, format)) return std::unexpected(Error::invalid);
  try {
    auto d = std::make_shared<detail::Builder>();
    d->key = key; d->realization = realization; d->format = format;
    d->interpretation.assign(interpretation.begin(), interpretation.end());
    d->parts.reserve(inputs.size());
    for (const auto& input : inputs) {
      if (!power_of_two(input.alignment) || std::ranges::any_of(d->parts,
          [&](const auto& p) { return p.id == input.id; })) return std::unexpected(Error::invalid);
      detail::Part p;
      p.id = input.id; p.native = input.native;
      if (!input.bytes.empty()) {
        auto allocation = orbital::allocation::allocate(pool,
            {input.bytes.size(), 0, input.alignment, input.padding,
             orbital::allocation::Initialization::uninitialized});
        if (!allocation) return std::unexpected(Error::capacity);
        p.bytes = std::move(*allocation);
        std::memcpy(p.bytes.address, input.bytes.data(), input.bytes.size());
      }
      d->parts.push_back(std::move(p));
    }
    return Builder(std::move(d));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

WriteView::WriteView(std::shared_ptr<detail::Builder> owner, MutableBytes bytes)
    : owner_(std::move(owner)), bytes_(bytes) { ++owner_->writers; }
WriteView::WriteView(WriteView&& other) noexcept
    : owner_(std::move(other.owner_)), bytes_(std::exchange(other.bytes_, {})) {}
WriteView::~WriteView() { if (owner_) --owner_->writers; }

Result<WriteView> Builder::write(Id id) {
  if (!impl_) return std::unexpected(Error::stale);
  for (auto& p : impl_->parts) if (p.id == id) return WriteView(impl_, orbital::allocation::bytes(p.bytes));
  return std::unexpected(Error::absent);
}

Result<Rep> Builder::freeze() {
  if (!impl_) return std::unexpected(Error::stale);
  if (impl_->writers) return std::unexpected(Error::busy);
  try {
    Rep rep = std::make_shared<Representation>(impl_);
    impl_.reset();
    return rep;
  }
  catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

Result<Rep> capture(Key key, Id realization, Id format, Bytes interpretation,
    std::span<const ExternalPart> inputs, std::shared_ptr<const void> closure) {
  if (!valid(key, realization, format) || !closure) return std::unexpected(Error::invalid);
  try {
    auto d = std::make_shared<detail::Builder>();
    d->key = key; d->realization = realization; d->format = format;
    d->interpretation.assign(interpretation.begin(), interpretation.end());
    d->external_owner = std::move(closure);
    d->parts.reserve(inputs.size());
    for (const auto& input : inputs) {
      if (!power_of_two(input.alignment) ||
          input.padding > std::numeric_limits<std::size_t>::max() - input.bytes.size() ||
          (input.bytes.empty() && input.padding) ||
          (!input.bytes.empty() && reinterpret_cast<std::uintptr_t>(input.bytes.data()) % input.alignment) ||
          std::ranges::any_of(d->parts, [&](const auto& p) { return p.id == input.id; }))
        return std::unexpected(Error::invalid);
      detail::Part p;
      p.id = input.id; p.native = input.native; p.external = input.bytes;
      p.interpretation = input.interpretation;
      p.external_alignment = input.alignment; p.external_padding = input.padding;
      d->parts.push_back(std::move(p));
    }
    return std::make_shared<Representation>(std::move(d));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

namespace detail {
const Part* find(const Representation& rep, Id id) noexcept {
  for (const auto& p : rep.data->parts) if (p.id == id) return &p;
  return nullptr;
}

Result<Bytes> qualify(const Representation& rep, Bytes bytes, std::size_t padding, Requirements r) {
  if (r.format != rep.data->format || !power_of_two(r.alignment) ||
      r.offset > bytes.size() || r.size > bytes.size() - r.offset)
    return std::unexpected(Error::invalid);
  const auto remaining = bytes.size() - r.offset - r.size;
  if (r.padding > remaining && r.padding - remaining > padding) return std::unexpected(Error::invalid);
  if (bytes.empty()) {
    if (r.padding) return std::unexpected(Error::invalid);
    return Bytes{};
  }
  const auto* address = bytes.data() + r.offset;
  if (reinterpret_cast<std::uintptr_t>(address) % r.alignment) return std::unexpected(Error::invalid);
  return Bytes{address, r.size};
}
} // namespace detail
} // namespace selected
