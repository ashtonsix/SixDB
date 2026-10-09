#include "detail.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace selected::detail {

Store::Store() {
  identity = std::make_shared<StoreIdentity>();
  auto initial = std::make_shared<State>();
  initial->owner = identity;
  head = std::move(initial);
}

const Object* find(const State& state, Id id) noexcept {
  const auto item = std::find_if(state.objects.begin(), state.objects.end(),
      [id](const Object& object) { return object.key.object == id; });
  return item == state.objects.end() ? nullptr : &*item;
}

} // namespace selected::detail

namespace selected {

Id ReadContext::epoch() const noexcept {
  if (borrowed_owner_ && borrowed_owner_->get() != borrowed_state_) return 0;
  const auto* selected = state();
  return selected ? selected->epoch : 0;
}

Bytes ReadContext::logical_state(Id id) const noexcept {
  if (borrowed_owner_ && borrowed_owner_->get() != borrowed_state_) return {};
  const auto* selected = state();
  const auto* object = selected ? detail::find(*selected, id) : nullptr;
  return object ? Bytes(object->logical) : Bytes{};
}

std::shared_ptr<const detail::State> ReadContext::ownership() const {
  if (state_) return state_;
  if (borrowed_owner_ && borrowed_owner_->get() == borrowed_state_)
    return *borrowed_owner_;
  return {};
}

Result<Selection> bind(const ReadContext& context, Id id) {
  if (context.borrowed_owner_ && context.borrowed_owner_->get() != context.borrowed_state_)
    return std::unexpected(Error::stale);
  const auto* selected = context.state();
  if (!selected) return std::unexpected(Error::invalid);
  const auto* object = detail::find(*selected, id);
  if (!object) return std::unexpected(Error::absent);
  if (!object->rep || !object->rep->data || object->rep->data->key != object->key)
    return std::unexpected(Error::invalid);
  return Selection(object->rep.get(), nullptr);
}

Result<Selection> bind(const ReadContext& context, Id id, const Rep& alternative) {
  if (context.borrowed_owner_ && context.borrowed_owner_->get() != context.borrowed_state_)
    return std::unexpected(Error::stale);
  const auto* selected = context.state();
  if (!selected || !alternative || !alternative->data)
    return std::unexpected(Error::invalid);
  const auto* object = detail::find(*selected, id);
  if (!object) return std::unexpected(Error::absent);
  if (alternative->data->key != object->key)
    return std::unexpected(Error::invalid);
  return Selection(alternative.get(), &alternative);
}

Result<Retained> retain(const ReadContext& context, Selection selection) {
  if (context.borrowed_owner_ && context.borrowed_owner_->get() != context.borrowed_state_)
    return std::unexpected(Error::stale);
  const auto* selected = context.state();
  if (!selected || !selection.rep_ || !selection.rep_->data)
    return std::unexpected(Error::invalid);
  const auto* object = detail::find(*selected, selection.rep_->data->key.object);
  if (!object || selection.rep_->data->key != object->key)
    return std::unexpected(Error::invalid);

  Retained result;
  if (selection.owner_) {
    if (selection.owner_->get() != selection.rep_)
      return std::unexpected(Error::invalid);
    result.rep_ = *selection.owner_;
  } else {
    if (object->rep.get() != selection.rep_)
      return std::unexpected(Error::invalid);
    result.rep_ = object->rep;
  }
  return result;
}

Draft::Draft(const ReadContext& context) : impl_(std::make_unique<detail::Draft>()) {
  impl_->basis = context.ownership();
}

Draft::Draft(Draft&&) noexcept = default;
Draft& Draft::operator=(Draft&&) noexcept = default;
Draft::~Draft() = default;

Result<void> Draft::replace(Rep rep, Bytes logical) {
  if (!impl_ || !impl_->basis || !rep || !rep->data)
    return std::unexpected(Error::invalid);
  if (impl_->sealed) return std::unexpected(Error::busy);
  const auto key = rep->data->key;
  const auto* previous = detail::find(*impl_->basis, key.object);
  if (previous && key.version <= previous->key.version)
    return std::unexpected(Error::invalid);

  try {
    // Copy before touching the prior private change, including on allocation failure.
    detail::Object replacement{key, {logical.begin(), logical.end()}, std::move(rep)};
    const auto item = std::find_if(impl_->changes.begin(), impl_->changes.end(),
        [key](const detail::Object& object) { return object.key.object == key.object; });
    if (item == impl_->changes.end()) impl_->changes.push_back(std::move(replacement));
    else *item = std::move(replacement);
    return {};
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::capacity);
  } catch (const std::length_error&) {
    return std::unexpected(Error::capacity);
  }
}

