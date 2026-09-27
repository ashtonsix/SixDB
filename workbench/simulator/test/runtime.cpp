// Public-port counterexamples for the environment, not an Orbital protocol proof.
// Probe actors see only Context; observers retain small histories for assertions.
#include <sixdb/sim/runtime.hpp>

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using namespace sixdb::sim;
using History = std::vector<Record>;
using Handler = std::function<void(Context&, const Event&)>;

void require(bool condition, std::string_view explanation) {
  if (!condition) throw std::runtime_error(std::string(explanation));
}

template<class F> void rejects(F&& action, std::string_view explanation) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  require(rejected, explanation);
}

class Probe final : public Actor {
 public:
  explicit Probe(Handler handler) : handler_(std::move(handler)) {}
  void receive(Context& context, const Event& event) override { handler_(context, event); }
 private:
  Handler handler_;
};

Factory probe(Handler handler) {
  return [handler = std::move(handler)] { return std::make_unique<Probe>(handler); };
}

Host host(HostId id) {
  Host result;
  result.id = id;
  result.cpu.latency_ns = 0;
  result.transmit.latency_ns = 0;
  result.receive.latency_ns = 0;
  result.disk.latency_ns = 100;
  return result;
}

void process(Simulation& simulation, HostId machine, ProcessId process_id, ActorId actor,
             Handler handler) {
  simulation.add_process(process_id, machine);
  simulation.add_actor(actor, process_id, probe(std::move(handler)));
}

void capture(Simulation& simulation, History& history) {
  simulation.observe([&history](const Record& record) { history.push_back(record); });
}

std::vector<Record> notes(const History& history, std::string_view kind) {
  std::vector<Record> result;
  for (const auto& record : history) if (record.kind == kind) result.push_back(record);
  return result;
}

bool detail(const History& history, std::string_view kind, std::string_view value) {
  return std::ranges::any_of(history, [&](const Record& record) {
    return record.kind == kind && record.detail == value;
  });
}

void drain(Simulation& simulation, Time until = 20'000) {
  const auto result = simulation.run(until);
  require(!result.budget_exhausted, "small finite fixture exhausted its event budget");
}

void process_boundaries_and_callbacks() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  simulation.add_process(1, 1);
  simulation.add_actor(11, 1, probe([](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      context.note("probe_boot", std::to_string(context.incarnation()));
      if (context.incarnation() == 1) {
        context.write("survivor", "durable!", 1);
        context.compute(500, 2);
        context.timer(500, 3);
      } else context.timer(1'000, 4);
    } else if (event.kind == EventKind::timer && event.tag == 4) {
      context.read("survivor", 8, 5);
    } else if (event.tag == 5) {
      context.note("probe_recovered", event.bytes);
    } else context.note("probe_old_callback", std::to_string(event.tag));
  }));
  simulation.add_actor(12, 1, probe([](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      context.note("probe_sibling_boot", std::to_string(context.incarnation()));
      context.timer(600, 1);
    } else context.note("probe_sibling_timer", std::to_string(context.incarnation()));
  }));
  process(simulation, 1, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) context.timer(600, 1);
    else context.note("probe_other_process", std::to_string(context.incarnation()));
  });
  History history;
  capture(simulation, history);
  simulation.start();
  simulation.at(10, "kill first process", [](Simulation& world) { world.crash(1); });
  simulation.at(20, "restart first process", [](Simulation& world) { world.restart(1); });
  drain(simulation);
  require(notes(history, "probe_boot").size() == 2, "main actor did not restart fresh");
  require(notes(history, "probe_sibling_boot").size() == 2, "process crash missed sibling actor");
  require(notes(history, "probe_old_callback").empty(), "old incarnation received a callback");
  require(detail(history, "probe_sibling_timer", "2") &&
          !detail(history, "probe_sibling_timer", "1"), "sibling timer escaped process fencing");
  require(detail(history, "probe_other_process", "1"), "same-host process died unnecessarily");
  require(detail(history, "probe_recovered", "durable!"), "submitted write died with its process");
  require(std::ranges::any_of(history, [](const Record& record) {
    return record.kind == "storage.write" && record.actor == 11 && record.tag == 1 &&
           record.time > 20 && record.incarnation == 1;
  }), "old durable write was relabeled as the replacement process's evidence");
  require(simulation.usage(1).memory == 0, "process/recovery path leaked resident bytes");
}

