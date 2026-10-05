#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace publication {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t page_bytes = 4096, block_bytes = 256, fanout = 64;
struct Patch { std::size_t offset; Bytes bytes; };
using Batch = std::vector<Patch>;
enum class Kind { snapshot, flat_pages, radix_pages, block_pages, overlay };
constexpr std::array kinds{Kind::snapshot, Kind::flat_pages, Kind::radix_pages, Kind::block_pages, Kind::overlay};
std::string_view name(Kind);
std::uint64_t hash(std::span<const std::uint8_t>);
Batch coalesce(const Batch&);
void apply(Bytes&, const Batch&);
Batch changes(std::string_view shape, std::size_t bytes, unsigned epoch);

struct Counts {
  std::uint64_t payload_copied=0, metadata_copied=0, hash_payload=0, hash_metadata=0;
  std::uint64_t lookup_entries=0, read_bytes=0, allocations=0, live_bytes=0, peak_bytes=0;
  std::uint64_t compact_bytes=0;
  void allocated(std::size_t);
  void freed(std::size_t);
};
struct State;
using Cut = std::shared_ptr<const State>;
std::shared_ptr<const Batch> capture(const Batch&, Counts&);
std::shared_ptr<const Batch> capture_coalesced(const Batch&, Counts&);
class Material {
  Kind kind_;
  std::size_t bytes_;
  bool roots_;
  bool full_hash_;
  unsigned compact_every_;
  Counts& counts_;
public:
  Material(Kind, std::size_t bytes, bool material_roots, unsigned compact_every, Counts&, bool full_hash=false);
  Cut seed(std::span<const std::uint8_t>);
  Cut update(Cut, std::shared_ptr<const Batch>, std::uint64_t epoch);
  Bytes read(Cut, std::size_t offset, std::size_t length);
  std::uint64_t digest(Cut) const;
  std::size_t size() const { return bytes_; }
};

// A sink holds an exact submitted cut; its completion never establishes a
// consumer epoch. Authority and reconstruction remain in the journal.
class Sink {
  struct Request { std::uint64_t epoch; Cut cut; std::size_t bytes; bool live=true; };
  std::vector<Request> requests_;
  std::uint64_t installed_=0;
  std::size_t pending_=0, limit_;
public:
  explicit Sink(std::size_t limit) : limit_(limit) {}
  bool submit(std::uint64_t, Cut, std::size_t);
  bool complete(std::size_t ticket, bool success);
  std::uint64_t installed() const { return installed_; }
  std::size_t pending() const { return pending_; }
  std::size_t tickets() const { return requests_.size(); }
};
}
