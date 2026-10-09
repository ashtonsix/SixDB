#include "probe.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

void check(bool valid) { if (!valid) std::abort(); }
int main() {
  using namespace loom_metadata;
  const std::array<Word, 2> storage{};
  const auto* a = &storage[0]; const auto* b = &storage[1];
  check(pass8({a}).anchor == a);
  const auto p16 = pass16({a, std::numeric_limits<Word>::max()});
  check(p16.anchor == a && p16.cut == 0);
  const auto p32 = pass32({a, b, 37, std::numeric_limits<Word>::max()});
  check(p32.anchor == a && p32.descriptor == b && p32.cut == 38 && p32.generation == 0);
  const auto success = pass_result16(Result16::success(a, b));
  check(success.anchor == a && success.detail.descriptor == b);
  for (const auto error : {orbital::Error::invalid_argument, orbital::Error::absent,
      orbital::Error::conflict, orbital::Error::stale, orbital::Error::unavailable,
      orbital::Error::capacity, orbital::Error::io, orbital::Error::rejected}) {
    const auto refusal = pass_result16(Result16::failure(error));
    check(!refusal.anchor && refusal.detail.error == error);
    const auto failed = pass_progress(orbital::Progress::fail(error));
    check(failed.kind() == orbital::Progress::Kind::failed && failed.failure() == error);
  }
  orbital::Signal signal;
  for (Word generation : {Word{0}, Word{1}, Word{1} << 63, std::numeric_limits<Word>::max()}) {
    const auto wait = pass_progress(orbital::Progress::wait(signal, generation));
    check(wait.kind() == orbital::Progress::Kind::waiting && wait.signal() == &signal &&
          wait.observed_generation() == generation);
  }
  check(pass_progress(orbital::Progress::done()).kind() == orbital::Progress::Kind::done);
  check(pass_progress(orbital::Progress::yield()).kind() == orbital::Progress::Kind::yielded);
  const auto wrapper = checked16({a, 37});
  check(wrapper && wrapper->anchor == a && wrapper->cut == 37);
  std::puts("loom metadata packet controls: passed");
}
