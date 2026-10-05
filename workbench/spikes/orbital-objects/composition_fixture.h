#pragma once
#include "composition.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace object_fixture {
using namespace object_views;
inline std::size_t page() { return static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); }
inline Bytes seed(std::size_t pages = 4) {
  Bytes bytes(pages * page());
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<std::uint8_t>((i * 29 + i / 7 + 13) % 251);
  return bytes;
}
inline std::vector<Range> whole(std::size_t size) {
  std::vector<Range> out;
  for (std::size_t i = 0; i < size; i += page()) out.push_back({i, page()});
  return out;
}
inline ByteUse one(Context& c, std::size_t at, Range r, bool service = false) {
  auto view = c.open(c.retain(at), std::span(&r, 1), service);
  return std::move(view.bytes.front());
}
inline Bytes read_all(Context& c, std::size_t at, std::size_t size) {
  auto ranges = whole(size);
  auto view = c.open(c.retain(at), ranges);
  Bytes out(size);
  for (std::size_t i = 0; i < ranges.size(); ++i)
    std::memcpy(out.data() + ranges[i].offset, view.bytes[i].data(), ranges[i].size);
  return out;
}
struct Caller {
  Bytes before = seed(), after;
  std::uint64_t expected = 2 * 123 + 46 * (11 + 14);
  std::shared_ptr<History> history;
  Caller() {
    for (std::size_t row = 0; row < 8; ++row) {
      before[row * 16] = 47;
      before[128 + row] = row == 1 || row == 3 || row == 4;
      const auto at = page() * (row < 4 ? 1 : 2) + (row % 4) * 64;
      std::fill_n(before.begin() + at, 64, static_cast<std::uint8_t>(row + 10));
      before[at] = row == 1 || row == 4 ? 123 : (row == 2 ? 238 : 17);
    }
    after = before;
    std::memcpy(after.data() + 256, &expected, 8);
    std::fill_n(after.begin() + 3 * page(), 64, 0);
    std::memcpy(after.data() + 3 * page(), &expected, 8);
    history = std::make_shared<History>(std::vector<Bytes>{before, after});
  }
  std::uint64_t run(Context& c, Strategy strategy) const {
    std::array<Range, 2> metadata{{{0, 128}, {128, 8}}};
    auto directory = c.open(c.retain(0), metadata);
    std::uint64_t sum{};
    for (std::size_t row = 0; row < 8; ++row) {
      if (!directory.bytes[1].data()[row]) continue;
      const auto at = page() * (row < 4 ? 1 : 2) + (row % 4) * 64;
      auto body = one(c, 0, {at, 64});
      if (body.data()[0] != 123) continue;
      for (std::size_t i = 0; i < directory.bytes[0].data()[row * 16]; ++i)
        sum += body.data()[i];
    }
    std::array<Range, 2> writes{{{256, 8}, {3 * page(), 64}}};
    auto work = c.begin(strategy, writes);
    if (!work) throw std::runtime_error("caller preparation denied");
    std::memcpy(work->data(0), &sum, 8);
    std::memset(work->data(1), 0, 64);
    std::memcpy(work->data(1), &sum, 8);
    if (!work->publish(1)) throw std::runtime_error("caller publication denied");
    return sum;
  }
};
}  // namespace object_fixture
