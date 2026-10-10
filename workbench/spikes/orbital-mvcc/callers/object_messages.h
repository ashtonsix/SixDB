#pragma once
#include <loom/worker.h>
#include <array>
#include <algorithm>
#include <chrono>

// Disposable owner-posted completion control. No external arrival model, Task,
// catalog mutation or general callback extension is introduced by this fixture.
namespace loom_object_ring {
class CompletionBurst {
public:
  enum class Mode { unloaded, empty_poll, per_quantum, deferred };
  static constexpr std::size_t count = 8;
  struct Record {
    orbital::worker::Message message;
    CompletionBurst* owner{};
    std::uint64_t sequence{}, posted_ns{}, completed_ns{}, checksum{};
    std::uint32_t retained{}, handled{}, released{};
  };
private:
  loom::Worker& worker_;
  std::array<Record, count> records_;
  std::array<std::array<std::uint64_t, 8>, count> payload_{};
  std::array<std::uint64_t, count> expected_{}, order_{};
  std::size_t group_{}, handled_{}, boundaries_{}, polls_{};
  Mode mode_;
  bool diagnostic_{}, failed_{};
  static std::uint64_t now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  static const orbital::worker::MessageOps operations;
public:
  CompletionBurst(loom::Worker& worker, Mode mode) : worker_(worker), mode_(mode) {
    for (std::size_t i = 0; i < count; ++i) {
      auto& record = records_[i];
      record.owner = this; record.message.context = &record; record.message.ops = &operations;
      for (std::size_t j = 0; j < 8; ++j) payload_[i][j] = (i + 1) * 100 + j * j;
      expected_[i] = (i + 1) * 800 + 140; // sum of authored squares 0^2 through 7^2
    }
  }
  ~CompletionBurst() { drain(); }
  CompletionBurst(const CompletionBurst&) = delete;
  CompletionBurst& operator=(const CompletionBurst&) = delete;
  bool loaded() const noexcept { return mode_ == Mode::per_quantum || mode_ == Mode::deferred; }
  bool polls_during_reads() const noexcept { return mode_ == Mode::per_quantum || mode_ == Mode::empty_poll; }
  void post(std::size_t group, bool diagnostic) noexcept {
    group_ = group; diagnostic_ = diagnostic; handled_ = boundaries_ = polls_ = 0;
    failed_ = orbital::worker::pending(worker_.lane);
    if (failed_) return; // Never rewrite a record which may still be queued.
    order_.fill(UINT64_MAX);
    for (std::size_t i = 0; i < count; ++i) {
      auto& record = records_[i];
      record.sequence = group * count + i;
      record.posted_ns = record.completed_ns = record.checksum = 0;
      record.retained = record.handled = record.released = 0;
      if (loaded()) {
        if (diagnostic) record.posted_ns = now();
        orbital::worker::post(worker_.lane, record.message);
      }
    }
  }
  // Called only after an actual quantum returns, before retirement and refill.
  void boundary() noexcept {
    ++boundaries_; ++polls_;
    const auto completed = loom::poll(worker_, {.messages = 1, .tasks = 0});
    const auto expected = mode_ == Mode::per_quantum ? std::min(count, boundaries_) : 0;
    failed_ |= handled_ != expected || completed > 1;
  }
  void drain() noexcept {
    for (std::size_t attempt = 0; orbital::worker::pending(worker_.lane) && attempt < count + 1; ++attempt) {
      ++polls_; loom::poll(worker_, {.messages = 1, .tasks = 0});
    }
    failed_ |= orbital::worker::pending(worker_.lane);
  }
  bool correct(std::size_t quanta) const noexcept {
    if (failed_ || handled_ != (loaded() ? count : 0) ||
        boundaries_ != (polls_during_reads() ? quanta : 0)) return false;
    for (std::size_t i = 0; i < count; ++i) {
      const auto& record = records_[i];
      if (record.retained != unsigned(loaded()) || record.handled != unsigned(loaded()) ||
          record.released != unsigned(loaded())) return false;
      if (loaded() && (order_[i] != group_ * count + i || record.checksum != expected_[i])) return false;
      if (loaded() && diagnostic_ && record.completed_ns < record.posted_ns) return false;
      if (!diagnostic_ && (record.posted_ns || record.completed_ns)) return false;
    }
    return true;
  }
  const auto& records() const noexcept { return records_; }
  std::size_t handled() const noexcept { return handled_; }
  std::size_t boundaries() const noexcept { return boundaries_; }
  std::size_t polls() const noexcept { return polls_; }
  static constexpr std::size_t operations_bytes() noexcept { return sizeof(operations); }
};
inline const orbital::worker::MessageOps CompletionBurst::operations{
      [](auto& message) noexcept { ++static_cast<Record*>(message.context)->retained; },
      [](auto& message, auto& lane) noexcept {
        auto& record = *static_cast<Record*>(message.context);
        auto& self = *record.owner;
        self.failed_ |= &lane != &self.worker_.lane || !orbital::worker::on_lane(lane);
        ++record.handled;
        if (self.handled_ < count) self.order_[self.handled_] = record.sequence;
        else self.failed_ = true;
        ++self.handled_;
        record.checksum = 0;
        for (auto word : self.payload_[record.sequence % count]) record.checksum += word;
        if (self.diagnostic_) record.completed_ns = now();
      },
      [](auto& message) noexcept { ++static_cast<Record*>(message.context)->released; }};
} // namespace loom_object_ring
