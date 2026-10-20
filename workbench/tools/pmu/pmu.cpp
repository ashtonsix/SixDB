#include "pmu.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <linux/perf_event.h>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace workbench::pmu {
namespace {
bool valid_name(std::string_view value) {
  auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (value.empty() || value.size() > 64 || !letter(value.front())) return false;
  for (char c : value)
    if (!letter(c) && !(c >= '0' && c <= '9') && c != '_' && c != '.' && c != '-') return false;
  return true;
}
template<class T> T number(std::string_view text) {
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) { text.remove_prefix(2); base = 16; }
  if (text.empty()) throw std::invalid_argument("PMU event number is empty");
  T result{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, base);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw std::invalid_argument("PMU event number is malformed or out of range");
  return result;
}
void quoted(std::ostream& out, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << char(c);
    else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
    else out << char(c);
  }
  out << '"';
}
struct Failure {
  const char* phase{};
  int code{}; // Zero denotes a protocol/lifecycle failure, not a syscall errno.
  const char* reason{};
};
void failure_json(std::ostream& out, const Failure& error) {
  out << "{\"phase\":"; quoted(out, error.phase);
  out << ",\"errno\":";
  if (error.code) out << error.code; else out << "null";
  out << ",\"message\":";
  quoted(out, error.code ? std::strerror(error.code) : error.reason);
  out << '}';
}
const char* mode_name(Mode mode) {
  switch (mode) {
    case Mode::off: return "off";
    case Mode::basic: return "basic";
    case Mode::stalls: return "stalls";
  }
  throw std::invalid_argument("invalid PMU mode");
}
}

EventSpec parse_event_spec(std::string_view input) {
  const auto first = input.find(':');
  const auto second = first == input.npos ? input.npos : input.find(':', first + 1);
  if (first == input.npos || second == input.npos || input.find(':', second + 1) != input.npos)
    throw std::invalid_argument("PMU event must be name:type:config");
  const auto name = input.substr(0, first);
  if (!valid_name(name)) throw std::invalid_argument("PMU event name must start with an ASCII letter and use letters, digits, _, . or - (up to 64 bytes)");
  return {std::string(name), number<std::uint32_t>(input.substr(first + 1, second - first - 1)),
          number<std::uint64_t>(input.substr(second + 1))};
}

struct Session::Impl {
  enum class State { off, ready, running, complete, unavailable, failed };
  struct Event {
    EventSpec spec;
    bool required{}, opened{}, in_group{};
    int fd{-1};
    std::optional<std::uint64_t> id, count;
    std::optional<Failure> error;
    Event(EventSpec initial, bool mandatory = false) : spec(std::move(initial)), required(mandatory) {}
  };
  Mode mode;
  State state{State::off};
  std::thread::id thread{std::this_thread::get_id()};
  std::vector<Event> events;
  std::vector<std::uint64_t> read_buffer;
  std::array<Failure, 8> errors{};
  std::size_t error_count{}, group_size{};
  bool errors_truncated{}, invalid_interval{};
  std::uint64_t enabled{}, running{};

