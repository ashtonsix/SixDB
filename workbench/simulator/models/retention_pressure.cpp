#include "retention_pressure.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace sixdb::sim::retention_pressure {
namespace {
constexpr ActorId source = 1, point = 2, pressure = 3, source_client = 10, point_client = 11,
                  reader = 12, replay = 13;
constexpr HostId source_host = 1, client_host = 2;
constexpr std::uint64_t initialize = 1, offer = 2, submit = 3, acknowledged = 4,
    ask = 5, answer = 6, begin = 7, cursor_ack = 8, close_reader = 9,
    retry_io = 20, io = 21, retry_wire = 22, release_pressure = 23;
constexpr std::uint64_t workspace_bytes = 1536, read_bytes = 264;

void require(bool yes, std::string_view why) { if (!yes) throw std::runtime_error(std::string(why)); }
void integer(Bytes& bytes, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) bytes += static_cast<char>(value >> (i * 8));
}
std::uint64_t integer(std::string_view bytes, std::size_t& at) {
  require(at <= bytes.size() && bytes.size() - at >= 8, "truncated retention value");
  std::uint64_t result{};
  for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(static_cast<unsigned char>(bytes[at++])) << (i * 8);
  return result;
}
struct Packet { std::uint64_t sequence{}; std::string key; Bytes value; };
Bytes encode(const Packet& p) {
  Bytes out; integer(out, p.sequence); integer(out, p.key.size()); out += p.key; out += p.value; return out;
}
Packet packet(std::string_view bytes) {
  std::size_t at{}; Packet p; p.sequence = integer(bytes, at); auto length = integer(bytes, at);
  require(length <= bytes.size() - at, "truncated retention key");
  p.key = bytes.substr(at, length); at += length; p.value = bytes.substr(at); return p;
}
Bytes value(std::uint64_t sequence, std::size_t size = 256) {
  Bytes result(size, '\0');
  for (std::size_t i = 0; i < size; ++i) result[i] = static_cast<char>((i + sequence * 13) & 255);
  return result;
}
Bytes row(std::uint64_t sequence, std::string_view bytes) { Bytes out; integer(out, sequence); out += bytes; return out; }
std::pair<std::uint64_t, Bytes> row(std::string_view bytes) {
  std::size_t at{}; auto sequence = integer(bytes, at); return {sequence, Bytes(bytes.substr(at))};
}
std::string operation_key(std::uint64_t sequence) {
  std::ostringstream out; out << "data/op/" << std::setw(4) << std::setfill('0') << sequence; return out.str();
}
std::string checkpoint_key(std::uint64_t sequence) {
  std::ostringstream out; out << "data/checkpoint/" << std::setw(4) << std::setfill('0') << sequence; return out.str();
}
std::uint64_t suffix(std::string_view key) {
  return std::stoull(std::string(key.substr(key.rfind('/') + 1)));
}
struct Manifest {
  std::uint64_t latest{2}, checkpoint{2}, replay_cursor{}, target{26};
  bool reader_active{true};
};
Bytes encode(const Manifest& m) {
  Bytes out; for (auto n : {m.latest, m.checkpoint, m.replay_cursor, m.target, std::uint64_t(m.reader_active)}) integer(out, n); return out;
}
Manifest manifest(std::string_view bytes) {
  std::size_t at{}; Manifest m; m.latest = integer(bytes, at); m.checkpoint = integer(bytes, at);
  m.replay_cursor = integer(bytes, at); m.target = integer(bytes, at); m.reader_active = integer(bytes, at);
  require(at == bytes.size() && m.replay_cursor <= m.latest && m.checkpoint <= m.latest, "invalid retention manifest"); return m;
}

using Done = std::function<void(Context&, const Event&)>;
/// Local operation continuation; physical refusal never silently consumes it.
/// The model charges a fixed workspace for bookkeeping and explicit buffers for
/// queued payloads. This helper is not an environment subclass or a GC policy.
class Serial : public Actor {
 protected:
  struct OperationState {
    Operation operation{}; std::string key, after; Bytes bytes; Done done;
    std::optional<Buffer> buffer;
  };
  Case config_;
  std::optional<Buffer> workspace_;
  std::optional<OperationState> operation_;
  bool retry_pending_{}, lost_{};
  explicit Serial(Case config) : config_(config) {}
  bool boot(Context& ctx, std::uint64_t size) {
    workspace_ = ctx.allocate(Bytes(size, '\0'));
    if (!workspace_) { ctx.note("pressure.blocked", "workspace"); return false; }
    return true;
  }
  void perform(Context& ctx, Operation operation, std::string key, Done done,
               Bytes bytes = {}, std::string after = {}) {
    require(!operation_, "overlapping serial storage operation");
    operation_ = OperationState{operation, std::move(key), std::move(after), std::move(bytes), std::move(done), {}};
    issue(ctx);
  }
  void issue(Context& ctx) {
    require(operation_.has_value(), "retry lost operation");
    auto& op = *operation_;
    if (op.operation == Operation::write) {
      auto buffer = ctx.allocate(op.bytes);
      if (!buffer) { refused(ctx); return; }
      op.buffer = *buffer;
      auto id = ctx.write(op.key, *buffer, io);
      ctx.release(*buffer); op.buffer.reset();
      ctx.note("pressure.write_intent", encode(Packet{0, op.key, op.bytes}), id);
    } else if (op.operation == Operation::read) {
      auto limit = op.key == "manifest" || op.key == "current" ? 64U :
          op.key == "data/codec" ? 32U : op.key == "cursor" ? 320U : read_bytes;
      ctx.read(op.key, limit, io);
    }
    else if (op.operation == Operation::erase) ctx.erase(op.key, io);
    else if (op.operation == Operation::list) ctx.list(op.key, op.after, 4, 128, io);
    else throw std::logic_error("unsupported serial port");
  }
  void refused(Context& ctx) {
    ctx.note("pressure.refused", operation_->key, static_cast<std::uint64_t>(operation_->operation));
    if (ctx.self() == source && operation_->operation == Operation::read &&
        operation_->key == "data/base/0" && config_.negative == Negative::lose_retry) {
      lost_ = true; ctx.note("pressure.lost_continuation", operation_->key); return;
    }
    if (!retry_pending_) { retry_pending_ = true; ctx.timer(config_.retry_ns, retry_io); }
  }
  bool consume(Context& ctx, const Event& event) {
    if (event.kind == EventKind::timer && event.tag == retry_io) {
      retry_pending_ = false; if (operation_ && !lost_) issue(ctx); return true;
    }
    if (event.kind != EventKind::completion || event.tag != io) return false;
    require(operation_.has_value(), "completion without serial operation");
    if (event.status == Status::capacity) { refused(ctx); return true; }
    auto op = std::move(*operation_); operation_.reset();
    if (event.operation == Operation::read && event.status == Status::ok)
      ctx.note("pressure.read_result", encode(Packet{0, op.key, event.bytes}), event.id);
    op.done(ctx, event); return true;
  }
};

