// Actual allocation/Hold integration: rebind packs after relocation and retain
// old bounded bindings only while their address and physical grant remain valid.
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <loom/worker.h>
#include <thread>

namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace alloc = orbital::allocation;
[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr, "Ikea/Orbital growth: %s\n", reason);
  std::abort();
}
void require(bool value, const char* reason) {
  if (!value)
    fail(reason);
}
template <class T, class E> T take(std::expected<T, E> value, const char* reason) {
  if (!value)
    fail(reason);
  return std::move(*value);
}
template <class E> void okay(std::expected<void, E> value, const char* reason) {
  require(bool(value), reason);
}

void series(loom::Worker& worker) {
  using F = sp::format<20>;
  constexpr std::size_t first_rows = 7, next_rows = 256;
  constexpr auto old_bytes = F::tile_bytes, next_bytes = next_rows * 20 / 8;
  auto allocation =
      take(loom::allocate(worker, {.bytes = old_bytes, .alignment = 64}), "allocate Series");
  require(alloc::reserved_bytes(allocation) < next_bytes, "Series fixture must require relocation");
  std::array<std::uint32_t, first_rows> first{19, 42, 71, 88, 13, 512, 1048575};
  std::uint64_t sum = 0;
  for (auto value : first)
    sum += value;
  {
    auto held = take(loom::hold(allocation, {0, old_bytes}), "initial Series Hold");
    auto bytes = loom::bytes(held);
    auto view =
        take(sp::view<F, std::uint8_t>::attach(
                 first_rows,
                 {{{{reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()}, F::tile_bytes},
                   {},
                   {}}}),
             "initial Series view");
    auto prepared = take(sp::bind_mutation(view), "initial Series bind");
    std::array<ikea::owner_write, 32> records;
    ikea::source_write_journal effects{records};
    okay(prepared.initialize(std::span<const std::uint32_t>(first), effects),
         "initial Series construction");
    for (auto record : effects.entries())
      require(record.source == &view && record.bytes.plane == 0 &&
                  record.bytes.offset + record.bytes.size <= old_bytes,
              "initial Series effects");
    auto old_address = allocation.address;
    auto rejected = loom::grow(allocation,
        {.bytes = next_bytes, .alignment = 64, .initialization = alloc::Initialization::zero});
    require(!rejected && rejected.error() == orbital::Error::conflict,
            "Series growth must refuse live physical Hold");
    require(allocation.address == old_address && allocation.size == old_bytes,
            "failed grow changed allocation");
    auto reader = sp::bind_decoder<std::uint32_t>(view);
    for (std::size_t i = 0; i < first_rows; ++i)
      require(reader.get(i) == first[i], "failed grow changed Series values");
  }
  okay(loom::grow(allocation,
           {.bytes = next_bytes, .alignment = 64, .initialization = alloc::Initialization::zero}),
       "grow Series after retiring Hold");
  {
    auto held = take(loom::hold(allocation, {0, next_bytes}), "grown Series Hold");
    auto bytes = loom::bytes(held);
    auto view =
        take(sp::view<F, std::uint8_t>::attach(
                 next_rows,
                 {{{{reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()}, F::tile_bytes},
                   {},
                   {}}}),
             "grown Series view");
    auto prepared = take(sp::bind_mutation(view), "grown Series bind");
    auto reader = sp::bind_decoder<std::uint32_t>(view);
    for (std::size_t i = 0; i < next_rows; ++i)
      require(reader.get(i) == (i < first_rows ? first[i] : 0),
              "growth did not preserve prefix/initialize capacity");
    std::array<std::uint32_t, next_rows - first_rows> appended;
    std::uint64_t expected_delta = 0;
    for (std::size_t i = 0; i < appended.size(); ++i)
      expected_delta += (appended[i] = std::uint32_t(i * 73 + 1));
    std::array<ikea::owner_write, 512> records;
    ikea::source_write_journal effects{records};
    sp::sum_change change;
    okay(prepared.replace(first_rows, std::span<const std::uint32_t>(appended),
                          sp::row_selection::all(), change, effects),
         "append Series after growth");
    require(change.finish() == expected_delta, "Series append summary delta");
    std::uint64_t actual_sum = 0;
    for (std::size_t i = 0; i < next_rows; ++i) {
      auto value = take(reader.get(i), "read grown Series");
      require(value == (i < first_rows ? first[i] : appended[i - first_rows]),
              "grown Series independent values");
      actual_sum += value;
    }
    require(actual_sum == sum + expected_delta, "grown Series complete summary");
    for (auto record : effects.entries())
      require(record.source == &view && record.bytes.plane == 0 &&
                  record.bytes.offset + record.bytes.size <= next_bytes,
              "grown Series effects");
    effects.used = 0;
    sp::sum_change replacement;
    okay(prepared.set(next_rows - 1, std::uint32_t{123}, replacement, effects),
         "replace grown Series last row");
    require(reader.get(next_rows - 2) == appended[appended.size() - 2] &&
                reader.get(next_rows - 1) == 123,
            "grown last-row replacement damaged neighbor");
    require(replacement.finish() == std::uint64_t{123} - appended.back(),
            "grown Series replacement delta");
  }
}

