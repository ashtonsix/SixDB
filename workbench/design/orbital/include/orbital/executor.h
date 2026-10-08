#pragma once

// Provisional isolated execution and verification; declarations only.
// Native Lane/Thread services are implemented in Orbital; see ../../DESIGN.md.

#include <orbital/worker.h>
#include <orbital/object.h>

namespace orbital::journal {
struct Interpretation;
void retain_ref(Interpretation&) noexcept;
void release_ref(Interpretation&) noexcept;
}
namespace orbital::worker {
struct Executor;
struct Execution;
void retain_ref(Executor&) noexcept;
void release_ref(Executor&) noexcept;
void retain_ref(Execution&) noexcept;
void release_ref(Execution&) noexcept;
struct ExecutorOps {
  Progress (*start)(Executor&, Ref<Execution>);
  void (*cancel)(Execution&) noexcept;
};
struct Executor {
  std::atomic<std::uint32_t> references{};
  void (*retire)(Executor&) noexcept{};
  allocation::Hold storage;
  const ExecutorOps* ops{};
  void* backend{};
};

struct ExecutionContext {
  TransactionId transaction;
  SerialPosition position;
  Epoch input_epoch;
  std::uint64_t authority_generation;
};
struct Projection {
  Ref<object::RetainedView> use;
  ByteRange coverage;
  bool writable;
};
struct Report {
  ProgramId interpretation;
  ExecutionContext context;
  port::Source interaction;
  port::Source outcome;
};

// Physical access and completion-message handling retain this record, its
// executor and descriptors independently of the caller Task.
struct Execution {
  std::atomic<std::uint32_t> references{};
  void (*retire)(Execution&) noexcept{};
  allocation::Hold storage, descriptor_storage;
  Ref<Executor> executor;
  Ref<journal::Interpretation> interpretation;
  ExecutionContext context;
  port::Source arguments;
  std::span<const Projection> projections;
  Report report;
  Signal finished;
  Message completion;
  void* backend_state{};
};
// Validate captured authority, initialized coverage and padding. Reuse owned
// descriptors or copy into reserved storage; completion returns to caller Lane.
[[nodiscard]] Result<void> prepare(Execution&, Executor&, Lane& completion_lane,
    Ref<journal::Interpretation>, ExecutionContext,
    port::Source arguments, std::span<const Projection>,
    std::span<Projection> reserved_descriptors);
// Retry observes the same accepted/terminal execution. Preaccept pressure is
// idle; cancelling caller interest does not retire physical projections.
Progress start(Ref<Execution>);
void cancel_interest(Execution&) noexcept;
void finish(Ref<Execution>, Report, Result<void>) noexcept;
[[nodiscard]] Result<const Report*> inspect(const Execution&);
// Compare main interaction and selected outcome against captured semantics.
// Owner/Ikea imports under the original authority; verification never publishes.
[[nodiscard]] Result<void> verify(
    const Execution& main, std::span<const Ref<Execution>> checkers,
    bool compare_exact_representation);
} // namespace orbital::worker