class Source final : public Serial {
  struct Command { ActorId from; std::uint64_t tag; Packet packet; Buffer buffer; };
  std::deque<Command> queue_;
  std::optional<Command> active_;
  Manifest state_;
  bool ready_{}, initializing_{};
  std::vector<std::pair<std::string, Bytes>> initial_;
  std::size_t initial_index_{};
  std::vector<std::string> keys_;
  std::size_t key_index_{};
  std::string cursor_;
  std::function<void(Context&)> collection_done_;
  Bytes recovered_current_;
  std::uint64_t recovered_version_{};
 public:
  explicit Source(Case config) : Serial(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::boot) {
      if (!boot(ctx, workspace_bytes)) return;
      perform(ctx, Operation::read, "manifest", [this](Context& c, const Event& e) {
        if (e.status == Status::ok) { state_ = manifest(e.bytes); recover_current(c); }
        else { require(e.status == Status::missing, "source root recovery failed"); c.note("pressure.source_empty"); }
      }); return;
    }
    if (consume(ctx, event)) return;
    if (event.kind != EventKind::message) return;
    if (event.tag == initialize) {
      require(!ready_ && !initializing_ && !operation_, "initialization overlapped recovery");
      state_.target = row(event.bytes).first; initializing_ = true;
      // Authored initialization bytes arrive once, not in restart factories.
      auto bytes = row(event.bytes).second;
      std::size_t at{}; auto count = integer(std::string_view(bytes), at);
      for (std::uint64_t i = 0; i < count; ++i) {
        auto length = integer(std::string_view(bytes), at); require(length <= bytes.size() - at, "bad initial record");
        auto p = packet(std::string_view(bytes).substr(at, length)); at += length;
        initial_.emplace_back(std::move(p.key), std::move(p.value));
      }
      require(at == bytes.size(), "extra initial bytes"); initialize_next(ctx); return;
    }
    if (event.tag != submit && event.tag != ask && event.tag != cursor_ack && event.tag != close_reader) return;
    auto p = packet(event.bytes);
    if ((event.tag == submit && event.from != source_client) ||
        (event.tag == cursor_ack && event.from != replay) ||
        (event.tag == close_reader && event.from != reader) ||
        (event.tag == ask && event.from != reader && event.from != replay)) return;
    auto same = [&](const Command& c) { return c.from == event.from && c.tag == event.tag && c.packet.sequence == p.sequence && c.packet.key == p.key; };
    if ((active_ && same(*active_)) || std::ranges::any_of(queue_, same)) return;
    auto retained = ctx.allocate(event.bytes);
    if (!retained) { ctx.note("pressure.queue_refused", "source"); return; }
    queue_.push_back(Command{event.from, event.tag, std::move(p), *retained}); pump(ctx);
  }
 private:
  void recover_current(Context& ctx) {
    perform(ctx, Operation::read, "data/codec", [this](Context& c, const Event& e) {
      require(e.status == Status::ok && e.bytes == "replace-v1", "recovery decoder unavailable");
      perform(c, Operation::read, checkpoint_key(state_.checkpoint), [this](Context& c2, const Event& checkpoint) {
        require(checkpoint.status == Status::ok, "recovery checkpoint unavailable");
        auto [sequence, bytes] = row(checkpoint.bytes); require(sequence == state_.checkpoint, "checkpoint version differs from root");
        recovered_version_ = sequence; recovered_current_ = std::move(bytes); recover_next(c2);
      });
    });
  }
  void recover_next(Context& ctx) {
    if (recovered_version_ == state_.latest) {
      ready_ = true; ctx.note("pressure.current_recovered", row(recovered_version_, recovered_current_));
      ctx.note("pressure.source_recovered", encode(state_)); pump(ctx); return;
    }
    perform(ctx, Operation::read, operation_key(recovered_version_ + 1), [this](Context& c, const Event& e) {
      require(e.status == Status::ok, "recovery operation unavailable");
      auto [sequence, bytes] = row(e.bytes); require(sequence == recovered_version_ + 1, "recovery operation gap");
      recovered_version_ = sequence; recovered_current_ = std::move(bytes); recover_next(c);
    });
  }
  void initialize_next(Context& ctx) {
    if (initial_index_ != initial_.size()) {
      auto [key, bytes] = initial_[initial_index_++];
      perform(ctx, Operation::write, std::move(key), [this](Context& c, const Event& e) {
        require(e.status == Status::ok, "initialize write failed"); initialize_next(c);
      }, std::move(bytes)); return;
    }
    initial_.clear();
    save_manifest(ctx, [this](Context& c) { ready_ = true; c.note("pressure.source_initialized", encode(state_)); pump(c); });
  }
  void save_manifest(Context& ctx, std::function<void(Context&)> done) {
    perform(ctx, Operation::write, "manifest", [done = std::move(done)](Context& c, const Event& e) {
      require(e.status == Status::ok, "manifest write failed"); done(c);
    }, encode(state_));
  }
  void finish(Context& ctx, bool ack = true) {
    require(active_.has_value(), "finished missing source command");
    if (ack) ctx.send(active_->from, acknowledged, encode(active_->packet));
    ctx.release(active_->buffer); active_.reset(); pump(ctx);
  }
  void pump(Context& ctx) {
    if (!ready_ || active_ || operation_ || queue_.empty()) return;
    active_ = std::move(queue_.front()); queue_.pop_front();
    const auto p = active_->packet;
    if (active_->tag == submit) {
      if (p.sequence <= state_.latest) { finish(ctx); return; }
      require(p.sequence == state_.latest + 1 && p.sequence <= state_.target && p.value.size() == 256, "source update sequence changed");
      perform(ctx, Operation::write, operation_key(p.sequence), [this, sequence = p.sequence](Context& c, const Event& e) {
        require(e.status == Status::ok, "source value write failed"); state_.latest = sequence;
        save_manifest(c, [this](Context& c2) {
          c2.note("pressure.source_commit", {}, state_.latest);
          if ((state_.latest - 2) % 4 == 0 || state_.latest == state_.target) checkpoint(c2);
          else finish(c2);
        });
      }, row(p.sequence, p.value));
    } else if (active_->tag == close_reader) {
      if (!state_.reader_active) { finish(ctx); return; }
      state_.reader_active = false;
      save_manifest(ctx, [this](Context& c) { c.note("pressure.reader_root_released");
        collect(c, [this](Context& c2) { finish(c2); }); });
    } else if (active_->tag == cursor_ack) {
      require(p.sequence <= state_.latest, "replay cursor ahead of source");
      if (p.sequence <= state_.replay_cursor) { finish(ctx); return; }
      state_.replay_cursor = p.sequence;
      save_manifest(ctx, [this](Context& c) { c.note("pressure.replay_root_advanced", {}, state_.replay_cursor);
        collect(c, [this](Context& c2) { finish(c2); }); });
    } else {
      if (p.key == "root/reader" || p.key == "root/replay") {
        bool valid = (active_->from == reader && p.key == "root/reader" && state_.reader_active) ||
                     (active_->from == replay && p.key == "root/replay");
        auto response = Packet{p.sequence, p.key, valid ? encode(state_) : Bytes{}};
        ctx.send(active_->from, answer, encode(response)); finish(ctx, false); return;
      }
      bool allowed = p.key == "data/codec" || p.key == "data/base/0" || p.key.starts_with("data/op/");
      if (active_->from == reader) allowed = allowed && state_.reader_active &&
          (!p.key.starts_with("data/op/") || suffix(p.key) <= 2);
      else allowed = allowed && (!p.key.starts_with("data/op/") || suffix(p.key) <= state_.latest);
      require(allowed, "query escaped fixture context");
      if (active_->from == reader && p.key == "data/base/0") ctx.note("pressure.old_first_access", encode(state_));
      perform(ctx, Operation::read, p.key, [this, p](Context& c, const Event& e) {
        require(e.status == Status::ok || e.status == Status::missing, "source query failed");
        c.send(active_->from, answer, encode(Packet{p.sequence, p.key, e.status == Status::ok ? e.bytes : Bytes{}}));
        finish(c, false);
      });
    }
  }
  void checkpoint(Context& ctx) {
    perform(ctx, Operation::read, operation_key(state_.latest), [this](Context& c, const Event& e) {
      require(e.status == Status::ok, "checkpoint source missing");
      auto sequence = state_.latest;
      perform(c, Operation::write, checkpoint_key(sequence), [this, sequence](Context& c2, const Event& saved) {
        require(saved.status == Status::ok, "checkpoint write failed"); state_.checkpoint = sequence;
        save_manifest(c2, [this](Context& c3) { collect(c3, [this](Context& c4) {
          c4.note("pressure.checkpoint_finished", {}, state_.latest); finish(c4);
        }); });
      }, e.bytes);
    });
  }
  bool needed(std::string_view key) const {
    if (key == "data/codec") return true;
    if (key == "data/base/0") return state_.reader_active ||
        (config_.negative != Negative::skip_replay_root && state_.replay_cursor == 0);
    if (key.starts_with("data/checkpoint/")) return suffix(key) == state_.checkpoint;
    if (key.starts_with("data/op/")) {
      auto sequence = suffix(key);
      return sequence > state_.checkpoint || (state_.reader_active && sequence <= 2) ||
          (config_.negative != Negative::skip_replay_root && sequence > state_.replay_cursor);
    }
    return false;
  }
  void collect(Context& ctx, std::function<void(Context&)> done) {
    collection_done_ = std::move(done); list(ctx, "");
  }
  void list(Context& ctx, std::string after) {
    perform(ctx, Operation::list, "data/", [this](Context& c, const Event& e) {
      require(e.status == Status::ok, "collection directory failed");
      c.note("pressure.directory_page", {}, e.keys.size()); keys_ = e.keys; cursor_ = e.next; key_index_ = 0; sweep(c);
    }, {}, std::move(after));
  }
  void sweep(Context& ctx) {
    while (key_index_ < keys_.size()) {
      auto key = keys_[key_index_++];
      if (!needed(key)) {
        perform(ctx, Operation::erase, key, [this](Context& c, const Event& e) {
          require(e.status == Status::ok || e.status == Status::missing, "retirement failed"); sweep(c);
        }); return;
      }
    }
    if (!cursor_.empty()) list(ctx, cursor_);
    else { auto done = std::move(collection_done_); done(ctx); }
  }
};