void departed_messages_and_device_reset() {
  for (const bool power : {false, true}) {
    for (const Time failure : {Time{1}, Time{100}}) {
      Simulation simulation({.ordering = Ordering::fifo});
      simulation.add_host(host(1));
      simulation.add_host(host(2));
      simulation.add_link(Link{.from = 1, .to = 2, .propagation_ns = 1'000});
      process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
        if (event.kind == EventKind::boot) {
          Bytes value = "original";
          context.send(21, 1, value);
          value = "mutated!";
        }
      });
      process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
        if (event.kind == EventKind::message) context.note("probe_message", event.bytes);
      });
      History history;
      capture(simulation, history);
      simulation.start();
      simulation.at(failure, "sender loss", [power](Simulation& world) {
        if (power) world.power_loss(1, true); else world.crash(1);
      });
      drain(simulation);
      const bool expected = !power || failure == 100;
      require(notes(history, "probe_message").size() == (expected ? 1U : 0U),
              "sender loss incorrectly changed a submitted/departed message");
      if (expected) require(detail(history, "probe_message", "original"), "message shared mutable source bytes");
      require(simulation.usage(1).memory == 0 && simulation.usage(2).memory == 0,
              "transmission failure leaked buffers");
    }
  }
}

void backend_borrows_outlive_ownership() {
  for (const bool reset : {false, true}) {
    auto machine = host(1);
    machine.memory_bytes = 8;
    machine.disk.latency_ns = 1'000;
    Simulation simulation({.ordering = Ordering::fifo});
    simulation.add_host(machine);
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) {
        auto buffer = context.allocate("retained");
        require(buffer.has_value(), "initial lease unexpectedly refused");
        context.write("record", *buffer, 1);
        context.release(*buffer);
      } else context.note("probe_dead_completion");
    });
    process(simulation, 1, 2, 21, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) return;
      auto buffer = context.allocate("12345678");
      context.note("probe_allocation", buffer ? "yes" : "no", event.tag);
      if (buffer) context.release(*buffer);
    });
    History history;
    capture(simulation, history);
    simulation.start();
    simulation.at(5, "owner dies", [reset](Simulation& world) {
      if (reset) { world.power_loss(1); world.restart(2); }
      else world.crash(1);
    });
    simulation.inject(20, 21, 1);
    simulation.inject(2'000, 21, 2);
    drain(simulation, 100);
    const auto midway = simulation.usage(1);
    require(midway.memory == (reset ? 0U : 8U), "backend lease retired at wrong failure boundary");
    require(midway.reserved_storage == (reset ? 0U : 8U), "pending write reservation lost or leaked");
    drain(simulation);
    const auto allocations = notes(history, "probe_allocation");
    require(allocations.size() == 2, "allocation probes did not execute");
    require(allocations[0].detail == (reset ? "yes" : "no") && allocations[1].detail == "yes",
            "owner loss released an active borrow, or completion failed to release it");
    require(notes(history, "probe_dead_completion").empty(), "dead owner received durable callback");
    require(simulation.usage(1).memory == 0, "backend retirement leaked resident bytes");
    require(simulation.usage(1).durable == (reset ? 0U : 8U), "power reset and process crash treated alike");
  }
}

void device_reset_retires_old_cpu_service() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      if (context.incarnation() == 1) context.compute(10'000, 1);
      else context.compute(1, 2);
    } else context.note("probe_cpu_completion", std::to_string(context.incarnation()), event.tag);
  });
  History history;
  capture(simulation, history);
  simulation.start();
  simulation.at(10, "CPU reset", [](Simulation& world) { world.power_loss(1); });
  simulation.at(20, "restart after reset", [](Simulation& world) { world.restart(1); });
  drain(simulation, 100);
  const auto results = notes(history, "probe_cpu_completion");
  require(results.size() == 1 && results[0].tag == 2 && results[0].detail == "2",
          "reset machine kept the dead generation's CPU queue");
  drain(simulation);
  require(notes(history, "probe_cpu_completion").size() == 1, "retired CPU event reached a new process incarnation");
}

void process_crash_retires_its_active_and_queued_cpu_work() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      context.compute(10'000, 1);
      context.compute(10'000, 2);
    } else context.note("probe_dead_cpu_completion");
  });
  process(simulation, 1, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) context.timer(20, 1);
    else if (event.kind == EventKind::timer) context.compute(1, 2);
    else context.note("probe_live_cpu_completion");
  });
  History history;
  capture(simulation, history);
  simulation.start();
  simulation.at(10, "CPU owner lost", [](Simulation& world) { world.crash(1); });
  drain(simulation, 100);
  require(notes(history, "probe_live_cpu_completion").size() == 1,
          "process-owned CPU work blocked a healthy colocated process after owner death");
  const auto cancelled = notes(history, "compute.cancelled");
  require(cancelled.size() == 2 && cancelled[0].size + cancelled[1].size == 10,
          "CPU cancellation lost elapsed service or charged unstarted queued work");
  drain(simulation);
  require(notes(history, "probe_dead_cpu_completion").empty(), "cancelled CPU work called a dead process");
}

