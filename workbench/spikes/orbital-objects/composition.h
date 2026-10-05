#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

// Private experiment interfaces, not Orbital production types.
namespace object_views {
using Bytes = std::vector<std::uint8_t>;
struct Range { std::size_t offset{}, size{}; };
enum class Strategy { copy, share, reuse };
const char* name(Strategy strategy);

struct Counts {
  std::uint64_t charged{}, peak{}, maps{}, unmaps{}, copies{}, fetched{},
      reconstructed{}, grants{}, active{}, peak_active{}, protections{},
      reused{}, replacements{}, stale{}, denied{}, aliases{}, discarded{},
      unreleased{}, undo{}, zeroed{};
  std::uint64_t cached{}, peak_home{};
};

class Pool {
 public:
  struct State;
  explicit Pool(std::size_t limit, std::size_t service_reserve = 0,
                std::size_t cache_pages = 0);
  std::size_t page() const;
  Counts counts() const;
  void reset_counts();
  struct Home;
  std::shared_ptr<Home> allocate(bool service = false);
 private:
  friend class Context;
  friend class Projection;
  friend class Working;
  explicit Pool(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};

class ByteUse {
 public:
  struct Hold;
  ByteUse() = default;
  const std::uint8_t* data() const;
  std::size_t size() const;
  std::uint64_t home() const;
  explicit operator bool() const { return static_cast<bool>(hold_); }
  /// Copies share one physical-use record; completion is its final release.
  void complete() { hold_.reset(); }
 private:
  friend class Context;
  friend class Projection;
  explicit ByteUse(std::shared_ptr<Hold> hold) : hold_(std::move(hold)) {}
  std::shared_ptr<Hold> hold_;
};

class History {
 public:
  /// Preseeded file-backed logical outcomes; no transaction/journal implementation.
  explicit History(std::vector<Bytes> outcomes);
  ~History();
  std::size_t size() const;
  std::size_t retained_bytes() const;
  std::size_t positions() const { return positions_; }
  /// Source-delay/error injection; install only while fixture readers are idle.
  void source_hook(std::function<void(std::size_t, Range)> hook) { hook_ = std::move(hook); }
  void read(std::size_t position, Range range, std::uint8_t* output) const;
 private:
  int fd_{-1};
  std::size_t size_{}, positions_{};
  std::function<void(std::size_t, Range)> hook_;
};

struct Pending {
  enum class Outcome { unresolved, effect, no_effect };
  Outcome outcome{Outcome::unresolved};
  std::size_t result{};
};
struct RetainedState {
  std::shared_ptr<History> history;
  std::size_t position{};
  std::shared_ptr<Pending> pending;
};

struct View {
  RetainedState state;
  std::vector<ByteUse> bytes;
};

class Projection {
 public:
  struct Binding;
  /// Permissions are byte ranges. Partial pages get zero-masked private backing.
  /// Optional hook injects construction failure after each native alias mapping.
  Projection(const View& source, std::span<const Range> allowed,
             std::function<void(const std::uint8_t*)> mapped_hook = {});
  const std::uint8_t* page_data(std::size_t index) const;
  ByteUse borrow(std::size_t index, Range range) const;
  std::size_t pages() const { return bindings_.size(); }
 private:
  std::vector<std::shared_ptr<Binding>> bindings_;
  std::vector<std::vector<Range>> allowed_;
};

class Working;
class Context {
 public:
  struct State;
  struct Ticket {
    std::uint64_t incarnation{}, epoch{}, home{};
    std::size_t position{};
    Range range;
  };
  Context(Pool pool, std::shared_ptr<History> history, std::size_t position = 0);
  ~Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  RetainedState retain(std::size_t position) const;
  View open(const RetainedState& retained, std::span<const Range> ranges,
            bool service = false);
  Ticket resolve(Range range);
  /// Detects a changed binding; never treats a generation as logical state.
  ByteUse acquire(const Ticket& ticket);
  std::unique_ptr<Working> begin(Strategy strategy, std::span<const Range> writes);
  std::size_t position() const;
  void close();
 private:
  friend class Working;
  std::shared_ptr<State> state_;
};

class Working {
 public:
  struct State;
  ~Working();
  Working(const Working&) = delete;
  Working& operator=(const Working&) = delete;
  std::uint8_t* data(std::size_t write_index);
  /// Supplied outcome position; installs actual stores, not fixture result bytes.
  bool publish(std::size_t position);
  void abort();
 private:
  friend class Context;
  explicit Working(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}  // namespace object_views
