#include "workload.h"
#include <map>

namespace orbital_bench {
namespace {
class Reference final : public Store {
  std::map<Key, Record> rows_;
public:
  void load(std::span<const Row> rows) override {
    rows_.clear();
    for (auto& row : rows) rows_.emplace(row.key, row.record);
  }
  void begin(bool) override {}
  void end() override {}
  std::optional<Record> get(Key key) override {
    auto found = rows_.find(key);
    return found == rows_.end() ? std::nullopt : std::optional(found->second);
  }
  bool update(Key key, Record record) override {
    auto found = rows_.find(key);
    if (found == rows_.end()) return false;
    found->second = record; return true;
  }
  bool insert(Key key, Record record) override { return rows_.emplace(key, record).second; }
  bool erase(Key key) override { return rows_.erase(key); }
  std::size_t scan(Key key, std::span<Row> output) override {
    std::size_t used = 0;
    for (auto it = rows_.lower_bound(key); it != rows_.end() && used < output.size(); ++it)
      output[used++] = {it->first, it->second};
    return used;
  }
};
} // namespace
std::unique_ptr<Store> reference_store() { return std::make_unique<Reference>(); }
} // namespace orbital_bench
