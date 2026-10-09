#pragma once

// Completed embedded state: coherent logical selection, immutable physical
// descriptions and private writes. Catalog operations and ownership stay on
// one adopted Lane. Native byte use needs no scheduler entry or extra Hold.

#include <orbital/allocation.h>
#include <memory>

namespace orbital::object {

struct Description {
  ObjectId object{};
  std::uint64_t version{};
  ReplicaId replica{};
  ProgramId format{};
  Bytes logical_metadata;
  Bytes interpretation;
};

struct RepresentationSpec {
  ObjectId object{};
  std::uint64_t version{};
  ReplicaId replica{};
  ProgramId format{};
  Bytes logical_metadata;
  Bytes interpretation;
  std::shared_ptr<const void> dependencies;
};

/// The supplied owner guarantees immutable body, padding and dependencies.
/// Orbital copies descriptions, not body bytes. An umbrella owner retains its
/// whole group even when only one Part escapes.
struct PartSource {
  PartId id{};
  Bytes body;
  Bytes interpretation;
  std::size_t readable_padding{};
  std::size_t alignment{1};
  std::shared_ptr<const void> backing;
};

struct PartPlan {
  PartId id{};
  allocation::AllocationRequest allocation;
  Bytes interpretation;
};

namespace detail {
struct Catalog;
struct Snapshot;
struct Version;
struct Rep;
struct Part;
struct Build;
struct Edit;
struct Acceptance;
struct Leaf {
  const Description* description{};
  PartId id{};
  Bytes body;
  Bytes interpretation;
  std::size_t readable_padding{}, alignment{1};
  Part* owner{};
};
struct Access { std::span<const Leaf* const> parts; Rep* owner{}; };
void retain_ref(Catalog&) noexcept;
void release_ref(Catalog&) noexcept;
void retain_ref(Snapshot&) noexcept;
void release_ref(Snapshot&) noexcept;
void retain_ref(Version&) noexcept;
void release_ref(Version&) noexcept;
void retain_ref(Rep&) noexcept;
void release_ref(Rep&) noexcept;
void retain_ref(Part&) noexcept;
void release_ref(Part&) noexcept;
void retain_ref(Build&) noexcept;
void release_ref(Build&) noexcept;
void retain_ref(Edit&) noexcept;
void release_ref(Edit&) noexcept;
void retain_ref(Acceptance&) noexcept;
void release_ref(Acceptance&) noexcept;
struct Api;
} // namespace detail

/// Borrowed immutable Part. Its selection or retained owner must remain live.
class View {
public:
  View() = default;
  explicit operator bool() const noexcept { return leaf_; }
  Bytes body() const noexcept { return leaf_->body; }
  Bytes interpretation() const noexcept { return leaf_->interpretation; }
  const Description& description() const noexcept { return *leaf_->description; }
  PartId id() const noexcept { return leaf_->id; }
  std::size_t readable_padding() const noexcept { return leaf_->readable_padding; }
  std::size_t alignment() const noexcept { return leaf_->alignment; }
private:
  explicit View(const detail::Leaf* leaf) : leaf_(leaf) {}
  const detail::Leaf* leaf_{};
  friend class Selection;
  friend struct detail::Api;
};

class Representation {
public:
  Representation() = default;
  explicit operator bool() const noexcept { return bool(rep_); }
  const Description& description() const noexcept;
  std::size_t part_count() const noexcept;
  [[nodiscard]] Result<View> part(std::size_t) const & noexcept;
  Result<View> part(std::size_t) const && = delete;
private:
  Ref<detail::Rep> rep_;
  friend struct detail::Api;
};

/// The descriptor is fixed at selection. Conversion cannot retarget this access.
class Selection {
public:
  Selection() = default;
  explicit operator bool() const noexcept { return access_; }
  const Description& description() const noexcept;
  std::size_t part_count() const noexcept { return access_->parts.size(); }
  [[nodiscard]] Result<View> part(std::size_t index) const noexcept {
    if (!access_ || index >= access_->parts.size()) return std::unexpected(Error::absent);
    return View(access_->parts[index]);
  }
  [[nodiscard]] Result<View> part(PartId) const noexcept;
private:
  detail::Version* version_{};
  const detail::Access* access_{};
  friend struct detail::Api;
};

class RetainedView {
public:
  RetainedView() = default;
  View view() const & noexcept;
  View view() const && = delete;
  explicit operator bool() const noexcept { return bool(part_); }
private:
  Ref<detail::Part> part_;
  friend struct detail::Api;
};

class RetainedSelection {
public:
  RetainedSelection() = default;
  Selection selection() const & noexcept;
  Selection selection() const && = delete;
  explicit operator bool() const noexcept { return bool(version_); }
private:
  Ref<detail::Version> version_;
  Ref<detail::Rep> rep_;
  friend struct detail::Api;
};

[[nodiscard]] RetainedView retain(View) noexcept;
[[nodiscard]] RetainedSelection retain(Selection) noexcept;

class BorrowedContext {
public:
  BorrowedContext() = default;
private:
  detail::Catalog* catalog_{};
  std::uint64_t cut_{};
  friend struct detail::Api;
};

/// Keeps future discovery coherent across publications and waits, including
/// Objects not looked up yet. Close it when only resolved retention is needed.
class ReadContext {
public:
  ReadContext() = default;
private:
  Ref<detail::Snapshot> snapshot_;
  friend struct detail::Api;
};

[[nodiscard]] Result<Selection> select(const BorrowedContext&, ObjectId, bool alternative = false) noexcept;
Result<Selection> select(BorrowedContext&&, ObjectId, bool = false) = delete;
[[nodiscard]] Result<Selection> select(const ReadContext&, ObjectId, bool alternative = false) noexcept;
Result<Selection> select(ReadContext&&, ObjectId, bool = false) = delete;

class WriteView {
public:
  WriteView() = default;
  WriteView(WriteView&&) noexcept;
  WriteView& operator=(WriteView&&) noexcept;
  ~WriteView();
  WriteView(const WriteView&) = delete;
  WriteView& operator=(const WriteView&) = delete;
  /// Borrowed writable span; all uses end before writer close or Builder
  /// revocation. Cached raw spans cannot be mechanically revoked.
  [[nodiscard]] Result<MutableBytes> bytes() const & noexcept;
  Result<MutableBytes> bytes() const && = delete;
private:
  Ref<detail::Build> build_;
  std::size_t index_{};
  friend struct detail::Api;
};

class Builder {
public:
  Builder() = default;
  Builder(Builder&&) noexcept;
  Builder& operator=(Builder&&) noexcept;
  ~Builder();
  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;
  [[nodiscard]] Result<WriteView> write(std::size_t);
  /// Caller has finished initialization/format validation; no writer may be open.
  [[nodiscard]] Result<Representation> freeze();
private:
  Ref<detail::Build> build_;
  friend struct detail::Api;
};

class Draft {
public:
  Draft() = default;
  Draft(Draft&&) noexcept = default;
  Draft& operator=(Draft&&) noexcept = default;
  Draft(const Draft&) = delete;
  Draft& operator=(const Draft&) = delete;
  std::uint64_t version() const noexcept;
  [[nodiscard]] Result<void> put(Representation);
  [[nodiscard]] Result<void> erase(ObjectId);
  /// Staged borrows end before replacement/erase of that Object or Draft
  /// acceptance/destruction. Retain explicitly to escape private work.
  [[nodiscard]] Result<Selection> select(ObjectId) const & noexcept;
  Result<Selection> select(ObjectId) const && = delete;
private:
  Ref<detail::Edit> edit_;
  friend struct detail::Api;
};

enum class Disposition { empty, accepted, installed, aborted };

/// prepare accepts a finished immutable result. Abort, including destruction of
/// an uninstalled Prepared, terminates it with no logical effect. It promises
/// no later retry/outcome. Only one accepted result is pending per Store.
class Prepared {
public:
  Prepared() = default;
  Prepared(Prepared&&) noexcept;
  Prepared& operator=(Prepared&&) noexcept;
  ~Prepared();
  Prepared(const Prepared&) = delete;
  Prepared& operator=(const Prepared&) = delete;
  Disposition disposition() const noexcept;
  void abort() noexcept;
  void retire() noexcept;
private:
  Ref<detail::Acceptance> acceptance_;
  friend struct detail::Api;
};

struct Statistics {
  std::size_t metadata_bytes{}, metadata_peak{}, metadata_limit{};
  std::size_t objects{}, versions{}, contexts{}, retirement_records{};
  std::uint64_t completed_cut{}, oldest_cut{}, accepted{}, installed{}, aborted{};
};

class Store {
public:
  Store() = default;
  Store(Store&&) noexcept = default;
  Store& operator=(Store&&) noexcept = default;
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  [[nodiscard]] static Result<Store> open(worker::Lane&, std::size_t metadata_limit);
  explicit operator bool() const noexcept { return bool(catalog_); }
  /// Borrow ends before logical publication, Store destruction or suspension.
  /// An alternative borrow also ends before that alternative is replaced.
  BorrowedContext borrow() const & noexcept;
  BorrowedContext borrow() const && = delete;
  [[nodiscard]] Result<ReadContext> read() const;
  [[nodiscard]] Result<Draft> edit() const;
  [[nodiscard]] Result<Representation> capture(const RepresentationSpec&, std::span<const PartSource>) const;
  [[nodiscard]] Result<Builder> build(const RepresentationSpec&, allocation::Pool&, std::span<const PartPlan>) const;
  /// Refusal leaves the private Draft usable. Success consumes it and issues
  /// Store-bound single-use authority; callers cannot supply a commit position.
  [[nodiscard]] Result<Prepared> prepare(Draft&);
  [[nodiscard]] Result<void> install(Prepared&) noexcept;
  /// Caller establishes semantic equivalence; Orbital verifies exact binding
  /// and logical metadata. Physical adoption does not advance the logical cut.
  [[nodiscard]] Result<void> adopt(Selection, Representation);
  Statistics statistics() const noexcept;
  /// Budget counts examined history entries plus retired records, not time.
  /// Opaque owner callbacks may reenter on this Lane or release the Store handle;
  /// their work, including nested drain calls, is outside this call's budget.
  std::size_t drain(std::size_t budget) noexcept;
private:
  Ref<detail::Catalog> catalog_;
  friend struct detail::Api;
};

} // namespace orbital::object