void observers_cannot_reenter_execution_or_mutate_processes() {
  for (const std::string action : {"crash", "restart", "run"}) {
    Simulation simulation({.ordering = Ordering::fifo});
    simulation.add_host(host(1));
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) context.timer(10, 101);
      else context.note("probe_guard_actor", std::to_string(event.tag));
    });
    process(simulation, 1, 2, 21, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) context.note("probe_guard_peer_boot");
    });
    History history;
    bool exercised = false, rejected = false;
    simulation.observe([&](const Record& record) {
      history.push_back(record);
      if (!exercised && record.kind == "actor.receive" && record.actor == 11 && record.tag == 101) {
        exercised = true;
        try {
          if (action == "crash") simulation.crash(1);
          else if (action == "restart") simulation.restart(2);
          else simulation.run(1'000);
        } catch (const std::exception&) { rejected = true; }
        simulation.at(record.time, "allowed deferred observer action", [](Simulation& world) {
          world.inject(world.now(), 11, 102);
        });
        simulation.pause();
      }
    });
    simulation.start();
    simulation.at(1, "stop peer", [](Simulation& world) { world.crash(2); });
    const auto paused = simulation.run(1'000);
    require(exercised && rejected, "observer directly changed an executing simulation");
    require(paused.paused && simulation.now() == 10 && detail(history, "probe_guard_actor", "101"),
            "rejected observer action mutated the in-flight actor or interrupted its handler");
    drain(simulation);
    require(detail(history, "probe_guard_actor", "102"), "observer could not defer an allowed action");
    require(notes(history, "probe_guard_peer_boot").size() == 1, "rejected observer restart changed the stopped peer");
  }
}

void receive_bytes_are_charged_during_callback() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  auto destination = host(2);
  destination.memory_bytes = 8;
  simulation.add_host(destination);
  simulation.add_link(Link{.from = 1, .to = 2});
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) context.send(21, 1, Bytes{"abcdefgh"});
  });
  process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) return;
    auto other = context.allocate("12345678");
    context.note("probe_receive_capacity", other ? "free" : "held", event.tag);
    if (other) context.release(*other);
    if (event.kind == EventKind::message) {
      context.note("probe_receive_bytes", event.bytes);
      context.timer(1, 2);
    }
  });
  History history;
  capture(simulation, history);
  simulation.start();
  drain(simulation);
  const auto observations = notes(history, "probe_receive_capacity");
  require(observations.size() == 2 && observations[0].detail == "held" && observations[1].detail == "free",
          "receive buffer was uncharged during callback or retained after it");
  require(detail(history, "probe_receive_bytes", "abcdefgh"), "receive callback lost its bytes");
}

void received_volatile_bytes_do_not_cross_a_device_reset() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  simulation.add_host(host(2));
  simulation.add_link(Link{.from = 1, .to = 2});
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      context.send(21, 1, Bytes{"old"});
      context.timer(5'000, 2);
    } else if (event.kind == EventKind::timer) context.send(21, 2, Bytes{"fresh"});
  });
  process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::message) context.note("probe_post_reset_message", event.bytes);
  });
  History history;
  bool reset = false;
  simulation.observe([&](const Record& record) {
    history.push_back(record);
    if (!reset && record.kind == "network.arrive" && record.actor == 21) {
      reset = true;
      // FIFO makes this incident run after NIC completion but before the
      // scheduled actor callback. Those bytes already reside on this host.
      simulation.at(record.time, "reset resident receive buffer", [](Simulation& world) {
        world.power_loss(2);
        world.restart(2);
      });
    }
  });
  simulation.start();
  drain(simulation);
  require(reset, "receive-reset incident was not exercised");
  require(!detail(history, "probe_post_reset_message", "old"), "pre-reset receive bytes reached the replacement process");
  require(detail(history, "probe_post_reset_message", "fresh"), "receiver failed to resume after reset");
  require(simulation.usage(2).memory == 0, "fenced receive callback leaked its buffer");
}