class Client final : public Actor {
  Case config_; ActorId destination_;
  std::deque<std::pair<Packet, Buffer>> offers_;
  bool retry_{};
 public:
  Client(Case config, ActorId destination) : config_(config), destination_(destination) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::message && event.tag == offer) {
      auto p = packet(event.bytes); ctx.note("pressure.arrived", {}, p.sequence);
      auto bytes = ctx.allocate(event.bytes); require(bytes.has_value(), "authored client input budget too small");
      offers_.emplace_back(std::move(p), *bytes); send(ctx);
    } else if (event.kind == EventKind::message && event.tag == acknowledged && event.from == destination_) {
      auto p = packet(event.bytes);
      if (!offers_.empty() && p.sequence == offers_.front().first.sequence) {
        ctx.note("pressure.completed", {}, p.sequence); ctx.release(offers_.front().second); offers_.pop_front(); send(ctx);
      }
    } else if (event.kind == EventKind::timer && event.tag == retry_wire) { retry_ = false; send(ctx); }
  }
 private:
  void send(Context& ctx) {
    if (offers_.empty()) return;
    ctx.send(destination_, submit, offers_.front().second);
    if (!retry_) { retry_ = true; ctx.timer(config_.retry_ns * 2, retry_wire); }
  }
};

class Point final : public Serial {
  bool ready_{}; std::uint64_t latest_{};
  std::optional<std::pair<Packet, Buffer>> active_;
 public:
  explicit Point(Case config) : Serial(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::boot) {
      if (!boot(ctx, 128)) return;
      perform(ctx, Operation::read, "current", [this](Context& c, const Event& e) {
        require(e.status == Status::missing || e.status == Status::ok, "point recovery failed");
        if (e.status == Status::ok) latest_ = row(e.bytes).first;
        ready_ = true; c.note("pressure.point_recovered", {}, latest_);
      }); return;
    }
    if (consume(ctx, event)) return;
    if (!ready_ || event.kind != EventKind::message || event.tag != submit || event.from != point_client) return;
    auto p = packet(event.bytes);
    if (p.sequence <= latest_) { ctx.send(point_client, acknowledged, event.bytes); return; }
    if (active_) return;
    require(p.sequence == latest_ + 1, "point input gap");
    auto retained = ctx.allocate(event.bytes); if (!retained) return;
    active_ = {p, *retained};
    perform(ctx, Operation::write, "current", [this](Context& c, const Event& e) {
      require(e.status == Status::ok, "point write failed"); latest_ = active_->first.sequence;
      c.send(point_client, acknowledged, encode(active_->first)); c.release(active_->second); active_.reset();
    }, row(p.sequence, p.value));
  }
};

