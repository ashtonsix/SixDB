#include "retained_view.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace sixdb::sim::retained_view {
namespace {
constexpr ActorId owner = 1;
constexpr HostId host = 1;
constexpr ProcessId process = 1;
constexpr std::uint64_t initialize = 1, prepare = 2, delayed_read = 3, verify_head = 4, late_write = 5, io = 6;
constexpr std::array<unsigned char, 4> masks{17, 33, 65, 129};
using Records = std::vector<std::pair<std::string, Bytes>>;
using Done = std::function<void(Context&)>;
using Loaded = std::function<void(Context&, Bytes)>;

// Fixture encoding only: length-delimited opaque fields, not an Orbital format.
void number(Bytes& out, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
void field(Bytes& out, std::string_view bytes) {
  number(out, static_cast<std::uint32_t>(bytes.size())); out.append(bytes);
}
struct Reader {
  std::string_view bytes;
  std::size_t at{};
  std::uint32_t number() {
    if (bytes.size() - at < 4) throw std::invalid_argument("truncated fixture number");
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(static_cast<unsigned char>(bytes[at++])) << (i * 8);
    return result;
  }
  Bytes field() {
    auto size = number();
    if (size > bytes.size() - at) throw std::invalid_argument("truncated fixture field");
    auto result = Bytes(bytes.substr(at, size)); at += size; return result;
  }
  void finish() const { if (at != bytes.size()) throw std::invalid_argument("trailing fixture bytes"); }
};
struct Descriptor {
  std::uint32_t version{};
  std::string base, codec;
  std::vector<std::string> operations;
};
Bytes encode(const Descriptor& d) {
  Bytes out; number(out, d.version); field(out, d.base); field(out, d.codec);
  number(out, static_cast<std::uint32_t>(d.operations.size()));
  for (const auto& op : d.operations) field(out, op);
  return out;
}
Descriptor decode(std::string_view bytes) {
  Reader r{bytes}; Descriptor out;
  out.version = r.number(); out.base = r.field(); out.codec = r.field();
  auto count = r.number();
  if (count > 4) throw std::invalid_argument("fixture replay chain too long");
  while (count--) out.operations.push_back(r.field());
  r.finish(); return out;
}
Bytes pack(const Records& records) {
  Bytes out; number(out, static_cast<std::uint32_t>(records.size()));
  for (const auto& [key, value] : records) { field(out, key); field(out, value); }
  return out;
}
Records unpack(std::string_view bytes) {
  Reader r{bytes}; Records result; auto count = r.number();
  if (count > 16) throw std::invalid_argument("fixture record bound exceeded");
  while (count--) { auto key = r.field(); auto value = r.field(); result.emplace_back(std::move(key), std::move(value)); }
  r.finish(); return result;
}
Bytes base_bytes() {
  Bytes result(1024, '\0');
  for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<char>(i & 255);
  return result;
}
Bytes base_record(std::uint32_t version, std::string_view bytes) {
  Bytes result; number(result, version); field(result, bytes); return result;
}
Descriptor chain(std::uint32_t version) {
  Descriptor d{version, "data/base/0", "data/codec/xor-v1", {}};
  for (std::uint32_t i = 1; i <= version; ++i) d.operations.push_back("data/op/" + std::to_string(i));
  return d;
}
std::set<std::string> references(const Descriptor& d) {
  std::set<std::string> result{d.base, d.codec}; result.insert(d.operations.begin(), d.operations.end()); return result;
}
Records initial_records() {
  Records records{{"data/base/0", base_record(0, base_bytes())},
      {"data/codec/xor-v1", "xor-bytes-v1"}, {"data/codec/raw-v1", "raw-bytes-v1"},
      {"data/garbage", Bytes(128, 'x')}, {"head", encode(chain(4))},
      {"roots/reader", encode(chain(2))}, {"roots/replay", encode(chain(4))}};
  for (unsigned i = 0; i < masks.size(); ++i) {
    Bytes delta; number(delta, i + 1); delta.push_back(static_cast<char>(masks[i]));
    records.emplace_back("data/op/" + std::to_string(i + 1), std::move(delta));
  }
  return records;
}
std::string root_name(Root root) { return root == Root::reader ? "roots/reader" : "roots/replay"; }

class Owner final : public Actor {
 public:
  explicit Owner(Case config) : config_(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::boot) {
      workspace_ = ctx.allocate(Bytes(4096, '\0'));
      if (!workspace_) { ctx.note("retained.blocked", "workspace"); return; }
      ctx.note("retained.boot", "empty");
      return;
    }
    if (!workspace_) return;
    if (event.kind == EventKind::completion) {
      if (event.tag != io || !pending_) throw std::logic_error("unexpected retained-view completion");
      auto next = std::exchange(pending_, {}); next(ctx, event); return;
    }
    if (pending_) throw std::logic_error("retained-view fixture overlapped serialized commands");
    switch (event.tag) {
      case initialize:
        initial_ = unpack(event.bytes); initial_index_ = 0; initialize_next(ctx); break;
      case prepare: prepare_checkpoint(ctx); break;
      case delayed_read:
        ctx.note("retained.first_access", config_.negative == Negative::publish_before_checkpoint ? "head" : root_name(config_.root));
        reconstruct(ctx, config_.negative == Negative::publish_before_checkpoint ? "head" : root_name(config_.root),
          [this](Context& c, Bytes bytes) {
            c.note("retained.old_complete", std::move(bytes));
            erase(c, root_name(config_.root), [this](Context& next) {
              collect(next, [](Context& done) { done.note("retained.collected", "after-release"); });
            });
          });
        break;
      case verify_head:
        reconstruct(ctx, "head", [](Context& c, Bytes bytes) { c.note("retained.checkpoint_complete", std::move(bytes)); }); break;
      case late_write: write(ctx, "data/checkpoint/4", event.bytes, [](Context&) {}); break;
      default: throw std::invalid_argument("unknown retained-view command");
    }
  }

 private:
  Case config_;
  std::optional<Buffer> workspace_;
  std::function<void(Context&, const Event&)> pending_;
  Records initial_;
  std::size_t initial_index_{};
  Descriptor restored_;
  std::string restored_root_, codec_;
  Bytes restored_bytes_;
  std::uint32_t restored_version_{};
  std::size_t replay_index_{};
  Loaded restored_done_;
  std::set<std::string> keep_;
  std::vector<std::string> page_;
  std::size_t page_index_{};
  std::string next_page_;
  Done collect_done_;

  bool status(Context& ctx, const Event& event, std::string_view key, bool missing_ok = false) {
    if (event.status == Status::ok || (missing_ok && event.status == Status::missing)) return true;
    ctx.note(event.status == Status::capacity ? "retained.blocked" : "retained.unavailable",
             std::string(key) + ":" + std::string(name(event.status)));
    return false;
  }
  void read(Context& ctx, std::string key, Loaded done) {
    pending_ = [this, key, done = std::move(done)](Context& c, const Event& e) mutable {
      if (status(c, e, key)) done(c, e.bytes);
    };
    ctx.read(std::move(key), 1200, io);
  }
  void write(Context& ctx, std::string key, Bytes bytes, Done done) {
    auto buffer = ctx.allocate(bytes);
    if (!buffer) { ctx.note("retained.blocked", "write-buffer:" + key); return; }
    pending_ = [this, key, done = std::move(done)](Context& c, const Event& e) mutable {
      if (status(c, e, key)) done(c);
    };
    auto operation = ctx.write(key, *buffer, io);
    // The public write borrows these exact immutable bytes. Ownership can end
    // immediately, including when the process will die before completion.
    ctx.release(*buffer);
    ctx.note("retained.write_intent", pack({{key, bytes}}), operation);
  }
  void erase(Context& ctx, std::string key, Done done) {
    pending_ = [this, key, done = std::move(done)](Context& c, const Event& e) mutable {
      if (status(c, e, key, true)) done(c);
    };
    ctx.erase(std::move(key), io);
  }
  void initialize_next(Context& ctx) {
    if (initial_index_ == initial_.size()) {
      initial_.clear(); ctx.note("retained.initialized"); ctx.timer(0, prepare); return;
    }
    auto [key, bytes] = initial_[initial_index_++];
    write(ctx, std::move(key), std::move(bytes), [this](Context& c) { initialize_next(c); });
  }
  void reconstruct(Context& ctx, std::string root, Loaded done) {
    restored_root_ = root; restored_done_ = std::move(done);
    read(ctx, std::move(root), [this](Context& c, Bytes bytes) {
      restored_ = decode(bytes);
      read(c, restored_.codec, [this](Context& c2, Bytes decoder) {
        codec_ = std::move(decoder);
        if (codec_ != "xor-bytes-v1" && codec_ != "raw-bytes-v1") throw std::runtime_error("unknown persisted decoder");
        read(c2, restored_.base, [this](Context& c3, Bytes base) {
          Reader r{base}; restored_version_ = r.number(); restored_bytes_ = r.field(); r.finish();
          replay_index_ = 0; replay_next(c3);
        });
      });
    });
  }
  void replay_next(Context& ctx) {
    if (replay_index_ == restored_.operations.size()) {
      if (restored_version_ != restored_.version) throw std::runtime_error("incomplete fixture replay");
      ctx.note("retained.reconstructed", pack({{restored_root_, encode(restored_)}, {"bytes", restored_bytes_}}));
      auto done = std::move(restored_done_); done(ctx, std::move(restored_bytes_)); return;
    }
    if (codec_ != "xor-bytes-v1") throw std::runtime_error("raw checkpoint cannot replay operations");
    read(ctx, restored_.operations[replay_index_++], [this](Context& c, Bytes delta) {
      Reader r{delta}; auto version = r.number();
      if (version != restored_version_ + 1 || r.at + 1 != delta.size()) throw std::runtime_error("fixture delta sequence hole");
      for (char& byte : restored_bytes_) byte = static_cast<char>(static_cast<unsigned char>(byte) ^ static_cast<unsigned char>(delta[r.at]));
      restored_version_ = version; replay_next(c);
    });
  }
  void prepare_checkpoint(Context& ctx) {
    auto other = config_.root == Root::reader ? Root::replay : Root::reader;
    erase(ctx, root_name(other), [this](Context& c) {
      auto next = [this](Context& c2) {
        reconstruct(c2, "head", [this](Context& c3, Bytes bytes) {
          auto checkpoint = base_record(4, bytes);
          auto head = encode(Descriptor{4, "data/checkpoint/4", "data/codec/raw-v1", {}});
          if (config_.negative == Negative::publish_before_checkpoint) {
            write(c3, "head", head, [this, checkpoint = std::move(checkpoint)](Context& c4) {
              c4.timer(2'000'000, late_write, checkpoint); collect_before_reset(c4);
            });
          } else {
            write(c3, "data/checkpoint/4", std::move(checkpoint), [this, head = std::move(head)](Context& c4) {
              write(c4, "head", head, [this](Context& c5) { collect_before_reset(c5); });
            });
          }
        });
      };
      if (config_.negative == Negative::publish_before_checkpoint) erase(c, root_name(config_.root), std::move(next));
      else next(c);
    });
  }
  void collect_before_reset(Context& ctx) {
    collect(ctx, [](Context& c) { c.note("retained.collected", "before-reset"); });
  }
  void collect(Context& ctx, Done done) {
    collect_done_ = std::move(done); keep_.clear();
    read(ctx, "head", [this](Context& c, Bytes bytes) {
      keep_ = references(decode(bytes)); list_roots(c, "");
    });
  }
  void list_roots(Context& ctx, std::string after) {
    pending_ = [this](Context& c, const Event& event) {
      if (!status(c, event, "roots/")) return;
      c.note("retained.page", "roots/", event.keys.size());
      page_ = event.keys; next_page_ = event.next; page_index_ = 0; root_next(c);
    };
    ctx.list("roots/", std::move(after), 2, 128, io);
  }
  void root_next(Context& ctx) {
    if (page_index_ == page_.size()) {
      if (!next_page_.empty()) list_roots(ctx, next_page_); else list_data(ctx, "");
      return;
    }
    read(ctx, page_[page_index_++], [this](Context& c, Bytes bytes) {
      if (config_.negative != Negative::drop_live_root) {
        auto refs = references(decode(bytes)); keep_.insert(refs.begin(), refs.end());
      }
      root_next(c);
    });
  }
  void list_data(Context& ctx, std::string after) {
    pending_ = [this](Context& c, const Event& event) {
      if (!status(c, event, "data/")) return;
      c.note("retained.page", "data/", event.keys.size());
      page_ = event.keys; next_page_ = event.next; page_index_ = 0; data_next(c);
    };
    ctx.list("data/", std::move(after), 2, 128, io);
  }
  void data_next(Context& ctx) {
    while (page_index_ < page_.size()) {
      auto key = page_[page_index_++];
      if (!keep_.contains(key)) { erase(ctx, std::move(key), [this](Context& c) { data_next(c); }); return; }
    }
    if (!next_page_.empty()) list_data(ctx, next_page_);
    else { auto done = std::move(collect_done_); done(ctx); }
  }
};

} // namespace

