#include "native.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace loom_policy_spike;
namespace {
void check(bool condition, const char* reason) {
  if (!condition) { std::cerr << reason << '\n'; std::abort(); }
}
// Independent enumeration of issued addresses, including an unaligned page start.
std::vector<std::size_t> targets(Procedure procedure, Geometry geometry, std::size_t alignment) {
  std::vector<std::size_t> result;
  if (procedure == Procedure::none) return result;
  result.push_back(0);
  if (procedure == Procedure::distant_two) result.push_back(geometry.line_bytes);
  if (procedure == Procedure::distant_four)
    for (unsigned i = 1; i < 4; ++i) result.push_back(i * geometry.line_bytes);
  unsigned pages = 0;
  switch (procedure) {
    case Procedure::page_one: pages = 1; break;
    case Procedure::page_two: pages = 2; break;
    case Procedure::page_four: pages = 4; break;
    case Procedure::page_eight: pages = 8; break;
    default: break;
  }
  const auto next = geometry.page_bytes - alignment;
  for (unsigned i = 0; i < pages; ++i) {
    result.push_back(next + i * geometry.page_bytes);
    result.push_back(next + i * geometry.page_bytes + geometry.line_bytes);
  }
  return result;
}
void qualification() {
  check(!bind_code(Pattern::linear, 65536, 0, {}), "empty span admitted");
  check(!bind_code(Pattern::linear, 0, 65536, {}), "empty demand admitted");
  check(!bind_code(static_cast<Pattern>(2), 64, 64, {}), "invalid pattern admitted");
  check(!bind_code(Pattern::linear, 64, 64, {64, 1000}), "invalid page geometry admitted");
  check(bind_code(Pattern::linear, 65536, 127, {}) == 8, "tiny span widened by approximate demand");
  check(bind_code(Pattern::linear, 65536, 128, {}) == 9, "exact next extent floor lost");
  check(bind_code(Pattern::linear, 256, 65536, {}) == 10, "demand widened to available capacity");
  check(bind_code(Pattern::irregular, 64, 1, {}) == 0, "single occupied target rejected");
  for (Geometry geometry : {Geometry{64, 4096}, Geometry{128, 4096}, Geometry{64, 65536}})
    for (unsigned training : {0u, 2u, 6u})
      for (Pressure pressure : {Pressure::low, Pressure::busy}) {
        auto policy = make_policy({1, geometry, 8, training}, pressure);
        for (unsigned code = 0; code < demand_count; ++code)
          for (std::size_t alignment = 0; alignment < geometry.page_bytes; ++alignment)
            for (auto offset : targets(policy.procedures[code], geometry, alignment))
              check(offset < envelope_bytes(code), "policy revision issues an unqualified target");
      }
  auto unknown = make_policy({0, {}, 8, 0}, Pressure::low);
  auto trained = make_policy({1, {}, 8, 3}, Pressure::low);
  auto busy = make_policy({1, {}, 8, 3}, Pressure::busy);
  check(unknown.procedures[15] == Procedure::distant_one, "unknown training asserted a bootstrap");
  check(trained.procedures[15] == Procedure::page_eight, "qualified broad demand could not adopt bootstrap");
  check(busy.procedures[15] == Procedure::none, "pressure could not suppress hints");
  check(make_policy({1, {64, 65536}, 8, 3}, Pressure::low).procedures[15] == Procedure::distant_four,
        "larger pages widened beyond retained span");
}
void native() {
  for (Geometry geometry : {Geometry{64, 4096}, Geometry{128, 4096}}) {
    NativeWork work(geometry, 4, 1024);
    for (auto pattern : {Pattern::irregular, Pattern::linear})
      for (auto training : {0u, 3u})
        for (auto pressure : {Pressure::low, Pressure::busy}) {
          const auto policy = make_policy({1, geometry, 4, training}, pressure);
          const auto fixed = work.batch(policy, Dispatch::fixed_inline, 127, pattern);
          const auto procedure = work.batch(policy, Dispatch::procedure_lut, 127, pattern);
          const auto switched = work.batch(policy, Dispatch::code_lut_switch, 127, pattern);
          const auto continuation = work.batch(policy, Dispatch::continuation_lut, 127, pattern);
          check(fixed.loads == procedure.loads && fixed.loads == continuation.loads && fixed.loads == switched.loads,
                "policy dispatch changed completed loads");
          check(fixed.checksum == procedure.checksum && fixed.checksum == continuation.checksum && fixed.checksum == switched.checksum,
                "policy dispatch changed useful values");
          check(fixed.visits == procedure.visits && fixed.visits == continuation.visits && fixed.visits == switched.visits,
                "policy dispatch changed continuation progress");
          check(fixed.loads == 4 * 127 * (pattern == Pattern::linear ? 16 : 1),
                "completed work differs from independent expected extent");
        }
    for (unsigned width : {1u, 4u, 2u}) {
      const auto changed = make_policy({width, geometry, width, 3}, Pressure::low);
      check(work.batch(changed, Dispatch::continuation_lut, 31).loads == width * 31,
            "changed width did not use prepared bank capacity");
    }
  }
}
}
int main() {
  qualification();
  native();
  std::cout << "PASS: bounded demand classes, changing geometry/pressure/training, matched native arms\n";
}