void tuples(loom::Worker& worker) {
  constexpr std::size_t stride = 4, rows = 256;
  const std::array<tp::code, 4> codes{{{0, 0, 8}, {1, 0, 8}, {2, 0, 8}, {3, 0, 8}}};
  auto layout = take(tp::layout::make(stride, codes), "Tuple layout");
  auto constructor = take(tp::constructor::make(layout), "Tuple constructor");
  auto reader_plan =
      take(tp::reader<8>::make(layout, std::array<tp::byte, 4>{0, 1, 2, 3}), "Tuple reader plan");
  auto allocation =
      take(loom::allocate(worker, {.bytes = stride, .alignment = 64}), "allocate Tuple");
  require(alloc::reserved_bytes(allocation) < rows * stride,
          "Tuple fixture must require relocation");
  {
    auto held = take(loom::hold(allocation, {0, stride}), "initial Tuple Hold");
    auto bytes = loom::bytes(held);
    auto view =
        take(tp::view::bind(layout, {reinterpret_cast<tp::byte*>(bytes.data()), bytes.size()}, 1,
                            stride),
             "initial Tuple view");
    auto construct = take(tp::bind_constructor(constructor, view), "initial Tuple binding");
    std::array<tp::construction_input, 1> input{};
    input[0][0] = 0x34;
    input[0][1] = 0x12;
    input[0][2] = 0xcd;
    input[0][3] = 0xab;
    std::array<ikea::owner_write, 1> records;
    ikea::source_write_journal effects{records};
    okay(construct.initialize(0, input, effects), "initial Tuple construction");
    auto rejected = loom::grow(allocation,
        {.bytes = rows * stride, .alignment = 64, .initialization = alloc::Initialization::zero});
    require(!rejected && rejected.error() == orbital::Error::conflict,
            "Tuple growth must refuse live physical Hold");
    auto read = take(tp::bind_reader(reader_plan, view), "initial Tuple read");
    require(read.get(0) == 0xabcd1234u, "failed grow changed Tuple");
  }
  okay(
      loom::grow(allocation,
          {.bytes = rows * stride, .alignment = 64, .initialization = alloc::Initialization::zero}),
      "grow Tuple after retiring Hold");
  {
    auto held = take(loom::hold(allocation, {0, rows * stride}), "grown Tuple Hold");
    auto bytes = loom::bytes(held);
    auto view =
        take(tp::view::bind(layout, {reinterpret_cast<tp::byte*>(bytes.data()), bytes.size()}, rows,
                            stride),
             "grown Tuple view");
    auto construct =
        take(tp::bind_constructor(constructor, view), "grown Tuple construction binding");
    std::array<tp::construction_input, rows - 1> input{};
    for (std::size_t i = 0; i < input.size(); ++i) {
      const auto value = std::uint32_t(0x10203000u + i);
      for (unsigned j = 0; j < 4; ++j)
        input[i][j] = value >> (8 * j);
    }
    std::array<ikea::owner_write, rows> records;
    ikea::source_write_journal effects{records};
    okay(construct.initialize(1, input, effects), "append Tuple construction");
    auto read = take(tp::bind_reader(reader_plan, view), "grown Tuple read");
    require(read.get(0) == 0xabcd1234u, "Tuple append damaged prefix");
    for (std::size_t i = 1; i < rows; ++i)
      require(read.get(i) == 0x10203000u + i - 1, "Tuple independent grown values");
    for (auto record : effects.entries())
      require(record.source == &view && record.bytes.plane == 0 && record.bytes.offset >= stride &&
                  record.bytes.offset + record.bytes.size <= rows * stride,
              "grown Tuple effects");
  }
}
void reserved_window(loom::Worker& worker) {
  constexpr std::size_t stride = 4;
  const std::array<tp::code, 4> codes{{{0, 0, 8}, {1, 0, 8}, {2, 0, 8}, {3, 0, 8}}};
  auto layout = take(tp::layout::make(stride, codes), "window Tuple layout");
  auto constructor = take(tp::constructor::make(layout), "window Tuple constructor");
  auto reader_plan =
      take(tp::reader<8>::make(layout, std::array<tp::byte, 4>{0, 1, 2, 3}), "window Tuple reader");
  auto allocation =
      take(loom::allocate(
               worker,
               {.bytes = stride, .reserved_bytes = 16384, .alignment = 64, .readable_padding = 60}),
           "allocate reserved window");
  auto* address = allocation.address;
  {
    // The owner deliberately retains a wider read grant, including padding.
    // Tuple itself needs only the first four bytes. The grant still cannot
    // be overwritten by growth while its physical reader remains live.
    auto held = take(loom::hold(allocation, {0, 64}), "window padding Hold");
    const auto bytes = loom::bytes(held);
    auto view =
        take(tp::view::bind(layout, {reinterpret_cast<tp::byte*>(bytes.data()), bytes.size()}, 1,
                            stride),
             "window Tuple view");
    auto construct = take(tp::bind_constructor(constructor, view), "window construction binding");
    std::array<tp::construction_input, 1> input{};
    input[0][0] = 0x71;
    std::array<ikea::owner_write, 1> records;
    ikea::source_write_journal effects{records};
    okay(construct.initialize(0, input, effects), "window initial construction");
    auto refused = loom::grow(allocation,
                              {.bytes = 4096,
                               .reserved_bytes = 16384,
                               .alignment = 64,
                               .initialization = alloc::Initialization::zero});
    require(!refused && refused.error() == orbital::Error::conflict,
            "in-place grow overwrites held padding");
    require(allocation.address == address && allocation.size == stride,
            "padding conflict changed allocation");
    for (std::size_t i = stride; i < bytes.size(); ++i)
      require(bytes[i] == std::byte{}, "padding conflict changed held bytes");
  }
  okay(loom::grow(allocation,
                  {.bytes = 4096,
                   .reserved_bytes = 16384,
                   .alignment = 64,
                   .initialization = alloc::Initialization::zero}),
       "grow reserved window after padding Hold retirement");
  require(allocation.address == address, "reserved growth unexpectedly relocated");
  {
    auto held = take(loom::hold(allocation, {0, 4096}), "window body Hold");
    const auto bytes = loom::bytes(held);
    auto view =
        take(tp::view::bind(layout, {reinterpret_cast<tp::byte*>(bytes.data()), bytes.size()}, 1024,
                            stride),
             "window body view");
    auto read = take(tp::bind_reader(reader_plan, view), "window body reader");
    // Growth changes only bytes beyond this old grant, within the same
    // reserved address range. Its old view remains usable at its old extent.
    okay(loom::grow(allocation,
                    {.bytes = 8192,
                     .reserved_bytes = 16384,
                     .alignment = 64,
                     .initialization = alloc::Initialization::zero}),
         "disjoint in-place growth with live prefix Hold");
    require(allocation.address == address && read.get(0) == 0x71,
            "in-place growth invalidated old binding");
    require(!read.get(1024), "old binding acquired the grown extent implicitly");
    auto larger = take(loom::hold(allocation, {0, 8192}), "larger window Hold");
    auto larger_bytes = loom::bytes(larger);
    auto rebound =
        take(tp::view::bind(layout,
                            {reinterpret_cast<tp::byte*>(larger_bytes.data()), larger_bytes.size()},
                            2048, stride),
             "larger window view");
    auto rebound_read = take(tp::bind_reader(reader_plan, rebound), "larger window binding");
    for (std::size_t i = 0; i < 2048; ++i)
      require(rebound_read.get(i) == (i ? 0u : 0x71u), "window prefix or zero extent lost");
  }
}

int main() {
  loom::Worker worker;
  okay(loom::adopt(worker), "adopt Worker");
  auto arena = take(alloc::open_arena(64u << 20), "open Arena");
  okay(loom::configure(worker, arena, 8), "configure Worker");
  series(worker);
  tuples(worker);
  reserved_window(worker);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    auto status = loom::shutdown(worker);
    if (status.kind() == orbital::Progress::Kind::done)
      break;
    require(status.kind() != orbital::Progress::Kind::failed, "Worker shutdown failed");
    require(std::chrono::steady_clock::now() < deadline, "Worker shutdown stuck");
    loom::poll(worker, {1024, 1024});
    std::this_thread::yield();
  }
  require(arena->charged_bytes == 0, "Arena charges remain after shutdown");
  loom::relinquish(worker);
  std::puts("Real Ikea/Loom/Orbital: grow refusal, relocation/rebind, Series/Tuple values, "
            "summaries, effects and shutdown passed");
}