  explicit Impl(Mode selected, std::span<const EventSpec> extras) : mode(selected) {
    (void)mode_name(mode);
    if (mode == Mode::off) return;
    events.reserve((mode == Mode::stalls ? 4 : 2) + extras.size());
    events.push_back({{"cpu_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES}, true});
    events.push_back({{"retired_instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS}, true});
    if (mode == Mode::stalls) {
      events.push_back({{"frontend_stall_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND}});
      events.push_back({{"backend_stall_cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND}});
    }
    for (const auto& spec : extras) {
      if (!valid_name(spec.name)) throw std::invalid_argument("invalid PMU event name");
      for (const auto& event : events)
        if (spec.name == event.spec.name) throw std::invalid_argument("duplicate PMU event name: " + spec.name);
      events.push_back({spec});
    }
    // Allocate before owning raw FDs: a throwing constructor has no ~Impl.
    read_buffer.resize(3 + 2 * events.size());
    // A required pair failure prevents enabling a partial, misleading IPC group.
    if (!open(events[0], -1) || !open(events[1], events[0].fd)) {
      state = State::unavailable;
      error("setup", 0, "required cycles/instructions group unavailable");
      close_all();
      return;
    }
    for (std::size_t i = 2; i < events.size(); ++i) (void)open(events[i], events[0].fd);
    for (const auto& event : events) group_size += event.in_group;
    read_buffer.resize(3 + 2 * group_size);
    state = State::ready;
  }
  ~Impl() { close_all(); }
  void close_all() noexcept {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (it->fd >= 0) ::close(it->fd);
      it->fd = -1;
    }
  }
  void error(const char* phase, int code, const char* reason = nullptr) noexcept {
    invalid_interval = true;
    if (error_count < errors.size()) errors[error_count++] = {phase, code, reason};
    else errors_truncated = true;
  }
  bool open(Event& event, int leader) {
    perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = event.spec.type;
    attr.config = event.spec.config;
    attr.disabled = leader == -1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID |
                       PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    const auto fd = ::syscall(SYS_perf_event_open, &attr, 0, -1, leader, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0) { event.error = {"open", errno, nullptr}; return false; }
    event.fd = static_cast<int>(fd);
    event.opened = true;
    std::uint64_t id{};
    if (::ioctl(event.fd, PERF_EVENT_IOC_ID, &id) < 0) {
      event.error = {"id", errno, nullptr};
      ::close(event.fd); event.fd = -1;
      return false;
    }
    event.id = id;
    event.in_group = true;
    return true;
  }
  void start() noexcept {
    if (state == State::off || state == State::unavailable) return;
    if (state != State::ready) {
      error("start", 0, "Session permits exactly one start/stop interval");
      if (state != State::running) state = State::failed;
      return;
    }
    if (thread != std::this_thread::get_id()) {
      error("start", 0, "Session must start on its constructing thread"); state = State::failed; return;
    }
    if (::ioctl(events[0].fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0) {
      error("reset", errno); state = State::failed; return;
    }
    if (::ioctl(events[0].fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0) {
      error("enable", errno); state = State::failed; return;
    }
    state = State::running;
  }
  void stop() noexcept {
    if (state == State::off || state == State::unavailable || state == State::failed) return;
    if (state != State::running) {
      error("stop", 0, "Session must stop once after start"); state = State::failed; return;
    }
    if (thread != std::this_thread::get_id())
      error("stop", 0, "Session must stop on its constructing thread");
    if (::ioctl(events[0].fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0) error("disable", errno);
    const auto bytes = ::read(events[0].fd, read_buffer.data(), read_buffer.size() * sizeof(std::uint64_t));
    if (bytes < 0) error("read", errno);
    else if (bytes == 0) error("read", 0, "group read returned EOF; counters were not scheduled");
    else if (static_cast<std::size_t>(bytes) != read_buffer.size() * sizeof(std::uint64_t))
      error("read", 0, "group read size differs from prepared event set");
    else if (read_buffer[0] != group_size) error("read", 0, "group event count differs from prepared event set");
    else {
      enabled = read_buffer[1]; running = read_buffer[2];
      if (!enabled || !running || running > enabled)
        error("read", 0, "invalid or zero group enabled/running time");
      // IDs, not event ordering, bind values to the successfully opened set.
      for (auto& event : events) {
        if (!event.in_group) continue;
        std::size_t matches = 0;
        for (std::size_t i = 0; i < group_size; ++i) {
          if (read_buffer[4 + 2 * i] == *event.id) {
            ++matches; event.count = read_buffer[3 + 2 * i];
          }
        }
        if (matches != 1) error("read", 0, "group IDs differ from prepared event set");
      }
      if (!events[0].count.value_or(0) || !events[1].count.value_or(0))
        error("read", 0, "required cycles/instructions returned zero");
    }
    state = invalid_interval ? State::failed : State::complete;
  }
  void write_json(std::ostream& destination) const {
    std::ostringstream out;
    out.imbue(std::locale::classic()); out << std::setprecision(17);
    const bool measured = state == State::complete;
    const bool multiplexed = measured && running < enabled;
    const char* status = state == State::off ? "disabled" : state == State::ready ? "not_started" :
                         state == State::running ? "running" : state == State::unavailable ? "unavailable" :
                         state == State::failed ? "error" : multiplexed ? "multiplexed" : "complete";
    out << "{\"mode\":"; quoted(out, mode_name(mode));
    out << ",\"status\":"; quoted(out, status);
    out << ",\"scope\":\"calling_thread_user_space\",\"pid\":0,\"cpu\":-1,"
           "\"inherit\":false,\"exclude_kernel\":true,\"exclude_hv\":true,\"scaled\":false,";
    out << "\"time_enabled_ns\":";
    if (measured) out << enabled; else out << "null";
    out << ",\"time_running_ns\":";
    if (measured) out << running; else out << "null";
    out << ",\"coverage\":";
    if (measured) out << double(running) / double(enabled); else out << "null";
    out << ",\"multiplexed\":";
    if (measured) out << (multiplexed ? "true" : "false"); else out << "null";
    out << ",\"ipc\":";
    if (measured) out << double(*events[1].count) / double(*events[0].count); else out << "null";
    out << ",\"ipc_scope\":\"observed_same_group\",\"opened_event_set\":[";
    bool first = true;
    for (const auto& event : events) {
      if (!event.in_group) continue;
      if (!first) out << ','; first = false; quoted(out, event.spec.name);
    }
    out << "],\"errors\":[";
    for (std::size_t i = 0; i < error_count; ++i) { if (i) out << ','; failure_json(out, errors[i]); }
    out << "],\"errors_truncated\":" << (errors_truncated ? "true" : "false") << ",\"events\":[";
    first = true;
    for (const auto& event : events) {
      if (!first) out << ','; first = false;
      out << "{\"name\":"; quoted(out, event.spec.name);
      out << ",\"type\":" << event.spec.type << ",\"config\":\"0x" << std::hex << event.spec.config << std::dec << '"';
      out << ",\"required\":" << (event.required ? "true" : "false")
          << ",\"opened\":" << (event.opened ? "true" : "false")
          << ",\"in_group\":" << (event.in_group ? "true" : "false") << ",\"id\":";
      if (event.id) out << *event.id; else out << "null";
      out << ",\"status\":";
      quoted(out, event.error ? "unavailable" : !event.opened ? "not_attempted" :
                  measured && event.in_group ? "measured" : "not_measured");
      out << ",\"count\":";
      if (measured && event.in_group) out << *event.count; else out << "null";
      out << ",\"errors\":[";
      if (event.error) failure_json(out, *event.error);
      out << "]}";
    }
    out << "]}\n";
    destination << out.str();
  }
};

Session::Session(Mode mode, std::span<const EventSpec> extras) : impl_(std::make_unique<Impl>(mode, extras)) {}
Session::~Session() = default;
void Session::start() noexcept { impl_->start(); }
void Session::stop() noexcept { impl_->stop(); }
void Session::write_json(std::ostream& out) const { impl_->write_json(out); }
}
