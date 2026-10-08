#include <ikea/seriespack.h>
#include <loom/worker.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
namespace sp = ikea::seriespack;
namespace alloc = orbital::allocation;
using Format = sp::preset_format<20, sp::preset::compact>;
constexpr std::size_t first_rows = 16, final_rows = 32, padding = 64;

constexpr std::size_t body_bytes(std::size_t rows) {
  return rows / Format::tile_rows * Format::tile_bytes;
}

void check(bool condition, const char* explanation) noexcept {
  if (condition) return;
  std::fprintf(stderr, "loom_example_allocation: %s\n", explanation);
  std::abort();
}

template<class T, class E> T take(std::expected<T, E> result) {
  check(result.has_value(), "operation failed");
  return std::move(*result);
}

// The assigned owner keeps Allocation stable during this uninterrupted visit.
// Named Ikea bindings end before growth; ready work needs no escaping Hold.
void write_half(loom::Worker& worker, alloc::Allocation& column, std::size_t first) {
  check(orbital::worker::on_lane(worker.lane), "write ran on a foreign Lane");
  const auto rows = first + first_rows;
  auto bytes = loom::bytes(column).first(body_bytes(rows));
  auto view = take(sp::view<Format, std::uint8_t>::attach(rows,
      {{{{reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()}, Format::tile_bytes}, {}, {}}}));
  auto prepared = take(sp::bind_mutation(view));
  auto writer = prepared.erase<std::uint32_t, sp::sum_change>();
  std::array<std::uint32_t, first_rows> input;
  for (std::size_t i = 0; i < input.size(); ++i) input[i] = first + i + 1;
  std::array<ikea::owner_write, 64> records;
  ikea::source_write_journal effects{records};
  if (!first) {
    check(writer.initialize(input, effects).has_value(), "initial SeriesPack write failed");
  } else {
    sp::sum_change change;
    check(writer.replace(first, input, sp::row_selection::all(), change, effects).has_value(),
        "SeriesPack append failed");
    check(change.finish() == 392, "new initialized slots were not zero before append");
  }
  for (const auto& effect : effects.entries()) {
    check(effect.source == &view && !effect.bytes.plane, "unresolved source-qualified write");
    check(effect.bytes.offset + effect.bytes.size <= bytes.size(), "write effect exceeds current storage");
  }
}

std::uint64_t read_sum(orbital::Bytes bytes, std::size_t rows) {
  auto view = take(sp::view<Format>::attach(rows,
      {{{{reinterpret_cast<const std::uint8_t*>(bytes.data()), body_bytes(rows)}, Format::tile_bytes}, {}, {}}}));
  auto reader = sp::bind_decoder<std::uint32_t>(view);
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < rows; ++i) {
    auto value = take(reader.get(i));
    check(value == i + 1, "SeriesPack value differs from independent oracle");
    sum += value;
  }
  return sum;
}

// This physical record outlives its native thread and final Message handling.
// It contains no Task pointer; ending Task interest would not revoke its Hold.
struct Reader {
  alloc::Hold use;
  orbital::Signal done;
  orbital::worker::Message finished;
  std::atomic<bool> go{};
  std::uint64_t sum{};
  static const orbital::worker::MessageOps operations;
};

const orbital::worker::MessageOps Reader::operations{
    [](orbital::worker::Message&) noexcept {},
    [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
      auto& reader = *static_cast<Reader*>(message.context);
      check(reader.done.lane == &lane && !reader.use, "completion preceded physical reader retirement");
      orbital::complete(reader.done, {});
    },
    [](orbital::worker::Message&) noexcept {}};

struct Growth {
  loom::Task task;
  alloc::Allocation* column{};
  Reader* reader{};
  static orbital::Progress step(loom::Task& task) noexcept {
    auto& growth = *static_cast<Growth*>(task.context);
    auto& completed = growth.reader->done;
    const auto observed = completed.generation;
    if (completed.state != orbital::Readiness::ready)
      return orbital::Progress::wait(completed, observed);

    // The reader really finished before its Message. Relocation is now legal.
    // Rebind against the new bytes and write only the new half, exactly once.
    auto grown = loom::grow(*growth.column,
        {.bytes = body_bytes(final_rows), .alignment = 64,
         .readable_padding = padding, .initialization = alloc::Initialization::zero});
    if (!grown) return orbital::Progress::fail(grown.error());
    write_half(*task.worker, *growth.column, first_rows);
    return orbital::Progress::done();
  }
};
} // namespace

int main() {
  loom::Worker worker;
  check(loom::adopt(worker).has_value(), "Worker adoption failed");
  auto arena = take(alloc::open_arena(1 << 20));
  check(loom::configure(worker, std::move(arena), 1).has_value(), "Worker configuration failed");
  auto column = take(loom::allocate(worker,
      {.bytes = body_bytes(first_rows), .alignment = 64,
       .readable_padding = padding, .initialization = alloc::Initialization::zero}));
  write_half(worker, column, 0);
  check(read_sum(loom::bytes(column), first_rows) == 136, "initial SeriesPack sum differs");

  Reader reader;
  reader.use = take(loom::hold(column, {0, column.size + padding}));
  reader.done.lane = &worker.lane;
  reader.finished.context = &reader;
  reader.finished.ops = &Reader::operations;
  std::thread thread([&] {
    reader.go.wait(false, std::memory_order_acquire);
    const auto bytes = loom::bytes(reader.use);
    reader.sum = read_sum(bytes, first_rows);
    for (const auto byte : bytes.subspan(body_bytes(first_rows)))
      check(byte == std::byte{}, "readable padding was not initialized");
    loom::release(reader.use);
    orbital::worker::post(worker.lane, reader.finished);
  });

  Growth growth;
  growth.column = &column;
  growth.reader = &reader;
  growth.task.context = &growth;
  growth.task.step = Growth::step;
  check(loom::start(worker, growth.task), "growth Task admission failed");
  loom::poll(worker, {8, 1});
  check(growth.task.state == loom::TaskState::waiting && column.size == body_bytes(first_rows),
      "growth did not wait for its foreign reader");
  reader.go.store(true, std::memory_order_release);
  reader.go.notify_one();
  thread.join();

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (growth.task.worker) {
    check(std::chrono::steady_clock::now() < deadline, "growth did not make bounded progress");
    if (!loom::poll(worker, {8, 1})) std::this_thread::yield();
  }
  check(growth.task.state == loom::TaskState::done && reader.sum == 136, "growth or old reader result differs");
  check(read_sum(loom::bytes(column), final_rows) == 528, "appended SeriesPack sum differs");
  loom::release(column);
  for (;;) {
    const auto progress = loom::shutdown(worker);
    check(progress.kind() != orbital::Progress::Kind::failed, "Worker shutdown failed");
    if (progress.kind() == orbital::Progress::Kind::done) break;
    check(std::chrono::steady_clock::now() < deadline, "physical shutdown did not make bounded progress");
    if (!loom::poll(worker, {8, 1})) std::this_thread::yield();
  }
  loom::relinquish(worker);
  std::puts("loom_example_allocation: old sum 136, appended sum 528, physically drained");
}