class Pressure final : public Actor {
  Case config_; std::optional<Buffer> held_; bool active_{};
 public:
  explicit Pressure(Case config) : config_(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.tag == begin || (event.kind == EventKind::timer && event.tag == retry_wire)) {
      if (active_) return;
      held_ = ctx.allocate(Bytes(config_.memory_bytes - 1856, '\0'));
      if (!held_) { ctx.timer(config_.retry_ns, retry_wire); return; }
      active_ = true; ctx.note("pressure.held"); ctx.timer(config_.pressure_ns, release_pressure);
    } else if (event.kind == EventKind::timer && event.tag == release_pressure) {
      ctx.release(*held_); held_.reset(); ctx.note("pressure.released");
    }
  }
};

class Reader final : public Serial {
  bool active_{}, closing_{}, ended_{}; Packet query_;
  Bytes bytes_; std::uint64_t next_{}, target_{};
 public:
  explicit Reader(Case config) : Serial(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::boot) { boot(ctx, 512); return; }
    if (event.kind == EventKind::message && event.tag == begin && !active_) {
      active_ = true; ctx.note("pressure.arrived"); query(ctx, "root/reader");
    } else if (event.kind == EventKind::timer && event.tag == retry_wire && active_ && !ended_) {
      transmit(ctx);
    } else if (event.kind == EventKind::message && event.tag == answer && event.from == source && active_ && !closing_) {
      auto p = packet(event.bytes); if (p.sequence != query_.sequence || p.key != query_.key) return;
      if (p.value.empty()) { ended_ = true; ctx.note("pressure.failed", p.key); return; }
      if (p.key == "root/reader") { target_ = 2; require(manifest(p.value).reader_active, "closed reader grant"); query(ctx, "data/codec"); }
      else if (p.key == "data/codec") { require(p.value == "replace-v1", "unsupported source decoder"); query(ctx, "data/base/0"); }
      else {
        auto [sequence, bytes] = row(p.value); require(sequence == next_, "reader replay sequence changed"); bytes_ = std::move(bytes);
        if (next_ < target_) { ++next_; query(ctx, operation_key(next_)); }
        else { ctx.note("pressure.old_bytes", bytes_); closing_ = true; transmit(ctx); }
      }
    } else if (event.kind == EventKind::message && event.tag == acknowledged && event.from == source && closing_ && !ended_) {
      ended_ = true; ctx.note("pressure.completed");
    }
  }
 private:
  void query(Context& ctx, std::string key) { ++query_.sequence; query_.key = std::move(key); transmit(ctx); }
  void transmit(Context& ctx) {
    if (closing_) ctx.send(source, close_reader, encode(Packet{})); else ctx.send(source, ask, encode(query_));
    ctx.timer(config_.retry_ns * 2, retry_wire);
  }
};

