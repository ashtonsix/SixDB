#pragma once

// Experiment-local selected access. No runtime or container API is established.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

namespace orbital::allocation { struct Pool; }

namespace selected {
using Bytes = std::span<const std::byte>;
using MutableBytes = std::span<std::byte>;
using Id = std::uint64_t;
enum class Error { absent, invalid, stale, busy, capacity, unavailable, io };
template<class T> using Result = std::expected<T, Error>;
struct Key { Id object{}, version{}; auto operator<=>(const Key&) const = default; };
struct Representation;
using Rep = std::shared_ptr<const Representation>;
namespace detail { struct State; struct Builder; struct Store; struct Draft; struct Prepared; struct Slot; struct Transfer; struct Buffer; }

struct PartInput { Id id{}; Bytes bytes; bool native{true}; std::size_t alignment{64}, padding{32}; };
struct ExternalPart { Id id{}; Bytes bytes, interpretation; bool native{true}; std::size_t alignment{1}, padding{}; };
struct Requirements { Id format{}; std::size_t offset{}, size{}, alignment{1}, padding{}; };
struct Description { Key key; Id realization{}, format{}; std::size_t parts{}; Bytes interpretation; };
Description describe(const Representation&) noexcept;
// Copies descriptors, not pack bytes; closure owns every borrowed byte/metadata span.
Result<Rep> capture(Key, Id realization, Id format, Bytes interpretation,
    std::span<const ExternalPart>, std::shared_ptr<const void> closure);
Result<Bytes> part_interpretation(const Representation&, Id part);

class WriteView {
public:
  WriteView(WriteView&&) noexcept;
  WriteView& operator=(WriteView&&) = delete;
  ~WriteView();
  MutableBytes bytes() const noexcept { return bytes_; }
private:
  WriteView(std::shared_ptr<detail::Builder>, MutableBytes);
  std::shared_ptr<detail::Builder> owner_;
  MutableBytes bytes_;
  friend class Builder;
};

class Builder {
public:
  Builder(Builder&&) noexcept;
  Builder& operator=(Builder&&) noexcept;
  ~Builder();
  static Result<Builder> make(orbital::allocation::Pool&, Key, Id realization,
      Id format, Bytes interpretation, std::span<const PartInput>);
  Result<WriteView> write(Id part);
  // Busy while a writable scope is open. Failure preserves the private builder.
  Result<Rep> freeze();
private:
  explicit Builder(std::shared_ptr<detail::Builder>);
  std::shared_ptr<detail::Builder> impl_;
};

class ReadContext {
public:
  Id epoch() const noexcept;
  Bytes logical_state(Id object) const noexcept;
private:
  std::shared_ptr<const detail::State> state_;
  const detail::State* borrowed_state_{};
  const std::shared_ptr<const detail::State>* borrowed_owner_{};
  const detail::State* state() const noexcept { return state_ ? state_.get() : borrowed_state_; }
  std::shared_ptr<const detail::State> ownership() const;
  friend class Store;
  friend class Draft;
  friend class Retained;
  friend Result<class Selection> bind(const ReadContext&, Id);
  friend Result<class Selection> bind(const ReadContext&, Id, const Rep&);
  friend Result<class Retained> retain(const ReadContext&, class Selection);
};

class Selection {
public:
  const Representation& representation() const noexcept { return *rep_; }
private:
  const Representation* rep_{};
  const Rep* owner_{}; // Used only for explicit escape; ordinary use borrows.
  Selection(const Representation* rep, const Rep* owner) : rep_(rep), owner_(owner) {}
  friend Result<Selection> bind(const ReadContext&, Id);
  friend Result<Selection> bind(const ReadContext&, Id, const Rep&);
  friend Result<class Retained> retain(const ReadContext&, Selection);
};

// Default borrows ReadContext; alternative also borrows the supplied Rep handle.
Result<Selection> bind(const ReadContext&, Id object);
Result<Selection> bind(const ReadContext&, Id object, const Rep& alternative);
Result<Selection> bind(ReadContext&&, Id) = delete;
Result<Selection> bind(const ReadContext&&, Id) = delete;
Result<Selection> bind(ReadContext&&, Id, const Rep&) = delete;
Result<Selection> bind(const ReadContext&&, Id, const Rep&) = delete;
Result<Selection> bind(const ReadContext&, Id, Rep&&) = delete;
Result<Selection> bind(const ReadContext&, Id, const Rep&&) = delete;

class Retained {
public:
  const Representation& representation() const noexcept { return *rep_; }
  explicit operator bool() const noexcept { return bool(rep_); }
private:
  Rep rep_;
  friend Result<Retained> retain(const ReadContext&, Selection);
  friend class RetainedView;
  friend class Slot;
  friend struct detail::Transfer;
};
Result<Retained> retain(const ReadContext&, Selection);

class View {
public:
  // Borrows its qualified byte/interpretation owner; not a standalone lifetime.
  Bytes bytes() const noexcept { return bytes_; }
  Bytes interpretation() const noexcept;
private:
  Bytes bytes_;
  const Representation* rep_{};
  const std::shared_ptr<detail::Buffer>* buffer_{};
  View(Bytes bytes, const Representation* rep,
      const std::shared_ptr<detail::Buffer>* buffer = nullptr)
      : bytes_(bytes), rep_(rep), buffer_(buffer) {}
  View() = default;
  friend Result<View> ready(Selection, Id, Requirements);
  friend class Slot;
  friend class RetainedView;
};

// No allocation, registration, independent retain or backend dispatch.
Result<View> ready(Selection, Id part, Requirements);

class RetainedView {
public:
  RetainedView(RetainedView&&) noexcept;
  RetainedView& operator=(RetainedView&&) noexcept;
  RetainedView(const RetainedView&) = delete;
  RetainedView& operator=(const RetainedView&) = delete;
  // Promote before the borrowed View owner changes or ends.
  static Result<RetainedView> make(Retained, View);
  const View& view() const noexcept { return view_; }
private:
  RetainedView() = default;
  Retained selection_;
  std::shared_ptr<detail::Buffer> buffer_;
  View view_;
};

class Draft {
public:
  explicit Draft(const ReadContext&);
  Draft(Draft&&) noexcept;
  Draft& operator=(Draft&&) noexcept;
  ~Draft();
  Result<void> replace(Rep, Bytes logical_state);
private:
  std::unique_ptr<detail::Draft> impl_;
  friend class CommitOwner;
};

class Prepared {
public:
  Prepared(Prepared&&) noexcept;
  Prepared& operator=(Prepared&&) noexcept;
  ~Prepared();
  void retire() noexcept;
private:
  explicit Prepared(std::unique_ptr<detail::Prepared>);
  std::unique_ptr<detail::Prepared> impl_;
  friend class CommitOwner;
};

// Temporary single-owner catalog; its map geometry is not an Engine proposal.
class Store {
public:
  Store();
  ~Store();
  ReadContext read() const noexcept;
  // No ownership increment; ends before install or Store destruction.
  ReadContext borrow() const noexcept;
  // Caller owns the equivalence law. Checks exact retained version, not latest.
  Result<void> install_representation(const ReadContext&, Rep);
  Result<Rep> alternative(const ReadContext&, Id object, Id realization) const;
  void forget_alternatives() noexcept;
private:
  std::unique_ptr<detail::Store> impl_;
  friend class CommitOwner;
};

class CommitOwner {
public:
  explicit CommitOwner(Store& store) : store_(store) {}
  Result<Prepared> prepare(Draft&, Id next_epoch);
  // Exact-basis refusal or pointer swap; prior owners remain in Prepared.
  Result<void> install(Prepared&) noexcept;
private:
  Store& store_;
};

using Request = std::shared_ptr<detail::Transfer>;
class Slot {
public:
  Slot();
  ~Slot();
  Slot(const Slot&) = delete;
  Slot& operator=(const Slot&) = delete;
  Result<Request> submit(Retained, Id part, orbital::allocation::Pool&);
  // Borrows Slot's ready owners; ends before submit/cancel/destruction.
  // Promote while valid if native use must survive one of those operations.
  Result<View> ready(Requirements) const;
  void cancel() noexcept;
  std::size_t outstanding() const noexcept;
private:
  std::shared_ptr<detail::Slot> impl_;
};

// Deterministic memory backend: completion and owner receipt are distinct steps.
// The harness/backend retains each accepted Request through retire, independently
// of the Slot. This is not a Port registry surviving arbitrary backend loss.
Result<void> finish(const Request&, bool fail = false, std::size_t partial = 0);
bool settle(const Request&) noexcept;
bool retire(const Request&) noexcept;
bool physically_retired(const Request&) noexcept;
} // namespace selected