std::vector<std::string> audit(const std::vector<Record>& records) {
  std::map<std::string, Bytes> inventory;
  std::map<OpId, std::pair<std::string, Bytes>> writes;
  std::map<std::pair<std::uint64_t, std::string>, Bytes> restored;
  std::set<std::string> errors;
  unsigned resets = 0;
  // Independent byte oracle: combine authored masks, not the model replay loop.
  auto expected = [](unsigned version) {
    unsigned char combined{};
    if (version > masks.size()) throw std::invalid_argument("bad oracle version");
    for (unsigned i = 0; i < version; ++i) combined ^= masks[i];
    Bytes bytes(1024, '\0');
    for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>((i & 255) ^ combined);
    return bytes;
  };
  for (const auto& record : records) {
    if (record.kind == "retained.write_intent") writes[record.tag] = unpack(record.detail).at(0);
    else if (record.kind == "storage.write") {
      auto intent = writes.find(record.operation);
      if (intent == writes.end() || intent->second.first != record.detail) { errors.insert("durable write lacks matching intent"); continue; }
      const auto& [key, bytes] = intent->second;
      if (key == "head") {
        auto root = decode(bytes);
        if (root.base == "data/checkpoint/4" && (!inventory.contains(root.base) || inventory.at(root.base) != base_record(root.version, expected(root.version))))
          errors.insert("head published before matching checkpoint durable");
      }
      inventory[key] = bytes;
    } else if (record.kind == "storage.erase") {
      if (record.detail.starts_with("data/")) {
        for (const auto& [key, bytes] : inventory) if (key == "head" || key.starts_with("roots/")) {
          auto root = decode(bytes);
          if (root.base == record.detail || root.codec == record.detail ||
              std::ranges::find(root.operations, record.detail) != root.operations.end())
            errors.insert("retired live dependency " + record.detail + " of " + key);
        }
      }
      inventory.erase(record.detail);
    } else if (record.kind == "host.reset") ++resets;
    else if (record.kind == "retained.first_access" && !resets) errors.insert("old access preceded device reset");
    else if (record.kind == "retained.reconstructed") {
      auto parts = unpack(record.detail);
      const auto& [key, encoded] = parts.at(0); const auto& bytes = parts.at(1).second;
      auto root = decode(encoded);
      if (!inventory.contains(key) || inventory.at(key) != encoded) errors.insert("reconstruction differs from durable root");
      if (bytes != expected(root.version)) errors.insert("reconstructed bytes differ from authored history");
      for (const auto& ref : references(root)) if (!inventory.contains(ref)) errors.insert("reconstruction used nondurable dependency");
      restored[{record.incarnation, key}] = bytes;
    } else if (record.kind == "retained.old_complete") {
      bool match = false;
      for (const auto& [identity, bytes] : restored) if (identity.first == record.incarnation && identity.second.starts_with("roots/") && bytes == record.detail) match = true;
      if (!match) errors.insert("old completion lacks same-incarnation root reconstruction");
    } else if (record.kind == "retained.checkpoint_complete") {
      if (restored[{record.incarnation, "head"}] != record.detail) errors.insert("checkpoint completion lacks reconstruction");
    } else if (record.kind == "retained.unavailable") errors.insert("required reconstruction unavailable: " + record.detail);
    else if (record.kind == "retained.page" && record.tag > 2) errors.insert("directory page exceeded bound");
  }
  return {errors.begin(), errors.end()};
}