class Replay final : public Serial {
  bool active_{}, ended_{}, cursor_present_{}, awaiting_ack_{};
  Packet query_;
  std::uint64_t cursor_{}, target_{};
  Bytes bytes_;
 public:
  explicit Replay(Case config) : Serial(config) {}
  void receive(Context& ctx, const Event& event) override {
    if (event.kind == EventKind::boot) {
      if (!boot(ctx, 512)) return;
      perform(ctx, Operation::read, "cursor", [this](Context& c, const Event& e) {
        require(e.status == Status::missing || e.status == Status::ok, "replay cursor recovery failed");
        if (e.status == Status::ok) { auto p = packet(e.bytes); cursor_ = p.sequence; target_ = std::stoull(p.key); bytes_ = p.value;
          cursor_present_ = true; active_ = true; awaiting_ack_ = true; c.note("pressure.cursor_recovered", {}, cursor_); transmit(c); }
      }); return;
    }
    if (consume(ctx, event)) return;
    if (event.kind == EventKind::message && event.tag == begin && !active_) {
      active_ = true; ctx.note("pressure.arrived"); query(ctx, "root/replay");
    } else if (event.kind == EventKind::timer && event.tag == retry_wire && active_ && !ended_ && !operation_) transmit(ctx);
    else if (event.kind == EventKind::message && event.tag == answer && event.from == source && active_ && !awaiting_ack_ && !operation_ && !ended_) {
      auto p = packet(event.bytes); if (p.sequence != query_.sequence || p.key != query_.key) return;
      if (p.value.empty()) { ended_ = true; ctx.note("pressure.failed", p.key); return; }
      if (p.key == "root/replay") { auto root = manifest(p.value); target_ = root.target;
        require(root.latest == target_, "replay started before authored write stream completed"); query(ctx, "data/codec"); }
      else if (p.key == "data/codec") { require(p.value == "replace-v1", "unsupported replay decoder"); query(ctx, "data/base/0"); }
      else {
        auto [sequence, bytes] = row(p.value); require(sequence == (cursor_present_ ? cursor_ + 1 : 0), "replay consumed a gap");
        cursor_ = sequence; bytes_ = std::move(bytes); cursor_present_ = true;
        perform(ctx, Operation::write, "cursor", [this](Context& c, const Event& e) {
          require(e.status == Status::ok, "cursor persistence failed"); awaiting_ack_ = true; c.note("pressure.cursor_durable", {}, cursor_); transmit(c);
        }, encode(Packet{cursor_, std::to_string(target_), bytes_}));
      }
    } else if (event.kind == EventKind::message && event.tag == acknowledged && event.from == source && awaiting_ack_ && !operation_ && !ended_) {
      auto p = packet(event.bytes); if (p.sequence != cursor_) return;
      awaiting_ack_ = false;
      if (cursor_ == target_) { ended_ = true; ctx.note("pressure.replay_bytes", bytes_); ctx.note("pressure.completed"); }
      else query(ctx, operation_key(cursor_ + 1));
    }
  }
 private:
  void query(Context& ctx, std::string key) { ++query_.sequence; query_.key = std::move(key); transmit(ctx); }
  void transmit(Context& ctx) {
    if (awaiting_ack_) ctx.send(source, cursor_ack, encode(Packet{cursor_, {}, {}})); else ctx.send(source, ask, encode(query_));
    ctx.timer(config_.retry_ns * 2, retry_wire);
  }
};