void timer_payloads_obey_memory_and_process_lifetime() {
  for (const bool crash : {false, true}) {
    auto machine = host(1);
    machine.memory_bytes = 8;
    Simulation simulation({.ordering = Ordering::fifo});
    simulation.add_host(machine);
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) {
        context.timer(1'000, 1, "12345678");
        context.timer(1'200, 2, "x");
        auto extra = context.allocate("x");
        context.note("probe_timer_initial", extra ? "free" : "held");
        if (extra) context.release(*extra);
      } else if (event.kind == EventKind::completion) {
        require(event.tag == 2 && event.operation == Operation::none && event.status == Status::capacity,
                "timer capacity refusal lost its asynchronous result");
        context.note("probe_timer_refused");
      } else {
        require(event.kind == EventKind::timer && event.tag == 1 && event.bytes == "12345678",
                "refused timer fired or accepted timer lost its bytes");
        auto extra = context.allocate("x");
        context.note("probe_timer_callback", extra ? "free" : "held");
        if (extra) context.release(*extra);
      }
    });
    process(simulation, 1, 2, 21, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) return;
      auto buffer = context.allocate("abcdefgh");
      context.note("probe_timer_reuse", buffer ? "free" : "held", event.tag);
      if (buffer) context.release(*buffer);
    });
    History history;
    capture(simulation, history);
    simulation.start();
    if (crash) simulation.at(500, "timer owner lost", [](Simulation& world) { world.crash(1); });
    simulation.inject(600, 21, 1);
    simulation.inject(1'500, 21, 2);
    drain(simulation);
    require(detail(history, "probe_timer_initial", "held"), "timer payload bypassed finite resident bytes");
    require(notes(history, "probe_timer_refused").size() == 1, "timer refusal was not reported exactly once");
    const auto reuse = notes(history, "probe_timer_reuse");
    require(reuse.size() == 2 && reuse[0].detail == (crash ? "free" : "held") && reuse[1].detail == "free",
            "timer payload retired at the wrong lifetime boundary");
    const auto callbacks = notes(history, "probe_timer_callback");
    require(callbacks.size() == (crash ? 0U : 1U), "timer reached the wrong process incarnation");
    if (!crash) require(callbacks[0].detail == "held", "timer callback read uncharged resident bytes");
    require(simulation.usage(1).memory == 0, "timer completion/crash leaked payload memory");
  }
}

void cancelled_timer_disappears_from_replay_and_pending_work() {
  auto run = [](Options options) {
    Simulation simulation(options);
    auto machine = host(1);
    machine.memory_bytes = 16;
    simulation.add_host(machine);
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) context.timer(1'000'000'000, 1, "long payload");
      else context.note("probe_cancelled_timer_fired");
    });
    History history;
    capture(simulation, history);
    simulation.start();
    simulation.at(100, "cancel timer owner", [](Simulation& world) { world.crash(1); });
    const auto finished = simulation.run(1'000);
    require(!finished.pending, "cancelled long timer remained in the event queue");
    require(simulation.usage(1).memory == 0, "cancelled timer retained payload bytes");
    require(notes(history, "probe_cancelled_timer_fired").empty(), "cancelled timer reached its owner");
    simulation.finish_replay();
    return simulation.trace_hash();
  };
  std::ostringstream output;
  const auto original = run({.seed = 29, .decisions_out = &output});
  require(output.str().find("\nC ") != std::string::npos, "exact transcript did not record event cancellation");
  std::istringstream input(output.str());
  require(original == run({.decisions_in = &input}), "cancelled-event replay changed causal history");
}

void finite_queue_refusal_retires_buffers() {
  Simulation simulation({.ordering = Ordering::fifo});
  auto source = host(1);
  source.transmit.queue_bytes = 8;
  simulation.add_host(source);
  simulation.add_host(host(2));
  simulation.add_link(Link{.from = 1, .to = 2});
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) {
      context.send(21, 1, Bytes{"first123"});
      context.send(21, 2, Bytes{"second12"});
    } else context.note("probe_queue_result", std::string(name(event.status)), event.tag);
  });
  process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::message) context.note("probe_queue_delivery", event.bytes);
  });
  History history;
  capture(simulation, history);
  simulation.start();
  drain(simulation);
  const auto responses = notes(history, "probe_queue_result");
  require(responses.size() == 2, "refused send vanished without completion");
  require(std::ranges::any_of(responses, [](const Record& record) {
    return record.tag == 2 && record.detail == "capacity";
  }), "active transmit bytes were omitted from the finite queue");
  require(notes(history, "probe_queue_delivery").size() == 1 &&
          detail(history, "probe_queue_delivery", "first123"), "refused send still reached its peer");
  require(simulation.usage(1).memory == 0 && simulation.usage(2).memory == 0,
          "queue refusal leaked payload buffers");
}

