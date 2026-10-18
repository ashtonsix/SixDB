#include <loom/execution.h>

namespace loom::execution {

orbital::Result<Policy> Policy::bind(Options options) noexcept {
  if (!options.cheap_ns_per_request || !options.max_age || !options.minimum_samples ||
      !options.minimum_independent || !options.sample_interval)
    return std::unexpected(orbital::Error::invalid_argument);
  return Policy(options);
}

void Policy::reset() noexcept {
  forget_sample();
  sample_epoch_ = last_epoch_ = 0;
}

bool Policy::observe(Epoch now, SerialSample sample) noexcept {
  if (!sample.elapsed_ns || !sample.completed_requests) return false;
  const auto cost = sample.elapsed_ns / sample.completed_requests +
      std::uint64_t(sample.elapsed_ns % sample.completed_requests != 0);
  touch(now);
  sample_epoch_ = now;
  if (cost > options_.cheap_ns_per_request) {
    state_ = SampleState::expensive;
    cheap_samples_ = 0;
    if (expensive_samples_ < 2) ++expensive_samples_;
    remaining_ = expensive_samples_ == 1 ? 1 : options_.sample_interval;
  } else {
    state_ = SampleState::cheap;
    expensive_samples_ = 0;
    if (cheap_samples_ != std::numeric_limits<std::uint16_t>::max()) ++cheap_samples_;
    remaining_ = confirmed() ? options_.sample_interval : 1;
  }
  return true;
}

} // namespace loom::execution