struct Observer {
  struct Read { ActorId actor; std::uint64_t incarnation; std::string key; Bytes bytes; };
  const Case& config;
  Result result;
  std::map<std::pair<ActorId, std::string>, Bytes> durable;
  std::map<OpId, std::pair<ActorId, Packet>> writes;
  std::map<OpId, Read> reads;
  std::map<std::tuple<ActorId, std::uint64_t, std::string>, std::pair<std::uint64_t, Bytes>> observed_reads;
  std::map<ActorId, std::set<std::uint64_t>> arrivals, completions;
  std::set<std::string> errors;
  std::set<std::uint64_t> durable_source_writes;
  bool pressure_held{}, first_access{};
  bool old_bytes{}, replay_bytes{};
  explicit Observer(const Case& c) : config(c) {}
  void see(const Record& record) {
    if (record.kind == "pressure.write_intent") writes[record.tag] = {record.actor, packet(record.detail)};
    else if (record.kind == "storage.write") {
      auto it = writes.find(record.operation);
      require(it != writes.end() && it->second.first == record.actor && it->second.second.key == record.detail, "physical write lacks its modeled input");
      const auto& bytes = it->second.second.value;
      if (record.actor == source && record.detail == "manifest") {
        auto root = manifest(bytes);
        if (!durable.contains({source, checkpoint_key(root.checkpoint)}) ||
            durable.at({source, checkpoint_key(root.checkpoint)}) != row(root.checkpoint, value(root.checkpoint)))
          errors.insert("published checkpoint bytes are not durable");
        if (root.replay_cursor && (!durable.contains({replay, "cursor"}) || packet(durable.at({replay, "cursor"})).sequence < root.replay_cursor))
          errors.insert("source released replay root before consumer cursor durable");
      }
      if (record.actor == source && record.detail.starts_with("data/op/")) {
        auto sequence = suffix(record.detail);
        if (bytes != row(sequence, value(sequence))) errors.insert("persisted source write differs from authored bytes");
        else durable_source_writes.insert(sequence);
      }
      if (record.actor == replay && record.detail == "cursor") {
        auto cursor = packet(bytes);
        if (cursor.value != value(cursor.sequence)) errors.insert("replay cursor bytes differ from authored operations");
        if ((!durable.contains({replay, "cursor"}) && cursor.sequence != 0) ||
            (durable.contains({replay, "cursor"}) && cursor.sequence != packet(durable.at({replay, "cursor"})).sequence + 1))
          errors.insert("replay cursor skipped an operation");
      }
      durable[{record.actor, record.detail}] = bytes;
    } else if (record.kind == "storage.erase") {
      auto identity = std::pair{record.actor, record.detail};
      if (record.actor == source && durable.contains({source, "manifest"})) {
        auto root = manifest(durable.at({source, "manifest"}));
        auto k = std::string_view(record.detail);
        bool needed = k == "data/codec" || k == checkpoint_key(root.checkpoint);
        if (k == "data/base/0") needed = needed || root.reader_active || root.replay_cursor == 0;
        if (k.starts_with("data/op/")) {
          auto seq = suffix(k); needed = needed || seq > root.checkpoint ||
              (root.reader_active && seq <= 2) || seq > root.replay_cursor;
        }
        if (needed) errors.insert("retired live root dependency: " + record.detail);
      }
      if (durable.contains(identity)) result.reclaimed_bytes += durable.at(identity).size();
      durable.erase(identity);
    } else if (record.kind == "storage.read") {
      auto key = std::pair{record.actor, record.detail};
      if (durable.contains(key)) reads.emplace(record.operation, Read{record.actor, record.incarnation, record.detail, durable.at(key)});
    } else if (record.kind == "pressure.read_result") {
      auto p = packet(record.detail); auto key = std::pair{record.actor, p.key};
      if (!durable.contains(key) || durable.at(key) != p.value) errors.insert("read result lacks matching durable bytes");
      auto receipt = reads.find(record.tag);
      if (receipt == reads.end() || receipt->second.actor != record.actor || receipt->second.incarnation != record.incarnation ||
          receipt->second.key != p.key || receipt->second.bytes != p.value) {
        errors.insert("read result lacks matching physical read receipt");
      } else {
        observed_reads[{record.actor, record.incarnation, p.key}] = {record.id, p.value};
        reads.erase(receipt);
      }
    } else if (record.kind == "pressure.old_first_access") {
      first_access = true; auto root = manifest(record.detail);
      if (record.incarnation < 2 || root.latest < 10) errors.insert("old first access preceded restart/eight overwrites");
    } else if (record.kind == "pressure.current_recovered") {
      auto root = manifest(durable.at({source, "manifest"})); auto [sequence, bytes] = row(record.detail);
      if (sequence != root.latest || bytes != value(sequence)) errors.insert("recovered current bytes differ from durable root");
      std::uint64_t preceding{};
      auto required = [&](const std::string& key, const Bytes& expected) {
        auto it = observed_reads.find({source, record.incarnation, key});
        if (record.actor != source || it == observed_reads.end() || it->second.first <= preceding || it->second.second != expected)
          errors.insert("current recovery lacks same-incarnation read chain");
        else preceding = it->second.first;
      };
      required("manifest", encode(root)); required("data/codec", "replace-v1");
      required(checkpoint_key(root.checkpoint), row(root.checkpoint, value(root.checkpoint)));
      for (auto seq = root.checkpoint + 1; seq <= root.latest; ++seq) required(operation_key(seq), row(seq, value(seq)));
      if (!root.reader_active && root.replay_cursor == root.target) result.current_recovered_after_gc = true;
    } else if (record.kind == "pressure.old_bytes") {
      old_bytes = record.detail == value(2); if (!old_bytes) errors.insert("old reader returned wrong cut");
    } else if (record.kind == "pressure.replay_bytes") {
      replay_bytes = record.detail == value(config.writes + 2); if (!replay_bytes) errors.insert("replay returned wrong final bytes");
    } else if (record.kind == "pressure.arrived") {
      if (!arrivals[record.actor].insert(record.tag).second) errors.insert("duplicate offer arrival");
    }
    else if (record.kind == "pressure.completed") {
      if (record.actor == source_client) {
        if (!durable.contains({source, "manifest"}) || manifest(durable.at({source, "manifest"})).latest < record.tag ||
            !durable_source_writes.contains(record.tag))
          errors.insert("source completion lacks durable authored write");
      } else if (record.actor == point_client) {
        if (!durable.contains({point, "current"}) || row(durable.at({point, "current"})).first < record.tag ||
            row(durable.at({point, "current"})).second != value(row(durable.at({point, "current"})).first, 16))
          errors.insert("point completion lacks durable authored write");
      } else if (record.actor == reader) {
        if (!old_bytes || !durable.contains({source, "manifest"}) || manifest(durable.at({source, "manifest"})).reader_active)
          errors.insert("reader completion lacks bytes or durable root release");
      } else if (record.actor == replay) {
        if (!replay_bytes || !durable.contains({replay, "cursor"}) || packet(durable.at({replay, "cursor"})).sequence != config.writes + 2 ||
            !durable.contains({source, "manifest"}) || manifest(durable.at({source, "manifest"})).replay_cursor != config.writes + 2)
          errors.insert("replay completion lacks durable cursor release");
      }
      if (!arrivals[record.actor].contains(record.tag)) errors.insert("completion without offered work");
      if (record.actor == point_client && pressure_held) ++result.independent_during_pressure;
      if (!completions[record.actor].insert(record.tag).second) errors.insert("duplicate terminal completion");
    } else if (record.kind == "pressure.failed") {
      errors.insert("required read unavailable: " + record.detail);
      result.cohorts[record.actor == reader ? "old-reader" : "replay"].failed = 1;
    } else if (record.kind == "pressure.held") pressure_held = true;
    else if (record.kind == "pressure.released") { pressure_held = false; result.pressure_released = true; }
    else if (record.kind == "pressure.refused" && record.actor == source && record.detail == "data/base/0") ++result.reconstruction_refusals;
    else if (record.kind == "pressure.reader_root_released") result.reader_root_released = true;
    else if (record.kind == "pressure.replay_root_advanced") result.replay_root_advanced = true;
    else if (record.kind == "process.restart" && record.actor == source && record.incarnation > 1) ++result.source_restarts;
    else if (record.kind == "pressure.directory_page" && record.tag > 4) errors.insert("directory page exceeded bound");
  }
  Result finish(const Simulation& simulation, Run run) {
    for (auto [actor, cohort, offered] : {std::tuple{source_client, "source", std::uint64_t(config.writes)},
          std::tuple{point_client, "independent", std::uint64_t(config.writes)},
          std::tuple{reader, "old-reader", std::uint64_t(1)}, std::tuple{replay, "replay", std::uint64_t(1)}}) {
      auto& c = result.cohorts[cohort]; c.offered = offered; c.arrived = arrivals[actor].size();
      c.completed = completions[actor].size(); c.unfinished = offered - c.completed - c.failed;
    }
    result.violations = {errors.begin(), errors.end()}; result.source_usage = simulation.usage(source_host);
    result.trace_hash = simulation.trace_hash(); result.execution = run;
    if (!first_access) result.missing_incidents.push_back("late old-page access");
    if (!result.pressure_released) result.missing_incidents.push_back("transient pressure released");
    if (!result.reconstruction_refusals) result.missing_incidents.push_back("reconstruction actually refused under pressure");
    if (config.negative == Negative::none && !result.current_recovered_after_gc) result.missing_incidents.push_back("current checkpoint reconstructed after final retirement");
    return result;
  }
};
} // namespace

