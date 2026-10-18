#include <loom/execution.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace loom::execution;
std::uint64_t checks{};

void check(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    std::cerr << "loom_cold_promotion_check: " << message << '\n';
    std::abort();
  }
}

static_assert(sizeof(ColdPromotion) == 16);
static_assert(!std::is_convertible_v<DirectBatchSample, SerialSample>);
static_assert(!std::is_convertible_v<SerialSample, DirectBatchSample>);

ColdPromotion promotion(ColdPromotionOptions options = {13, 16, 2}) {
  auto result = ColdPromotion::bind(options);
  check(bool(result), "qualified options refused");
  return *result;
}

void options() {
  constexpr ColdPromotionOptions good{13, 16, 2};
  std::array invalid{good, good, good, good};
  invalid[0].batch_requests = 0;
  invalid[1].cheap_ns_per_request = 0;
  invalid[2].cheap_confirmations = 0;
  invalid[3].cheap_confirmations = 65536;
  for (const auto value : invalid) {
    const auto result = ColdPromotion::bind(value);
    check(!result && result.error() == orbital::Error::invalid_argument,
        "invalid options accepted or wrong error returned");
  }
  check(!ColdPromotion::bind({}), "default options invented a grain or performance cutoff");
  auto state = promotion();
  check(state.options().batch_requests == 13 && state.options().cheap_ns_per_request == 16 &&
      state.options().cheap_confirmations == 2, "bound options changed");
  state.reset();
  check(state.options().batch_requests == 13 && state.options().cheap_ns_per_request == 16 &&
      state.options().cheap_confirmations == 2, "reset changed immutable options");
}

void boundary_and_rejection() {
  auto state = promotion();
  check(!state.observe({208, 13}), "first inclusive-cutoff direct sample promoted prematurely");
  check(!state.observe({0, 13}), "zero elapsed time promoted");
  check(!state.observe({208, 0}) && !state.observe({208, 12}) && !state.observe({1, 14}),
      "zero, partial or oversized grain promoted");
  check(state.observe({208, 13}), "invalid observations changed the first cheap confirmation");
  check(!state.observe({1, 13}) && !state.observe({209, 13}) && !state.observe({1, 13}),
      "latched state requested a duplicate promotion or implicitly demoted");

  state.reset();
  check(!state.observe({208, 13}), "reset retained a prior confirmation");
  check(!state.observe({209, 13}), "a fraction above reciprocal cutoff promoted");
  check(!state.observe({208, 13}), "expensive valid observation did not break confirmation");
  check(state.observe({207, 13}), "new consecutive cheap observations failed to promote");
  state.reset();
  check(!state.observe({1, std::numeric_limits<std::uint32_t>::max()}),
      "wrong maximum grain contributed confirmation");
  check(!state.observe({208, 13}) && state.observe({208, 13}),
      "wrong maximum grain corrupted new confirmation sequence");
}

void independent_traces() {
  // Independent run-length oracle over all short cheap/expensive histories.
  // Insert invalid observations without treating them as useful evidence.
  for (unsigned required = 1; required <= 5; ++required) {
    for (unsigned history = 0; history < 1024; ++history) {
      auto state = promotion({97, 10, required});
      unsigned run = 0;
      bool latched = false;
      unsigned events = 0;
      for (unsigned step = 0; step < 10; ++step) {
        check(!state.observe({0, 97}) && !state.observe({970, 96}),
            "invalid observations cannot request promotion");
        const bool cheap = history & (1u << step);
        bool expected = false;
        if (!latched) {
          run = cheap ? run + 1 : 0;
          if (run >= required) { expected = true; latched = true; }
        }
        const bool actual = state.observe({cheap ? 970u : 971u, 97});
        check(actual == expected, "promotion differs from independent consecutive-run oracle");
        events += actual;
      }
      check(events <= 1, "one cold episode emitted multiple registration requests");
      state.reset();
      for (unsigned i = 1; i <= required; ++i)
        check(state.observe({970, 97}) == (i == required),
            "explicit demotion reset cannot resurrect the old episode");
    }
  }
}

void wide_arithmetic_and_confirmation_count() {
  constexpr auto max32 = std::numeric_limits<std::uint32_t>::max();
  constexpr auto max64 = std::numeric_limits<std::uint64_t>::max();
  const auto ceiling = std::uint64_t(max32) * max32;
  auto state = promotion({max32, max32, 2});
  check(!state.observe({ceiling, max32}), "maximum exact product promoted too early");
  check(!state.observe({max64, max32}), "maximum elapsed time overflowed into cheap");
  check(!state.observe({ceiling, max32}) && state.observe({ceiling, max32}),
      "wide expensive observation failed to reset or exact product was rounded");

  auto many = promotion({1, 1, 65535});
  for (unsigned i = 1; i <= 65535; ++i)
    check(many.observe({1, 1}) == (i == 65535), "maximum cheap count wrapped or promoted early");
  check(!many.observe({1, 1}) && !many.observe({max64, 1}) && !many.observe({1, 1}),
      "maximum count lost one-shot latch");
  many.reset();
  check(!many.observe({1, 1}), "maximum-count reset retained evidence");
}
} // namespace

int main() {
  options();
  boundary_and_rejection();
  independent_traces();
  wide_arithmetic_and_confirmation_count();
  std::cout << "loom_cold_promotion_check: " << checks << " independent checks passed\n";
}
