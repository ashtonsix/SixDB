#include "journal.h"
#include "material.h"
#include <ikea/bec256.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace bc = ikea::bec256;
using namespace publication;
namespace {
constexpr std::size_t descriptor_at = 16, summary_at = 64, body_at = 128;
constexpr std::array<std::uint8_t, 12> empty_descriptor{0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 12> full_descriptor{0, 1, 0, 0, 128, 0, 0, 0, 1, 0, 0, 0};
constexpr std::array<std::uint8_t, 9> empty_summary{0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr std::array<std::uint8_t, 9> full_summary{0, 1, 0, 0, 0, 0, 0, 0, 1};
template<class T, class E> T get(std::expected<T, E> result) {
  assert(result); return std::move(*result);
}
Bytes expected(bool full) {
  Bytes image(page_bytes);
  for (std::size_t i = 0; i < image.size(); ++i) image[i] = (i * 29 + 47) % 251;
  const auto& descriptor = full ? full_descriptor : empty_descriptor;
  const auto& summary = full ? full_summary : empty_summary;
  std::copy(descriptor.begin(), descriptor.end(), image.begin() + descriptor_at);
  std::copy(summary.begin(), summary.end(), image.begin() + summary_at);
  return image;
}
void verify(const Bytes& image, bool full) {
  assert(image == expected(full));
  const auto population = unsigned(image[descriptor_at]) | (unsigned(image[descriptor_at + 1]) << 8);
  const auto length = unsigned(image[descriptor_at + 2]) | (unsigned(image[descriptor_at + 3]) << 8);
  unsigned address = 0;
  for (unsigned i = 0; i < 4; ++i) address |= unsigned(image[descriptor_at + 4 + i]) << (i * 8);
  std::uint64_t count = 0;
  for (unsigned i = 0; i < 8; ++i) count |= std::uint64_t(image[summary_at + i]) << (i * 8);
  assert(population == (full ? 256U : 0U) && length == 0 && address == body_at);
  assert(count == population && image[summary_at + 8] == 1);
  auto source = get(bc::bind_reader(
      {reinterpret_cast<const bc::byte*>(image.data() + address), image.size() - address}, length, population));
  std::array<std::byte, 32> decoded;
  bc::decode(source, decoded);
  for (auto byte : decoded) assert(byte == (full ? bc::byte{0xff} : bc::byte{0}));
}
struct Head { unsigned epoch; Cut cut; };
}  // namespace

int main() {
  // This small directory/count layout is caller-owned, not a Bec256 format.
  const auto path = std::filesystem::temp_directory_path() /
      ("sixdb-unit-boundary-" + std::to_string(::getpid()));
  auto home = expected(false);
  bc::view body{{reinterpret_cast<bc::byte*>(home.data() + body_at), bc::scratch_bytes}};
  std::array<ikea::journal::entry, 1> slots;
  ikea::journal effects{slots};
  std::array<std::byte, 32> empty{}, full;
  full.fill(bc::byte{0xff});
  assert(get(bc::encode(empty, 0, body, 0, effects)) == 0 && effects.used == 0);
  assert(get(bc::encode(full, 256, body, 0, effects)) == 0 && effects.used == 0);
  assert(home == expected(false));

  Counts counts;
  Material material(Kind::radix_pages, page_bytes, true, 0, counts);
  const Head old{0, material.seed(home)};
  std::atomic<const Head*> visible{&old};
  // Replaying only the codec's body effects changes neither interpretation nor
  // count: a structurally valid zero-byte body still decodes to the old data.
  auto body_only = material.update(old.cut, capture({}, counts), 1);
  verify(material.read(body_only, 0, page_bytes), false);
  assert(material.digest(body_only) == material.digest(old.cut));

  // Owner stores population/length/address/generation and count/validity while
  // excluding readers, then captures those bytes with the enclosing mutation.
  const unsigned population = 256, length = 0, address = body_at, generation = 1;
  home[descriptor_at] = static_cast<std::uint8_t>(population); home[descriptor_at + 1] = population >> 8;
  home[descriptor_at + 2] = length; home[descriptor_at + 3] = length >> 8;
  for (unsigned i = 0; i < 4; ++i) {
    home[descriptor_at + 4 + i] = address >> (i * 8);
    home[descriptor_at + 8 + i] = generation >> (i * 8);
  }
  for (unsigned i = 0; i < 8; ++i) home[summary_at + i] = std::uint64_t(population) >> (i * 8);
  home[summary_at + 8] = 1;
  Batch patches{{descriptor_at, Bytes(home.begin() + descriptor_at, home.begin() + descriptor_at + 12)},
                {summary_at, Bytes(home.begin() + summary_at, home.begin() + summary_at + 9)}};
  auto stable = capture(patches, counts);
  Head newest{1, {}};
  {
    Journal journal(path, expected(false));
    journal.append(1, *stable);
    newest.cut = material.update(old.cut, stable, 1);
    assert(material.digest(newest.cut) != material.digest(old.cut));
    visible.store(&newest, std::memory_order_release);
    verify(material.read(visible.load(std::memory_order_acquire)->cut, 0, page_bytes), true);
    verify(material.read(old.cut, 0, page_bytes), false);
  }
  {
    Recovery reopened(path);
    assert(reopened.latest() == 1);
    verify(reopened.read_latest(1, 0, page_bytes), true);
    verify(reopened.read(0, 0, page_bytes), false);
  }
  std::filesystem::remove(path);
  std::puts("{\"status\":\"passed\",\"body_bytes\":0,\"body_effects\":0,\"owner_patch_bytes\":21,\"old_population\":0,\"new_population\":256,\"checks\":\"zero-body-encode,body-only-insufficient,descriptor-summary-cut,retained-old,reopened-new-and-old\"}");
}
