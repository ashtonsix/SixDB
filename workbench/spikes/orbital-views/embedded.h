#pragma once
#include "prototype.h"
#include <map>
#include <optional>
#include <string>

// A small composition fixture, not a journal format or selected module API.
namespace embedded {
using Id = std::uint64_t;
using views::Bytes;
using views::Epoch;
enum class Error { pending, io, missing, exists, busy, invalid, view };
template<class T> using Result = std::expected<T, Error>;
enum class Kind : std::uint8_t { create, update, erase };
struct Ticket {
  enum class Status { pending, complete, failed } status{Status::pending};
  Bytes bytes;
};

class Port {
 public:
  virtual ~Port() = default;
  virtual std::shared_ptr<Ticket> append(Bytes bytes) = 0;
  virtual Bytes read() = 0;
  virtual void truncate(std::size_t bytes) = 0;
};

// Completion is driven explicitly so the same history can exercise delayed I/O.
class FilePort final : public Port {
 public:
  explicit FilePort(const std::string& path);
  ~FilePort();
  std::shared_ptr<Ticket> append(Bytes bytes) override;
  Bytes read() override;
  void truncate(std::size_t bytes) override;
  void complete(const std::shared_ptr<Ticket>& ticket);
  void drain();
  void short_write(const std::shared_ptr<Ticket>& ticket, std::size_t bytes);
  void lose_ack(const std::shared_ptr<Ticket>& ticket);
 private:
  int fd_;
  std::vector<std::shared_ptr<Ticket>> queue_;
  void write(const std::shared_ptr<Ticket>& ticket, std::size_t bytes, bool acknowledge);
};

struct Snapshot {
  Epoch epoch{};
  bool present{};
  std::map<Id, std::shared_ptr<views::Part>> parts;
  std::map<Id, Epoch> sources;
};
class Mutation;
class History {
 public:
  explicit History(std::shared_ptr<Port> port, std::size_t reserve_bytes,
      std::shared_ptr<recycling::Pool> pool = {});
  Result<std::unique_ptr<Mutation>> intend(Id object, Kind kind);
  Result<views::Lease> open(Id object, Epoch epoch, Id part,
      std::span<const std::size_t> pages, views::Method method = views::Method::cow);
  Result<std::shared_ptr<views::Fetch>> fetch(Id object, Epoch epoch, Id part,
      std::span<const std::size_t> pages);
  Result<std::vector<Id>> layout(Id object, Epoch epoch) const;
  Result<Epoch> source(Id object, Epoch epoch, Id part) const;
  Epoch frontier() const { return frontier_; }
  bool needs_recovery() const { return uncertain_; }
 private:
  friend class Mutation;
  std::shared_ptr<Port> port_;
  std::shared_ptr<recycling::Pool> pool_;
  std::size_t reserve_;
  Epoch frontier_{}, next_epoch_{1};
  Id next_intent_{1};
  bool busy_{}, uncertain_{};
  using Revisions = std::map<Epoch, std::shared_ptr<Snapshot>>;
  std::map<Id, Revisions> objects_;
  std::shared_ptr<Snapshot> at(Id object, Epoch epoch) const;
  void recover();
};

class Mutation {
 public:
  ~Mutation();
  Mutation(const Mutation&) = delete;
  Result<void> start();
  Result<views::Draft*> edit(Id part, views::Method method,
      std::span<const std::size_t> pages);
  Result<views::Draft*> add(Id part);
  Result<void> remove(Id part);
  Result<std::shared_ptr<Ticket>> stage(Id part, std::span<const views::Range> ranges);
  Result<void> prepare();
  Result<void> commit();
  Epoch epoch() const { return epoch_; }
  std::shared_ptr<Ticket> intent() const { return intent_; }
  std::shared_ptr<Ticket> decision() const { return decision_; }
 private:
  friend class History;
  Mutation(History& owner, Id object, Kind kind, Id intent, Epoch epoch);
  History& owner_;
  Id object_, id_;
  Kind kind_;
  Epoch epoch_;
  bool active_{}, prepared_{}, finished_{}, failed_{};
  std::shared_ptr<Ticket> intent_, decision_;
  std::shared_ptr<Snapshot> candidate_;
  std::map<Id, std::unique_ptr<views::Draft>> drafts_;
  std::map<Id, views::Prepared> prepared_parts_;
  std::vector<std::pair<std::shared_ptr<Ticket>, std::shared_ptr<views::Delivery>>> stages_;
  History::Revisions::node_type head_;
  Bytes decision_bytes_;
};
}
