#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace remap {
using U = std::uint64_t;
constexpr U absent = ~U{0};
constexpr unsigned columns_max = 16;
struct Entry {
  U key;
  unsigned id;
  bool operator==(const Entry &) const = default;
};
struct Image {
  std::vector<Entry> rows;
  std::vector<std::vector<U>> columns;
};
struct Profile {
  std::string name;
  unsigned columns = 0, references = 0;
  bool summaries = false, stable_ids = false;
  unsigned natural_slack = 25;
};
struct Case {
  std::string name;
  unsigned population = 0, operations = 512;
  U universe = 65536;
  unsigned distribution = 0, insertion = 0;
};
struct Op {
  U erase;
  Entry row;
  std::array<U, columns_max> values;
  U probe;
};
struct Trace {
  Case config;
  Image initial;
  std::vector<Op> operations;
  std::vector<U> queries;
  U digest = 0;
  unsigned identities = 0;
};
struct Counters {
  U payload_moved = 0, key_moved = 0, relabelled = 0, references_repaired = 0;
  U summary_adjustments = 0, splits = 0, page_relabels = 0, largest_move = 0;
  U redistributions = 0;
};
struct Footprint {
  U payload = 0, keys = 0, routing = 0, references = 0, summary_entries = 0,
    pages = 0, nodes = 0;
};
class Index {
public:
  virtual ~Index() = default;
  virtual U point(U key) const = 0;
  virtual U scan(U lower, unsigned count) const = 0;
  virtual U secondary(unsigned id, unsigned index) const = 0;
  virtual void erase(U key) = 0;
  virtual void insert(Entry row, const U *values) = 0;
  virtual Image image() const = 0;
  virtual Footprint footprint() const = 0;
  virtual Counters counters() const = 0;
  virtual void validate() const = 0;
  virtual void reset_counters() = 0;
};
std::vector<Case> cases();
std::vector<Profile> profiles();
std::vector<std::string> methods();
Trace trace(Case c);
Trace after_trace(const Trace &);
std::unique_ptr<Index> make_index(const std::string &, const Profile &,
                                  const Image &, unsigned identities,
                                  bool accounting);
U row_digest(Entry, const U *, unsigned columns);
U cycle(Index &, const Trace &, const Profile &, bool verify = false);
void check_all();
} // namespace remap