void bounded_storage_and_enumeration() {
  auto machine = host(1);
  machine.storage_bytes = 24;
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(machine);
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot) { context.write("j/0", "abcdefgh", 1); return; }
    context.note("probe_storage_status", std::string(name(event.status)), event.tag);
    switch (event.tag) {
      case 1: context.write("j/1", "abcdefgh", 2); break;
      case 2: context.write("j/2", "abcdefgh", 3); break;
      case 3: context.write("j/3", "abcdefgh", 4); break;
      case 4: context.list("j/", "", 2, 64, 5); break;
      case 5:
        context.note("probe_page", event.keys.size() == 2 ? event.keys[0] + "," + event.keys[1] : "wrong", 5);
        context.note("probe_cursor", event.next, 5);
        context.list("j/", event.next, 2, 64, 6);
        break;
      case 6:
        context.note("probe_page", event.keys.size() == 1 ? event.keys[0] : "wrong", 6);
        context.note("probe_cursor", event.next, 6);
        context.list("j/", "", 2, 1, 7);
        break;
      case 7: context.read("j/0", 4, 8); break;
      case 8: context.erase("j/0", 9); break;
      case 9: context.read("j/0", 8, 10); break;
      case 10: context.write("j/3", "abcdefgh", 11); break;
      case 11: context.list("j/", "", 10, 64, 12); break;
      case 12:
        context.note("probe_final_page", event.keys.size() == 3 ? event.keys[0] + "," + event.keys[1] + "," + event.keys[2] : "wrong");
        break;
      default: throw std::runtime_error("unexpected storage continuation");
    }
  });
  History history;
  capture(simulation, history);
  simulation.start();
  drain(simulation);
  for (const auto& record : notes(history, "probe_storage_status")) {
    const auto expected = record.tag == 4 || record.tag == 7 || record.tag == 8 ? "capacity" :
                          record.tag == 10 ? "missing" : "ok";
    require(record.detail == expected, "incorrect bounded storage/enumeration status");
  }
  require(notes(history, "probe_storage_status").size() == 12, "storage chain stalled");
  require(detail(history, "probe_page", "j/0,j/1") && detail(history, "probe_page", "j/2"),
          "enumeration lost lexical order or its bound");
  const auto cursors = notes(history, "probe_cursor");
  require(cursors.size() == 2 && cursors[0].detail == "j/1" && cursors[1].detail.empty(),
          "enumeration continuation did not describe remaining records");
  require(detail(history, "probe_final_page", "j/1,j/2,j/3"), "erase or post-erase write was not durable");
  const auto usage = simulation.usage(1);
  require(usage.durable == 24 && usage.reserved_storage == 0 && usage.memory == 0,
          "bounded storage operations broke byte conservation");
}

void enumeration_cursor_survives_changes_between_pages() {
  auto machine = host(1);
  machine.storage_bytes = 16;
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(machine);
  process(simulation, 1, 1, 11, [cursor = std::string{}](Context& context, const Event& event) mutable {
    if (event.kind == EventKind::boot) { context.write("j/a", "abcdefgh", 1); return; }
    require(event.status == Status::ok, "mutating enumeration fixture refused an operation");
    switch (event.tag) {
      case 1: context.write("j/c", "abcdefgh", 2); break;
      case 2: context.list("j/", "", 1, 32, 3); break;
      case 3:
        require(event.keys == std::vector<std::string>{"j/a"}, "wrong first enumeration page");
        cursor = event.next;
        context.erase("j/a", 4);
        break;
      case 4: context.write("j/b", "abcdefgh", 5); break;
      case 5: context.list("j/", cursor, 10, 32, 6); break;
      case 6:
        require(event.keys == std::vector<std::string>({"j/b", "j/c"}) && event.next.empty(),
                "enumeration treated its deleted key cursor as an offset or frozen snapshot");
        context.note("probe_changed_enumeration", "complete");
        break;
      default: throw std::runtime_error("unexpected enumeration continuation");
    }
  });
  History history;
  capture(simulation, history);
  simulation.start();
  drain(simulation);
  require(detail(history, "probe_changed_enumeration", "complete"), "mutating enumeration did not finish");
  require(simulation.usage(1).memory == 0 && simulation.usage(1).durable == 16,
          "bounded enumeration mutation leaked bytes");
}