Prepared::Prepared(std::unique_ptr<detail::Prepared> impl) : impl_(std::move(impl)) {}
Prepared::Prepared(Prepared&&) noexcept = default;
Prepared& Prepared::operator=(Prepared&&) noexcept = default;
Prepared::~Prepared() = default;

void Prepared::retire() noexcept {
  impl_.reset();
}

Store::Store() : impl_(std::make_unique<detail::Store>()) {}
Store::~Store() = default;

ReadContext Store::read() const noexcept {
  ReadContext result;
  result.state_ = impl_->head;
  return result;
}

ReadContext Store::borrow() const noexcept {
  ReadContext result;
  result.borrowed_state_ = impl_->head.get();
  result.borrowed_owner_ = &impl_->head;
  return result;
}

Result<void> Store::install_representation(const ReadContext& context, Rep rep) {
  if (context.borrowed_owner_ && context.borrowed_owner_->get() != context.borrowed_state_)
    return std::unexpected(Error::stale);
  const auto* selected = context.state();
  if (!selected || selected->owner != impl_->identity || !rep || !rep->data)
    return std::unexpected(Error::invalid);
  const auto* object = detail::find(*selected, rep->data->key.object);
  if (!object) return std::unexpected(Error::absent);
  if (object->key != rep->data->key) return std::unexpected(Error::invalid);
  if (object->rep->data->realization == rep->data->realization) {
    return object->rep.get() == rep.get() ? Result<void>{}
                                        : std::unexpected(Error::invalid);
  }
  for (const auto& installed : impl_->alternatives) {
    if (installed->data->key == rep->data->key &&
        installed->data->realization == rep->data->realization) {
      return installed.get() == rep.get() ? Result<void>{}
                                         : std::unexpected(Error::invalid);
    }
  }

  try {
    impl_->alternatives.push_back(std::move(rep));
    return {};
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::capacity);
  } catch (const std::length_error&) {
    return std::unexpected(Error::capacity);
  }
}

Result<Rep> Store::alternative(const ReadContext& context, Id id, Id realization) const {
  if (context.borrowed_owner_ && context.borrowed_owner_->get() != context.borrowed_state_)
    return std::unexpected(Error::stale);
  const auto* selected = context.state();
  if (!selected || selected->owner != impl_->identity) return std::unexpected(Error::invalid);
  const auto* object = detail::find(*selected, id);
  if (!object) return std::unexpected(Error::absent);
  for (const auto& rep : impl_->alternatives) {
    if (rep->data->key == object->key && rep->data->realization == realization)
      return rep;
  }
  return std::unexpected(Error::absent);
}

void Store::forget_alternatives() noexcept {
  impl_->alternatives.clear();
}

Result<Prepared> CommitOwner::prepare(Draft& draft, Id next_epoch) {
  if (!draft.impl_ || !draft.impl_->basis) return std::unexpected(Error::invalid);
  if (draft.impl_->sealed) return std::unexpected(Error::busy);
  if (store_.impl_->head.get() != draft.impl_->basis.get())
    return std::unexpected(Error::stale);
  if (next_epoch <= draft.impl_->basis->epoch) return std::unexpected(Error::invalid);

  try {
    auto next = std::make_shared<detail::State>(*draft.impl_->basis);
    next->epoch = next_epoch;
    for (const auto& change : draft.impl_->changes) {
      const auto item = std::find_if(next->objects.begin(), next->objects.end(),
          [&change](const detail::Object& object) {
            return object.key.object == change.key.object;
          });
      if (item == next->objects.end()) next->objects.push_back(change);
      else *item = change;
    }

    auto prepared = std::make_unique<detail::Prepared>();
    prepared->basis = draft.impl_->basis;
    prepared->next = std::move(next);
    draft.impl_->sealed = true;
    return Prepared(std::move(prepared));
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::capacity);
  } catch (const std::length_error&) {
    return std::unexpected(Error::capacity);
  }
}

Result<void> CommitOwner::install(Prepared& prepared) noexcept {
  if (!prepared.impl_ || !prepared.impl_->next)
    return std::unexpected(Error::invalid);
  if (prepared.impl_->installed) return std::unexpected(Error::busy);
  if (store_.impl_->head.get() != prepared.impl_->basis.get())
    return std::unexpected(Error::stale);

  // No ownership is dropped here. Old roots are released by explicit retirement.
  store_.impl_->head.swap(prepared.impl_->next);
  prepared.impl_->installed = true;
  return {};
}

} // namespace selected