Result run_retained_view(const Case& config, Options options) {
  if (config.negative != Negative::none && config.incident != Incident::none)
    throw std::invalid_argument("combine negative and incident only after defining the fault intent");
  Simulation simulation(options);
  Host machine; machine.id = host; machine.memory_bytes = config.memory_bytes;
  machine.storage_bytes = config.storage_bytes; machine.disk.latency_ns = config.disk_ns;
  simulation.add_host(machine); simulation.add_process(process, host);
  simulation.add_actor(owner, process, [config] { return std::make_unique<Owner>(config); });
  Result result;
  enum class Pause { none, checkpoint_device, checkpoint_process, before_old_read, after_old_release };
  Pause reason{Pause::none};
  bool collected = false, old_released = false;
  simulation.observe([&](const Record& record) {
    if (record.kind.starts_with("retained.") || record.kind.starts_with("storage.") ||
        record.kind.starts_with("buffer.") || record.kind.starts_with("process.") ||
        record.kind.starts_with("host.") || record.kind == "operation.refused") result.evidence.push_back(record);
    if (!result.incident_exercised && config.incident != Incident::none &&
        ((config.incident == Incident::checkpoint_device_reset && record.kind == "storage.write" && record.detail == "data/checkpoint/4") ||
         (config.incident == Incident::checkpoint_process_loss && record.kind == "retained.write_intent" && unpack(record.detail).at(0).first == "data/checkpoint/4"))) {
      result.incident_exercised = true;
      reason = config.incident == Incident::checkpoint_device_reset ? Pause::checkpoint_device : Pause::checkpoint_process;
      simulation.pause();
    }
    if (record.kind == "retained.collected" && record.detail == "before-reset") {
      collected = true; reason = Pause::before_old_read; simulation.pause();
    }
    if (record.kind == "retained.collected" && record.detail == "after-release") {
      old_released = true; reason = Pause::after_old_release; simulation.pause();
    }
    if (record.kind == "retained.old_complete") result.old_read_complete = true;
    if (record.kind == "retained.checkpoint_complete") result.checkpoint_read_complete = true;
    if (record.kind == "host.reset") ++result.resets;
  });
  simulation.start(); simulation.inject(1000, owner, initialize, pack(initial_records()));
  for (;;) {
    auto run = simulation.run(config.until_ns);
    result.execution.events += run.events;
    result.execution.budget_exhausted = run.budget_exhausted;
    result.execution.pending = run.pending; result.execution.paused = run.paused;
    if (!run.paused) break;
    const auto incident = reason; reason = Pause::none;
    if (incident == Pause::none) throw std::logic_error("unexplained retained-view pause");
    // Pause occurs after the current atomic event and before another dispatch,
    // making persistence-before-callback faults exact under either scheduler.
    if (incident == Pause::checkpoint_process) {
      simulation.crash(process); result.bytes_after_process_loss = simulation.usage(host).memory;
    } else simulation.power_loss(host);
    simulation.at(simulation.now() + 10'000, "retained owner restart", [](Simulation& world) { world.restart(process); });
    const auto tag = incident == Pause::before_old_read ? delayed_read :
        incident == Pause::after_old_release ? verify_head : prepare;
    simulation.inject(simulation.now() + 20'000, owner, tag);
  }
  simulation.finish_replay();
  result.usage = simulation.usage(host); result.trace_hash = simulation.trace_hash();
  result.violations = audit(result.evidence);
  if (!collected) result.missing_milestones.push_back("collection before old access");
  if (!result.old_read_complete) result.missing_milestones.push_back("old root reconstruction");
  if (config.negative == Negative::none && !old_released) result.missing_milestones.push_back("old dependencies retired");
  if (config.negative == Negative::none && !result.checkpoint_read_complete) result.missing_milestones.push_back("checkpoint reconstruction after reset");
  if (config.incident != Incident::none && !result.incident_exercised) result.missing_milestones.push_back("requested checkpoint incident");
  return result;
}

} // namespace sixdb::sim::retained_view