class Checkpointer final : public Actor {
 public:
  void receive(Context& context, const Event& event) override {
    if (event.kind == EventKind::boot) {
      if (context.incarnation() == 1) context.write("journal", "answer", 1);
      else context.read("root", 32, 10);
      return;
    }
    require(event.status == Status::ok, "checkpoint fixture unexpectedly refused an operation");
    switch (event.tag) {
      case 1: context.write("root", "journal", 2); break;
      case 2:
        context.write("checkpoint", "answer", 3);
        context.note("probe_checkpoint_boundary", "checkpoint-submitted");
        break;
      case 3:
        context.note("probe_checkpoint_boundary", "checkpoint-durable");
        context.write("root", "checkpoint", 4);
        context.note("probe_checkpoint_boundary", "root-submitted");
        break;
      case 4:
        context.note("probe_checkpoint_boundary", "root-durable");
        context.erase("journal", 5);
        break;
      case 5: context.note("probe_checkpoint_boundary", "journal-deleted"); break;
      case 10:
        context.note("probe_recovered_root", event.bytes);
        context.read(event.bytes, 32, 11);
        break;
      case 11: context.note("probe_checkpoint_recovered", event.bytes); break;
      default: throw std::runtime_error("unexpected checkpoint continuation");
    }
  }
};

void checkpoint_delete_recovery() {
  for (const std::string cut : {"checkpoint-submitted", "checkpoint-durable", "root-submitted",
                                "root-durable", "journal-deleted"}) {
    auto machine = host(1);
    machine.storage_bytes = 32;
    machine.memory_bytes = 64;
    Simulation simulation({.ordering = Ordering::fifo});
    simulation.add_host(machine);
    simulation.add_process(1, 1);
    simulation.add_actor(11, 1, [] { return std::make_unique<Checkpointer>(); });
    History history;
    bool fired = false;
    simulation.observe([&](const Record& record) {
      history.push_back(record);
      if (!fired && record.kind == "probe_checkpoint_boundary" && record.detail == cut) {
        fired = true;
        simulation.at(record.time, "checkpoint cut", [](Simulation& world) { world.crash(1); world.restart(1); });
      }
    });
    simulation.start();
    drain(simulation);
    require(fired, "checkpoint fault boundary was not reached");
    require(detail(history, "probe_checkpoint_recovered", "answer"), "recovery could not discover retained checkpoint data");
    const auto usage = simulation.usage(1);
    require(usage.durable <= 32 && usage.reserved_storage == 0 && usage.memory == 0,
            "checkpoint replacement exceeded capacity or leaked a backend user");
  }
}

void absent_links_and_hidden_peer_failure() {
  std::vector<std::vector<std::string>> local_histories;
  for (const bool peer_dead : {false, true}) {
    Simulation simulation({.ordering = Ordering::fifo});
    simulation.add_host(host(1));
    simulation.add_host(host(2));
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) context.timer(10, 1);
      else if (event.kind == EventKind::timer) {
        context.send(21, 2, Bytes{"request"});
        context.note("probe_local", "submitted");
      } else context.note("probe_local", std::string(name(event.status)));
    });
    process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
      if (event.kind == EventKind::message) context.note("probe_unreachable_delivery");
    });
    History history;
    capture(simulation, history);
    simulation.start();
    if (peer_dead) simulation.at(1, "hidden peer loss", [](Simulation& world) { world.crash(2); });
    drain(simulation);
    require(notes(history, "probe_unreachable_delivery").empty(), "runtime invented an unconfigured link");
    std::vector<std::string> actions;
    for (const auto& record : notes(history, "probe_local")) actions.push_back(std::to_string(record.time) + ":" + record.detail);
    require(detail(history, "probe_local", "unreachable"), "unconfigured route did not report unreachable");
    local_histories.push_back(std::move(actions));
  }
  require(local_histories[0] == local_histories[1], "hidden peer death leaked through a local port");
}

std::uint64_t replay_fixture(Options options, Bytes payload, Time until = 10'000, bool finish = true) {
  Simulation simulation(options);
  simulation.add_host(host(1));
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::message) context.timer(10, event.tag, event.bytes);
    else if (event.kind == EventKind::timer) context.note("probe_replayed", event.bytes, event.tag);
  });
  simulation.start();
  simulation.inject(10, 11, 1, payload);
  simulation.inject(10, 11, 2, "second");
  drain(simulation, until);
  if (finish) simulation.finish_replay();
  return simulation.trace_hash();
}

void exact_replay_rejects_divergence() {
  std::ostringstream output;
  const auto original = replay_fixture({.seed = 79, .ordering = Ordering::seeded, .decisions_out = &output}, "first");
  require(!output.str().empty(), "exact replay emitted no transcript");
  std::istringstream input(output.str());
  const auto repeated = replay_fixture({.seed = 999, .decisions_in = &input}, "first");
  require(original == repeated, "exact replay changed causal history");
  rejects([&] {
    std::istringstream changed(output.str());
    replay_fixture({.decisions_in = &changed}, "other");
  }, "replay accepted altered message bytes");
  rejects([&] {
    std::istringstream unused(output.str());
    replay_fixture({.decisions_in = &unused}, "first", 0);
  }, "replay accepted an unused transcript suffix");
  rejects([&] {
    std::istringstream empty;
    replay_fixture({.decisions_in = &empty}, "first");
  }, "replay accepted missing choices");
}

