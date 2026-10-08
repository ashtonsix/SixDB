#include "native.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

using namespace loom_policy_spike;
namespace {
using Clock = std::chrono::steady_clock;
struct Options {
  Geometry geometry;
  unsigned width{8};
  std::size_t rounds{4096}, nodes{65536}, reps{3};
  int cpu{-1};
};
struct Message {
  enum Kind { profile, pressure, stop, rejected } kind{rejected};
  Profile value;
  Pressure load{};
};
Message parse(const std::string& line) {
  std::istringstream input(line);
  std::string command, rest;
  input >> command;
  Message result;
  if (command == "profile") {
    result.kind = Message::profile;
    if (!(input >> result.value.revision >> result.value.geometry.line_bytes >> result.value.geometry.page_bytes
                >> result.value.preferred_width >> result.value.training_accesses)) result.kind = Message::rejected;
  } else if (command == "pressure") {
    input >> rest;
    if (rest == "low" || rest == "busy") {
      result.kind = Message::pressure;
      result.load = rest == "low" ? Pressure::low : Pressure::busy;
    }
  } else if (command == "stop") result.kind = Message::stop;
  if (input >> rest) result.kind = Message::rejected;
  return result;
}
void print_policy(const Policy& policy) {
  std::cout << '[';
  for (unsigned i = 0; i < demand_count; ++i) {
    if (i) std::cout << ',';
    std::cout << '"' << procedure_name(policy.procedures[i]) << '"';
  }
  std::cout << ']';
}
const char* dispatch_name(Dispatch dispatch) {
  switch (dispatch) {
    case Dispatch::fixed_inline: return "fixed-inline";
    case Dispatch::code_lut_switch: return "code-lut-switch";
    case Dispatch::procedure_lut: return "procedure-lut";
    case Dispatch::continuation_lut: return "continuation-lut";
  }
  return "invalid";
}
int serve(Options options) {
  Profile current{0, options.geometry, options.width, 0};
  auto policy = make_policy(current, Pressure::low);
  // A fixed bounded bank makes later width adoption independent of allocation.
  // Only preferred_width entries are active; vacant capacity is skipped by Ring.
  auto work = std::make_unique<NativeWork>(options.geometry, 64, options.nodes, options.cpu);
  std::mutex mutex;
  std::deque<Message> queue;
  std::jthread reader([&](std::stop_token token) {
    std::string pending;
    auto post = [&](Message message) {
      std::lock_guard lock(mutex);
      // Stop/EOF settlement must remain possible under control saturation.
      if (queue.size() < 32 || message.kind == Message::stop) queue.push_back(message);
    };
    while (!token.stop_requested()) {
      pollfd fd{STDIN_FILENO, POLLIN | POLLHUP, 0};
      const auto ready = ::poll(&fd, 1, 50);
      if (ready < 0) { post(Message{Message::stop, {}, Pressure::low}); return; }
      if (!ready) continue;
      char buffer[4096];
      const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
      if (count <= 0) { post(Message{Message::stop, {}, Pressure::low}); return; }
      for (ssize_t i = 0; i < count; ++i) {
        if (buffer[i] == '\n') {
          const auto message = parse(pending);
          post(message);
          pending.clear();
          if (message.kind == Message::stop) return;
        } else {
          pending += buffer[i];
          if (pending.size() > 1024) { post(Message{Message::stop, {}, Pressure::low}); return; }
        }
      }
    }
  });
  std::cout << "{\"event\":\"ready\",\"revision\":0,\"width\":" << options.width
            << ",\"line\":" << options.geometry.line_bytes << ",\"page\":" << options.geometry.page_bytes
            << ",\"procedures\":";
  print_policy(policy);
  std::cout << "}\n" << std::flush;
  const auto start = Clock::now();
  auto last = start;
  std::uint64_t loads = 0, checksum = 0, batches = 0;
  bool stopping = false;
  while (!stopping) {
    // Every native batch is fully drained before a revision is adopted.
    const auto stats = work->batch(policy, Dispatch::code_lut_switch, 256,
                                  batches % 2 ? Pattern::linear : Pattern::irregular);
    loads += stats.loads;
    checksum += stats.checksum;
    ++batches;
    std::deque<Message> received;
    { std::lock_guard lock(mutex); received.swap(queue); }
    for (const auto& message : received) {
      if (message.kind == Message::stop) { stopping = true; break; }
      if (message.kind == Message::rejected) {
        std::cout << "{\"event\":\"rejected\",\"reason\":\"syntax\"}\n" << std::flush;
        continue;
      }
      if (message.kind == Message::profile) {
        const auto& value = message.value;
        if (value.revision <= current.revision || value.geometry.line_bytes != options.geometry.line_bytes ||
            value.geometry.page_bytes != options.geometry.page_bytes || !value.preferred_width ||
            value.preferred_width > 64 || value.training_accesses > 64) {
          std::cout << "{\"event\":\"rejected\",\"reason\":\"profile\"}\n" << std::flush;
          continue;
        }
        const auto apply_start = Clock::now();
        current = value;
        policy = make_policy(current, policy.pressure);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - apply_start).count();
        std::cout << "{\"event\":\"applied\",\"revision\":" << current.revision
                  << ",\"loads\":" << loads << ",\"width\":" << current.preferred_width
                  << ",\"training\":" << current.training_accesses << ",\"apply_ns\":" << elapsed
                  << ",\"procedures\":";
        print_policy(policy);
        std::cout << "}\n" << std::flush;
      } else {
        policy = make_policy(current, message.load);
        std::cout << "{\"event\":\"pressure\",\"revision\":" << current.revision
                  << ",\"value\":\"" << (message.load == Pressure::low ? "low" : "busy")
                  << "\",\"procedures\":";
        print_policy(policy);
        std::cout << "}\n" << std::flush;
      }
    }
    const auto now = Clock::now();
    if (now - last >= std::chrono::milliseconds(50)) {
      std::cout << "{\"event\":\"progress\",\"revision\":" << current.revision << ",\"loads\":" << loads
                << ",\"checksum\":" << checksum << ",\"batches\":" << batches << "}\n" << std::flush;
      last = now;
    }
    if (now - start > std::chrono::seconds(120)) stopping = true;
  }
  reader.request_stop();
  reader.join();
  work.reset();
  std::cout << "{\"event\":\"stopped\",\"revision\":" << current.revision
            << ",\"loads\":" << loads << ",\"checksum\":" << checksum << "}\n" << std::flush;
  return 0;
}
int bench(Options options) {
  NativeWork work(options.geometry, options.width, options.nodes, options.cpu);
  constexpr std::array paths{Dispatch::fixed_inline, Dispatch::code_lut_switch,
                             Dispatch::procedure_lut, Dispatch::continuation_lut};
  for (auto pattern : {Pattern::irregular, Pattern::linear})
    for (auto training : {0u, 3u})
      for (auto pressure : {Pressure::low, Pressure::busy}) {
        const auto policy = make_policy(Profile{1, options.geometry, options.width, training}, pressure);
        for (std::size_t trial = 0; trial < options.reps; ++trial) {
          std::optional<NativeStats> reference;
          for (unsigned arm = 0; arm < paths.size(); ++arm) {
            const auto path = paths[(arm + trial) % paths.size()];
            const auto result = work.batch(policy, path, options.rounds, pattern);
            if (reference && (reference->loads != result.loads || reference->checksum != result.checksum)) {
              std::cerr << "Matched dispatch arms produced different useful work\n";
              return 1;
            }
            reference = result;
            std::cout << "{\"event\":\"sample\",\"path\":\"" << dispatch_name(path)
                      << "\",\"pattern\":\"" << (pattern == Pattern::irregular ? "irregular" : "linear")
                      << "\",\"training\":" << training << ",\"pressure\":\""
                      << (pressure == Pressure::low ? "low" : "busy") << "\",\"trial\":" << trial
                      << ",\"width\":" << options.width << ",\"nodes\":" << options.nodes
                      << ",\"loads\":" << result.loads << ",\"visits\":" << result.visits
                      << ",\"checksum\":" << result.checksum << ",\"elapsed_ns\":" << result.elapsed_ns << "}\n";
          }
        }
      }
  return 0;
}
}
int main(int argc, char** argv) {
  Options options;
  const std::string command = argc > 1 ? argv[1] : "bench";
  try {
    for (int i = 2; i < argc; i += 2) {
      if (i + 1 == argc) throw std::invalid_argument("missing option value");
      const std::string option = argv[i];
      const auto value = std::stoull(argv[i + 1]);
      if (option == "--line") options.geometry.line_bytes = value;
      else if (option == "--page") options.geometry.page_bytes = value;
      else if (option == "--width") options.width = value;
      else if (option == "--nodes") options.nodes = value;
      else if (option == "--rounds") options.rounds = value;
      else if (option == "--reps") options.reps = value;
      else if (option == "--cpu") options.cpu = value;
      else throw std::invalid_argument("unknown option");
    }
    if (!valid_geometry(options.geometry) || !options.width || options.width > 64 ||
        options.nodes < 32 || options.nodes > (1u << 22) || !options.rounds || options.rounds > (1u << 24) ||
        !options.reps || options.reps > 100) throw std::invalid_argument("option outside spike bound");
    if (command == "serve") return serve(options);
    if (command == "bench") return bench(options);
    throw std::invalid_argument("expected serve or bench");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
