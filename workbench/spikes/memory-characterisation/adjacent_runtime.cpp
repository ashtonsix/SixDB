// Exercise the real cold service, isolating forward learning from the scheduling
// sweeps with explicitly synthetic known fields. These are test premises, not
// measured width or training facts for the machine.
#include <loom/hardware.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <sched.h>

int main() {
  using Clock = std::chrono::steady_clock;
  const auto context = loom::discover_hardware(sched_getcpu());
  if (!context || !context->qualified) return 1;
  const loom::HardwareTableEntry premise{
    context->signature, context->geometry, 8, 2, 1, true};
  loom::HardwareOptions options;
  options.table = {&premise, 1};
  options.limits = {1024 * 1024, 2 * 1024 * 1024, std::chrono::milliseconds(500)};
  loom::HardwareService service(options);
  const auto begin = Clock::now();
  const auto handle = service.request(*context);
  if (!handle) return 2;
  auto snapshot = (*handle)->snapshot();
  const auto initial_revision = snapshot.profile.revision;
  while (snapshot.state == loom::HardwareState::pending) {
    if (Clock::now() - begin > std::chrono::seconds(2)) return 3;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    snapshot = (*handle)->snapshot();
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
  unsigned widths{}, training{}, forward{};
  for (const auto& entry : loom::hardware_table()) {
    widths += entry.preferred_width != 0;
    training += entry.training_accesses != 0;
    for (const auto value : entry.adjacent_forward)
      forward += value != loom::ForwardObservation::unknown;
  }
  std::cout << "{\"schema\":1,\"fixture\":\"real-service-forward-only-synthetic-scheduling-premise\","
    << "\"cpu\":" << context->cpu << ",\"line_bytes\":" << context->geometry.line_bytes
    << ",\"page_bytes\":" << context->geometry.page_bytes
    << ",\"state\":" << unsigned(snapshot.state)
    << ",\"initial_revision\":" << initial_revision << ",\"final_revision\":" << snapshot.profile.revision
    << ",\"adjacent_forward\":[" << unsigned(snapshot.adjacent_forward[0]) << ','
    << unsigned(snapshot.adjacent_forward[1]) << "],\"elapsed_ns\":" << elapsed
    << ",\"mapping_limit\":1048576,\"memory_limit\":2097152,\"wall_limit_ms\":500,"
    << "\"retained_table\":{\"contexts\":" << loom::hardware_table().size()
    << ",\"width_candidates\":" << widths << ",\"training_candidates\":" << training
    << ",\"forward_known\":" << forward << "}}\n";
  return snapshot.state == loom::HardwareState::calibrated_local ? 0 : 4;
}