void keyed_random_is_not_a_shared_draw_stream() {
  std::vector<std::string> compared;
  for (const bool noise : {false, true}) {
    Simulation simulation({.seed = 17});
    simulation.add_host(host(1));
    process(simulation, 1, 1, 11, [noise](Context& context, const Event& event) {
      if (event.kind != EventKind::boot) return;
      const auto first = context.random("loss", 71);
      if (noise) for (std::uint64_t i = 0; i != 100; ++i) (void)context.random("other-flow", i);
      const auto repeated = context.random("loss", 71);
      context.note("probe_keyed_random", std::to_string(first) + ":" + std::to_string(repeated));
    });
    History history;
    capture(simulation, history);
    simulation.start();
    drain(simulation);
    const auto observations = notes(history, "probe_keyed_random");
    require(observations.size() == 1, "keyed random probe did not run");
    const auto& value = observations[0].detail;
    require(value.substr(0, value.find(':')) == value.substr(value.find(':') + 1), "same random key changed within one actor");
    compared.push_back(value);
  }
  require(compared[0] == compared[1], "unrelated random draws shifted an existing flow");
}

void causal_history_joins_messages_state_and_durable_reads() {
  Simulation simulation({.ordering = Ordering::fifo});
  simulation.add_host(host(1));
  simulation.add_host(host(2));
  simulation.add_link(Link{.from = 1, .to = 2});
  process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
    if (event.kind == EventKind::message && event.tag == 771)
      context.send(21, 501, Bytes{"recovered"});
  });
  process(simulation, 2, 2, 21, [](Context& context, const Event& event) {
    if (event.kind == EventKind::boot && context.incarnation() > 1)
      context.read("receipt", 32, 700);
    else if (event.kind == EventKind::message && event.tag == 501)
      context.write("receipt", event.bytes, 600);
    else if (event.kind == EventKind::completion && event.tag == 600)
      context.note("probe_causal_durable");
    else if (event.kind == EventKind::completion && event.tag == 700)
      context.note("probe_causal_recovered", event.bytes);
    else if (event.kind == EventKind::message && event.tag == 772)
      context.note("probe_causal_prior_state");
  });
  History history;
  capture(simulation, history);
  simulation.start();
  simulation.inject(1, 21, 772);
  simulation.inject(10, 11, 771);
  // Independent incident: its harness ancestry must not supply the missing
  // durable-write edge that this test is intended to check.
  simulation.at(3'000, "recover receipt", [](Simulation& world) { world.crash(2); world.restart(2); });
  drain(simulation);
  const auto recovered = notes(history, "probe_causal_recovered");
  require(recovered.size() == 1 && recovered[0].detail == "recovered", "causal fixture did not recover its durable bytes");
  auto check_ancestry = [&](const History& observed) {
    std::unordered_set<std::uint64_t> ancestors;
    std::vector<std::uint64_t> pending{recovered[0].id};
    while (!pending.empty()) {
      const auto id = pending.back();
      pending.pop_back();
      if (!id || !ancestors.insert(id).second) continue;
      const auto found = std::ranges::find_if(observed, [id](const Record& record) { return record.id == id; });
      require(found != observed.end(), "causal edge refers to an absent record");
      require(found->cause < id, "causal edge goes forward or cycles");
      pending.push_back(found->cause);
      for (const auto parent : found->parents) {
        require(parent < id, "joined causal parent goes forward or cycles");
        pending.push_back(parent);
      }
    }
    for (const auto [actor, tag] : {std::pair{ActorId{11}, std::uint64_t{771}},
                                   std::pair{ActorId{21}, std::uint64_t{772}},
                                   std::pair{ActorId{21}, std::uint64_t{600}}}) {
      require(std::ranges::any_of(observed, [&](const Record& record) {
        return record.actor == actor && record.tag == tag && ancestors.contains(record.id);
      }), "recovery history lost its message origin, previous actor state, or durable write");
    }
  };
  check_ancestry(history);
  auto missing_provenance = history;
  bool removed = false;
  for (auto& record : missing_provenance) if (record.kind == "storage.read") {
    removed |= !record.parents.empty();
    record.parents.clear();
  }
  require(removed, "causal negative control did not remove a durable-read edge");
  rejects([&] { check_ancestry(missing_provenance); }, "causal oracle accepted a read without durable provenance");
}

