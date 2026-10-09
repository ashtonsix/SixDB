#include "detail.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace selected {
Bytes View::interpretation() const noexcept { return rep_ ? describe(*rep_).interpretation : Bytes{}; }

Result<View> ready(Selection selection, Id id, Requirements r) {
  const auto& rep = selection.representation();
  const auto* part = detail::find(rep, id);
  if (!part) return std::unexpected(Error::absent);
  if (!part->native) return std::unexpected(Error::unavailable);
  auto bytes = detail::qualify(rep, part->body(), part->padding(), r);
  if (!bytes) return std::unexpected(bytes.error());
  return View(*bytes, &rep);
}

Result<RetainedView> RetainedView::make(Retained selected, View view) {
  if (!selected || &selected.representation() != view.rep_) return std::unexpected(Error::invalid);
  RetainedView result;
  result.selection_ = std::move(selected);
  if (view.buffer_) result.buffer_ = *view.buffer_;
  result.view_ = View(view.bytes_, view.rep_, result.buffer_ ? &result.buffer_ : nullptr);
  return result;
}

RetainedView::RetainedView(RetainedView&& other) noexcept
    : selection_(std::move(other.selection_)), buffer_(std::move(other.buffer_)),
      view_(other.view_.bytes_, other.view_.rep_, buffer_ ? &buffer_ : nullptr) {
  other.view_ = View{};
}

RetainedView& RetainedView::operator=(RetainedView&& other) noexcept {
  if (this != &other) {
    selection_ = std::move(other.selection_); buffer_ = std::move(other.buffer_);
    view_ = View(other.view_.bytes_, other.view_.rep_, buffer_ ? &buffer_ : nullptr);
    other.view_ = View{};
  }
  return *this;
}

Slot::Slot() : impl_(std::make_shared<detail::Slot>()) {}
Slot::~Slot() { cancel(); }

Result<Request> Slot::submit(Retained selected, Id id, orbital::allocation::Pool& pool) {
  if (!selected) return std::unexpected(Error::invalid);
  const auto* source = detail::find(selected.representation(), id);
  if (!source || source->body().empty()) return std::unexpected(Error::invalid);
  try {
    auto target = std::make_shared<detail::Buffer>();
    auto storage = orbital::allocation::allocate(pool,
        {source->body().size(), 0, 64, source->padding(), orbital::allocation::Initialization::uninitialized});
    if (!storage) return std::unexpected(Error::capacity);
    target->bytes = std::move(*storage);
    auto request = std::make_shared<detail::Transfer>();
    request->slot = impl_; request->selection = std::move(selected);
    request->source = source; request->target = std::move(target);
    // All fallible preparation precedes replacing waiter interest.
    cancel(); request->generation = impl_->generation;
    ++impl_->outstanding;
    return request;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

void Slot::cancel() noexcept {
  ++impl_->generation;
  impl_->buffer.reset(); impl_->selection = {};
}

std::size_t Slot::outstanding() const noexcept { return impl_->outstanding; }

Result<View> Slot::ready(Requirements r) const {
  if (!impl_->buffer) return std::unexpected(Error::unavailable);
  const auto& rep = impl_->selection.representation();
  auto bytes = detail::qualify(rep, orbital::allocation::bytes(impl_->buffer->bytes),
      impl_->buffer->bytes.readable_padding, r);
  if (!bytes) return std::unexpected(bytes.error());
  return View(*bytes, &rep, &impl_->buffer);
}

Result<void> finish(const Request& request, bool fail, std::size_t partial) {
  if (!request || request->completed || request->retired) return std::unexpected(Error::stale);
  const auto source = request->source->body();
  const auto count = fail ? std::min(partial, source.size()) : source.size();
  if (count) std::memcpy(request->target->bytes.address, source.data(), count);
  request->completed = true; request->success = !fail;
  return fail ? Result<void>{std::unexpected(Error::io)} : Result<void>{};
}

bool settle(const Request& request) noexcept {
  if (!request || !request->completed || request->settled || request->retired) return false;
  if (auto slot = request->slot.lock(); slot && slot->generation == request->generation && request->success) {
    slot->selection = request->selection;
    slot->buffer = request->target;
  }
  request->settled = true;
  return true;
}

bool retire(const Request& request) noexcept {
  if (!request || !request->completed || !request->settled || request->retired) return false;
  if (auto slot = request->slot.lock()) --slot->outstanding;
  request->source = nullptr; request->selection = {}; request->target.reset();
  request->retired = true;
  return true;
}

bool physically_retired(const Request& request) noexcept { return request && request->retired; }
} // namespace selected