std::vector<std::string> audit(const Case& config, const std::vector<Record>& records) {
  Observer observer(config);
  for (const auto& record : records) observer.see(record);
  return {observer.errors.begin(), observer.errors.end()};
}

Result run_case(const Case& config, Options options, std::ostream* trace, std::function<void(const Record&)> inspect) {
  require(config.writes >= 8 && config.writes <= 24, "retention fixture supports 8..24 writes");
  require(config.memory_bytes >= 3072 && config.retry_ns && config.pressure_ns, "invalid pressure budget");
  Simulation simulation(options);
  Host storage; storage.id = source_host; storage.memory_bytes = config.memory_bytes;
  storage.storage_bytes = config.storage_bytes; storage.disk.latency_ns = config.disk_ns;
  simulation.add_host(storage);
  Host clients; clients.id = client_host; clients.memory_bytes = 128 * 1024; clients.storage_bytes = 64 * 1024;
  clients.disk.latency_ns = config.disk_ns; simulation.add_host(clients);
  simulation.add_link(Link{.from = source_host, .to = client_host});
  simulation.add_link(Link{.from = client_host, .to = source_host});
  for (auto actor : {source, point, pressure}) simulation.add_process(actor, source_host);
  for (auto actor : {source_client, point_client, reader, replay}) simulation.add_process(actor, client_host);
  simulation.add_actor(source, source, [config] { return std::make_unique<Source>(config); });
  simulation.add_actor(point, point, [config] { return std::make_unique<Point>(config); });
  simulation.add_actor(pressure, pressure, [config] { return std::make_unique<Pressure>(config); });
  simulation.add_actor(source_client, source_client, [config] { return std::make_unique<Client>(config, source); });
  simulation.add_actor(point_client, point_client, [config] { return std::make_unique<Client>(config, point); });
  simulation.add_actor(reader, reader, [config] { return std::make_unique<Reader>(config); });
  simulation.add_actor(replay, replay, [config] { return std::make_unique<Replay>(config); });
  Observer observer(config);
  enum class Stop { none, restart_source, reset_source, start_pressure, start_reader, start_replay, reset_replay, final_recovery };
  Stop stop{Stop::none};
  bool source_fault{}, pressure_started{}, reader_started{}, replay_started{}, replay_fault{}, final_recovery{};
  bool source_finished{}, reader_finished{}, initialized{};
  Bytes authored_initial;
  simulation.observe([&](const Record& record) {
    observer.see(record); if (trace) sixdb::sim::write_json(*trace, record); if (inspect) inspect(record);
    if (record.kind == "pressure.source_empty" && !initialized) {
      initialized = true; simulation.inject(record.time + 1, source, initialize, authored_initial);
    }
    if (!source_fault && ((config.incident == Incident::checkpoint_reset && record.kind == "storage.write" && record.actor == source && record.detail == checkpoint_key(10)) ||
        (config.incident != Incident::checkpoint_reset && record.kind == "pressure.checkpoint_finished" && record.tag == 10))) {
      source_fault = true; stop = config.incident == Incident::checkpoint_reset ? Stop::reset_source : Stop::restart_source; simulation.pause();
    }
    if (source_fault && !pressure_started && record.kind == "pressure.source_recovered" && record.incarnation >= 2) {
      pressure_started = true; stop = Stop::start_pressure; simulation.pause();
    }
    if (!reader_started && record.kind == "pressure.held") { reader_started = true; stop = Stop::start_reader; simulation.pause(); }
    if (record.kind == "pressure.completed" && record.actor == source_client && record.tag == config.writes + 2) source_finished = true;
    if (record.kind == "pressure.completed" && record.actor == reader) reader_finished = true;
    if (record.kind == "pressure.completed" && record.actor == replay && !final_recovery) {
      final_recovery = true; stop = Stop::final_recovery; simulation.pause();
    }
    if (!replay_started && source_finished && reader_finished) { replay_started = true; stop = Stop::start_replay; simulation.pause(); }
    if (config.incident == Incident::replay_cursor_reset && !replay_fault && record.kind == "storage.write" && record.actor == replay && record.detail == "cursor") {
      auto it = observer.writes.find(record.operation);
      if (it != observer.writes.end() && packet(it->second.second.value).sequence == 8) { replay_fault = true; stop = Stop::reset_replay; simulation.pause(); }
    }
  });
  simulation.start();
  std::vector<Packet> records{{0, "data/base/0", row(0, value(0))}, {0, "data/codec", "replace-v1"},
      {0, operation_key(1), row(1, value(1))}, {0, operation_key(2), row(2, value(2))},
      {0, checkpoint_key(2), row(2, value(2))}};
  Bytes initial; integer(initial, records.size());
  for (const auto& record : records) { auto encoded = encode(record); integer(initial, encoded.size()); initial += encoded; }
  authored_initial = row(config.writes + 2, initial);
  for (std::uint64_t i = 0; i < config.writes; ++i) {
    simulation.inject(50'000 + i * 10'000, source_client, offer, encode(Packet{i + 3, {}, value(i + 3)}));
    simulation.inject(60'000 + i * 30'000, point_client, offer, encode(Packet{i + 1, {}, value(i + 1, 16)}));
  }
  Run execution;
  for (;;) {
    auto run = simulation.run(config.until_ns, 500'000); execution.events += run.events;
    execution.budget_exhausted = run.budget_exhausted; execution.pending = run.pending; execution.paused = run.paused;
    if (!run.paused) break;
    auto reason = std::exchange(stop, Stop::none);
    switch (reason) {
      case Stop::restart_source:
      case Stop::final_recovery:
        simulation.crash(source); simulation.at(simulation.now() + 10'000, "source restart", [](Simulation& s) { s.restart(source); }); break;
      case Stop::reset_source:
        simulation.power_loss(source_host); simulation.at(simulation.now() + 10'000, "source-host restart", [](Simulation& s) { for (auto actor : {source, point, pressure}) s.restart(actor); }); break;
      case Stop::start_pressure: simulation.inject(simulation.now() + 1, pressure, begin); break;
      case Stop::start_reader: simulation.inject(simulation.now() + 1000, reader, begin); break;
      case Stop::start_replay: simulation.inject(simulation.now() + 1000, replay, begin); break;
      case Stop::reset_replay:
        simulation.crash(replay); simulation.at(simulation.now() + 10'000, "replay cursor restart", [](Simulation& s) { s.restart(replay); }); break;
      case Stop::none: throw std::logic_error("unexpected pressure experiment pause");
    }
  }
  simulation.finish_replay(); auto result = observer.finish(simulation, execution);
  if (!source_fault) result.missing_incidents.push_back("source restart after eight writes");
  if (config.incident == Incident::replay_cursor_reset && !replay_fault) result.missing_incidents.push_back("durable replay cursor before callback");
  return result;
}

std::string_view name(Incident v) {
  switch (v) { case Incident::none: return "none"; case Incident::checkpoint_reset: return "checkpoint-reset"; case Incident::replay_cursor_reset: return "cursor-reset"; }
  throw std::invalid_argument("unknown retention incident");
}
std::string_view name(Negative v) {
  switch (v) { case Negative::none: return "none"; case Negative::skip_replay_root: return "skip-replay-root"; case Negative::lose_retry: return "lose-retry"; }
  throw std::invalid_argument("unknown retention negative");
}
void write_json(std::ostream& out, const Result& r) {
  auto quote = [&](std::string_view value) { out << std::quoted(std::string(value)); };
  out << "{\"cohorts\":{"; bool comma{};
  for (const auto& [name, c] : r.cohorts) { if (comma) out << ','; comma = true; quote(name);
    out << ":{\"offered\":" << c.offered << ",\"arrived\":" << c.arrived << ",\"completed\":" << c.completed
        << ",\"failed\":" << c.failed << ",\"unfinished\":" << c.unfinished << '}'; }
  out << "},\"violations\":["; comma = false;
  for (const auto& v : r.violations) { if (comma) out << ','; comma = true; quote(v); }
  out << "],\"missing_incidents\":["; comma = false;
  for (const auto& v : r.missing_incidents) { if (comma) out << ','; comma = true; quote(v); }
  out << "],\"execution\":{\"events\":" << r.execution.events << ",\"budget_exhausted\":" << (r.execution.budget_exhausted ? "true" : "false")
      << ",\"pending\":" << (r.execution.pending ? "true" : "false") << "},\"reconstruction_refusals\":" << r.reconstruction_refusals
      << ",\"independent_during_pressure\":" << r.independent_during_pressure << ",\"reclaimed_bytes\":" << r.reclaimed_bytes
      << ",\"source_restarts\":" << r.source_restarts << ",\"memory_peak\":" << r.source_usage.memory_peak
      << ",\"durable_bytes\":" << r.source_usage.durable << ",\"pressure_released\":" << (r.pressure_released ? "true" : "false")
      << ",\"reader_root_released\":" << (r.reader_root_released ? "true" : "false")
      << ",\"replay_root_advanced\":" << (r.replay_root_advanced ? "true" : "false")
      << ",\"current_recovered_after_gc\":" << (r.current_recovered_after_gc ? "true" : "false")
      << ",\"trace_hash\":" << r.trace_hash << "}\n";
}

} // namespace sixdb::sim::retention_pressure