void pause_and_budget_preserve_the_remaining_execution() {
  std::vector<std::uint64_t> hashes;
  for (const int mode : {0, 1, 2}) {
    Simulation simulation({.seed = 73, .ordering = Ordering::seeded});
    simulation.add_host(host(1));
    process(simulation, 1, 1, 11, [](Context& context, const Event& event) {
      if (event.kind == EventKind::boot) {
        context.timer(10, 1);
        context.timer(20, 2);
        context.timer(30, 3);
      } else {
        context.note("probe_staged_tick", std::to_string(event.tag));
        if (event.tag == 2) {
          context.note("probe_staged_after_pause");
          context.timer(5, 4);
        }
      }
    });
    History history;
    simulation.observe([&](const Record& record) {
      history.push_back(record);
      if (mode == 1 && record.kind == "probe_staged_tick" && record.detail == "2") simulation.pause();
    });
    simulation.start();
    if (mode == 1) {
      const auto paused = simulation.run(20'000);
      require(!paused.budget_exhausted && paused.pending && simulation.now() == 20,
              "pause advanced beyond the completed atomic event");
      require(notes(history, "probe_staged_after_pause").size() == 1,
              "pause interrupted an actor handler in the middle");
    } else if (mode == 2) {
      const auto stopped = simulation.run(20'000, 2);
      require(stopped.budget_exhausted && stopped.pending && simulation.now() < 20,
              "event budget advanced virtual time past pending events");
    }
    drain(simulation);
    require(notes(history, "probe_staged_tick").size() == 4, "staged execution lost a continuation");
    hashes.push_back(simulation.trace_hash());
  }
  require(hashes[0] == hashes[1] && hashes[0] == hashes[2], "pause or event budget changed the resumed causal execution");
}

void invalid_configuration_is_rejected() {
  rejects([] { Simulation world; auto h = host(1); h.disk.bytes_per_second = 0; world.add_host(h); },
          "zero service bandwidth was accepted");
  rejects([] { Simulation world; world.add_host(host(1)); world.add_host(host(1)); }, "duplicate host accepted");
  rejects([] { Simulation world; world.add_process(1, 99); }, "process attached to unknown host");
  rejects([] { Simulation world; world.add_actor(1, 99, probe([](Context&, const Event&) {})); },
          "actor attached to unknown process");
  rejects([] { Simulation world; world.add_host(host(1)); world.add_link(Link{.from = 1, .to = 99}); },
          "link attached to unknown host");
  rejects([] { Simulation world; world.add_host(host(1)); world.inject(0, 99, 1); }, "input addressed an unknown actor");
  rejects([] { Simulation world; world.start(); world.run(10); world.run(9); }, "virtual time moved backwards");
}
} // namespace

int main() {
  const std::pair<const char*, void (*)()> tests[] = {
    {"process boundaries and callback fencing", process_boundaries_and_callbacks},
    {"departed messages and device reset", departed_messages_and_device_reset},
    {"backend borrows outlive ownership", backend_borrows_outlive_ownership},
    {"device reset retires CPU service", device_reset_retires_old_cpu_service},
    {"process crash retires CPU work", process_crash_retires_its_active_and_queued_cpu_work},
    {"observer reentrancy guard", observers_cannot_reenter_execution_or_mutate_processes},
    {"receive bytes charged during callback", receive_bytes_are_charged_during_callback},
    {"receive completion before device reset", received_volatile_bytes_do_not_cross_a_device_reset},
    {"timer payload memory and lifetime", timer_payloads_obey_memory_and_process_lifetime},
    {"cancelled timers leave pending execution", cancelled_timer_disappears_from_replay_and_pending_work},
    {"finite queue refusal retires buffers", finite_queue_refusal_retires_buffers},
    {"bounded storage and enumeration", bounded_storage_and_enumeration},
    {"enumeration cursor across mutation", enumeration_cursor_survives_changes_between_pages},
    {"checkpoint deletion and recovery", checkpoint_delete_recovery},
    {"absent links and hidden peer failure", absent_links_and_hidden_peer_failure},
    {"exact replay rejection", exact_replay_rejects_divergence},
    {"keyed random independence", keyed_random_is_not_a_shared_draw_stream},
    {"joined causal recovery history", causal_history_joins_messages_state_and_durable_reads},
    {"pause and event-budget resume", pause_and_budget_preserve_the_remaining_execution},
    {"invalid configuration", invalid_configuration_is_rejected},
  };
  unsigned failures = 0;
  for (const auto& [label, run] : tests) {
    try { run(); std::cout << "PASS " << label << '\n'; }
    catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << label << ": " << error.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
