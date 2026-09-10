#pragma once
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ikea::heterogeneous::measurement {
using U = std::uint64_t;
inline void require(bool condition, const char *why) {
  if (!condition)
    throw std::runtime_error(why);
}
inline U now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline int pin() {
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  require(!sched_getaffinity(0, sizeof allowed, &allowed), "affinity read");
  int selected = -1;
  if (auto *requested = std::getenv("SIXDB_CPU"))
    selected = std::stoi(requested);
  else
    for (int i = 0; i < CPU_SETSIZE; ++i)
      if (CPU_ISSET(i, &allowed)) {
        selected = i;
        break;
      }
  require(selected >= 0 && selected < CPU_SETSIZE &&
              CPU_ISSET(selected, &allowed),
          "CPU unavailable");
  cpu_set_t only;
  CPU_ZERO(&only);
  CPU_SET(selected, &only);
  require(!sched_setaffinity(0, sizeof only, &only), "pin");
  return selected;
}
struct Counters {
  int cycles_fd = -1, instructions_fd = -1;
  U cycles = 0, instructions = 0, enabled = 0, running = 0;
  std::string status = "not_requested";
  explicit Counters(bool requested) {
    if (!requested)
      return;
    perf_event_attr attr{};
    attr.size = sizeof attr;
    attr.type = PERF_TYPE_HARDWARE;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED |
                       PERF_FORMAT_TOTAL_TIME_RUNNING;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    cycles_fd = syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    if (cycles_fd >= 0) {
      attr.disabled = 0;
      attr.config = PERF_COUNT_HW_INSTRUCTIONS;
      instructions_fd =
          syscall(SYS_perf_event_open, &attr, 0, -1, cycles_fd, 0);
    }
    status = cycles_fd >= 0 && instructions_fd >= 0
                 ? "available"
                 : "unavailable_" + std::to_string(errno);
  }
  ~Counters() {
    if (cycles_fd >= 0)
      close(cycles_fd);
    if (instructions_fd >= 0)
      close(instructions_fd);
  }
  void start() {
    if (status != "available")
      return;
    if (ioctl(cycles_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) ||
        ioctl(cycles_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP))
      status = "enable_failed";
  }
  void stop() {
    if (status != "available")
      return;
    struct {
      U count, enabled, running, values[2];
    } result{};
    if (ioctl(cycles_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) ||
        read(cycles_fd, &result, sizeof result) != sizeof result ||
        result.count != 2) {
      status = "read_failed";
      return;
    }
    cycles = result.values[0];
    instructions = result.values[1];
    enabled = result.enabled;
    running = result.running;
    if (!running)
      status = "not_scheduled";
  }
};
} // namespace ikea::heterogeneous::measurement
