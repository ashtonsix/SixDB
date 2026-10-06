#include "support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sched.h>

#if defined(LOOM_ABI_ORDINARY)
#define LOOM_ABI_CC
#elif defined(LOOM_ABI_REGCALL)
#if !defined(__x86_64__)
#error "This regcall comparison requires x86-64"
#endif
#define LOOM_ABI_CC __attribute__((regcall))
#elif defined(__x86_64__) || defined(__aarch64__)
#define LOOM_ABI_CC __attribute__((preserve_none))
#else
#error "This private ABI comparison requires x86-64 or AArch64"
#endif

namespace {
using Vector = std::uint64_t __attribute__((vector_size(16)));
using Scalars = std::array<std::uint64_t, 10>;
constexpr std::uint64_t salt = 0x9e3779b97f4a7c15ULL;

std::uint64_t mix(std::uint64_t value) noexcept {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::uint64_t triangular(std::uint64_t n) noexcept {
  // Division before multiplication preserves the intended modulo-2^64 sum.
  return (n & 1) ? n * ((n - 1) / 2) : (n / 2) * (n - 1);
}

struct Counters {
  std::uint64_t visits{}, completed{}, spins{}, budget_stops{}, skipped_slots{};
};

template <class Sequence> class Ring;
template <std::size_t... I> class Ring<std::index_sequence<I...>> {
  template <std::size_t> using VectorArgument = Vector;

 public:
  static constexpr std::size_t vector_count = sizeof...(I);
  using Vectors = std::array<Vector, vector_count>;
  struct Slot;
  // Twelve scalar/pointer arguments exceed the ordinary SysV/AAPCS scalar
  // register budget. Vectors are separate live values, not an aggregate ABI.
  typedef LOOM_ABI_CC void (*Function)(Ring*, Slot*, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
    std::uint64_t, std::uint64_t, std::uint64_t, VectorArgument<I>...);
  struct alignas(64) Slot {
    Scalars scalar{};
    Vectors vector{};
    Function function{};
    std::uint64_t identity{}, phase{};
    bool alternate{}, active{};
  };

  Ring(std::size_t width, std::size_t jobs, std::uint64_t steps, bool heterogeneous)
      : slots_(width), steps_(steps), live_(jobs) {
    if (!width || !jobs || jobs > width || !steps)
      throw std::invalid_argument("ABI ring requires 1..width jobs and positive steps");
    for (std::size_t i = 0; i != jobs; ++i) {
      auto& slot = slots_[i];
      slot.identity = i;
      slot.alternate = heterogeneous && (i & 1);
      slot.active = true;
      slot.function = slot.alternate ? callback<true> : callback<false>;
      for (unsigned k = 0; k != slot.scalar.size(); ++k)
        slot.scalar[k] = scalar_initial(i, k);
      for (unsigned j = 0; j != vector_count; ++j)
        slot.vector[j] = vector_initial(i, j);
    }
  }
  Ring(const Ring&) = delete;
  Ring& operator=(const Ring&) = delete;
  Ring(Ring&&) = delete;
  Ring& operator=(Ring&&) = delete;

  void spin() {
    spin_visits_ = 0;
    ++counters_.spins;
    enter(this);
    // Only the quantum counter advances in the callback/yield loop.
    counters_.visits += spin_visits_;
  }

  void manual_spin() {
    spin_visits_ = 0;
    ++counters_.spins;
    while (live_ && spin_visits_ != 128) {
      auto& slot = *next();
      if (slot.alternate) advance<true>(slot, slot.scalar, slot.vector);
      else advance<false>(slot, slot.scalar, slot.vector);
      ++spin_visits_;
      retire_if_done(slot);
    }
    counters_.visits += spin_visits_;
    if (live_) ++counters_.budget_stops;
  }

  void direct() {
    ++counters_.spins;
    for (auto& slot : slots_) {
      if (!slot.active) continue;
      const auto first_phase = slot.phase;
      while (slot.phase != steps_) {
        if (slot.alternate) advance<true>(slot, slot.scalar, slot.vector);
        else advance<false>(slot, slot.scalar, slot.vector);
      }
      counters_.visits += slot.phase - first_phase;
      retire_if_done(slot);
    }
  }

  bool pending() const noexcept { return live_ != 0; }
  const Counters& counters() const noexcept { return counters_; }
  static constexpr std::size_t slot_bytes() noexcept { return sizeof(Slot); }

  std::uint64_t verify(std::size_t jobs) const {
    if (live_ || counters_.completed != jobs || counters_.visits != jobs * steps_)
      throw std::runtime_error("ABI visit/completion count failed");
    std::uint64_t hash = 0;
    for (std::size_t i = 0; i != slots_.size(); ++i) {
      const auto& slot = slots_[i];
      if (i >= jobs) {
        if (slot.phase || slot.active)
          throw std::runtime_error("inactive ABI slot executed");
        continue;
      }
      if (slot.phase != steps_ || slot.active)
        throw std::runtime_error("ABI operation did not retire exactly once");
      // Closed-form expected values do not replay the candidate step loop.
      const auto series = triangular(steps_);
      for (unsigned k = 0; k != slot.scalar.size(); ++k) {
        const auto base = (k + 1) * salt + (i + 1) * 17 + (slot.alternate ? 101 : 0);
        const auto expected = scalar_initial(i, k) + steps_ * base + series * (k + 1);
        if (slot.scalar[k] != expected)
          throw std::runtime_error("scalar payload lost across ABI switch");
        hash += mix(slot.scalar[k] + k);
      }
      for (unsigned j = 0; j != vector_count; ++j) {
        const auto initial = vector_initial(i, j);
        const auto base0 = (i + 1) * (j + 1) + (slot.alternate ? 29 : 0);
        const auto base1 = salt * (j + 1) + (i + 1) * 31 + (slot.alternate ? 43 : 0);
        const auto expected0 = initial[0] + steps_ * base0 + series;
        const auto expected1 = initial[1] + steps_ * base1 + series * 2;
        if (slot.vector[j][0] != expected0 || slot.vector[j][1] != expected1)
          throw std::runtime_error("live vector payload lost across ABI switch");
        hash += mix(slot.vector[j][0] + 2 * j + 10);
        hash += mix(slot.vector[j][1] + 2 * j + 11);
      }
    }
    return hash;
  }

 private:
  std::vector<Slot> slots_;
  std::uint64_t steps_;
  std::size_t live_, cursor_{};
  std::uint64_t spin_visits_{};
  Counters counters_;

  static std::uint64_t scalar_initial(std::uint64_t identity, unsigned k) noexcept {
    return mix(identity * 1000 + k * 13);
  }
  static Vector vector_initial(std::uint64_t identity, unsigned j) noexcept {
    return {mix(salt * (identity + 1) + 17 * j), mix((identity + 7) ^ (j + 5))};
  }

  template <bool Alternate>
  [[gnu::always_inline]] static void advance(Slot& slot, Scalars& scalar, Vectors& vector) noexcept {
    const auto phase = slot.phase++;
    for (unsigned k = 0; k != scalar.size(); ++k)
      scalar[k] += (k + 1) * salt + (slot.identity + 1) * 17 +
        (Alternate ? 101 : 0) + phase * (k + 1);
    for (unsigned j = 0; j != vector_count; ++j) {
      const Vector delta{(slot.identity + 1) * (j + 1) + (Alternate ? 29 : 0) + phase,
        salt * (j + 1) + (slot.identity + 1) * 31 + (Alternate ? 43 : 0) + 2 * phase};
      vector[j] += delta;
    }
  }

  void retire_if_done(Slot& slot) noexcept {
    if (slot.phase == steps_) {
      slot.active = false;
      --live_;
      ++counters_.completed;
    }
  }

  Slot* next() noexcept {
    while (!slots_[cursor_].active) {
      ++counters_.skipped_slots;
      if (++cursor_ == slots_.size()) cursor_ = 0;
    }
    auto* slot = &slots_[cursor_];
    if (++cursor_ == slots_.size()) cursor_ = 0;
    return slot;
  }

  template <bool Alternate>
  static LOOM_ABI_CC void callback(Ring* ring, Slot* current,
    std::uint64_t a0, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3,
    std::uint64_t a4, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7,
    std::uint64_t a8, std::uint64_t a9, VectorArgument<I>... values) {
    Scalars scalar{a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
    Vectors vector{values...};
    advance<Alternate>(*current, scalar, vector);
    [[clang::musttail]] return yield(ring, current, scalar[0], scalar[1], scalar[2], scalar[3],
      scalar[4], scalar[5], scalar[6], scalar[7], scalar[8], scalar[9], vector[I]...);
  }

  static LOOM_ABI_CC void yield(Ring* ring, Slot* current,
    std::uint64_t a0, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3,
    std::uint64_t a4, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7,
    std::uint64_t a8, std::uint64_t a9, VectorArgument<I>... values) {
    if (current) {
      current->scalar = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
      current->vector = {values...};
      ring->retire_if_done(*current);
    }
    if (!ring->live_) return;
    if (ring->spin_visits_ == 128) {
      ++ring->counters_.budget_stops;
      return;
    }
    auto* next = ring->next();
    ++ring->spin_visits_;
    const auto scalar = next->scalar;
    const auto vector = next->vector;
    [[clang::musttail]] return next->function(ring, next, scalar[0], scalar[1], scalar[2], scalar[3],
      scalar[4], scalar[5], scalar[6], scalar[7], scalar[8], scalar[9], vector[I]...);
  }

  // Keep the ordinary outer caller's frame isolated from the experimental
  // callee-save convention. The same fixed entry is used in every ABI mode.
  // AArch64 preserve_none has the pinned-Clang frame-base hazard exercised by
  // the main microtask benchmark; only this fixed entry is uninstrumented.
  __attribute__((noinline, no_sanitize("address", "undefined")))
  static void enter(Ring* ring) {
    yield(ring, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, VectorArgument<I>{}...);
  }
};

const char* convention() noexcept {
#if defined(LOOM_ABI_ORDINARY)
  return "ordinary";
#elif defined(LOOM_ABI_REGCALL)
  return "regcall";
#else
  return "preserve_none";
#endif
}

template <std::size_t V>
loom_spike::Record run_case(const std::string& variant, std::size_t width,
  std::size_t jobs, std::uint64_t steps, bool heterogeneous, std::uint64_t repeat, int cpu) {
  Ring<std::make_index_sequence<V>> ring(width, jobs, steps, heterogeneous);
  const auto cpu_start = loom_spike::thread_cpu_ns();
  const auto start = loom_spike::now_ns();
  if (variant == "direct") ring.direct();
  else if (variant == "manual") while (ring.pending()) ring.manual_spin();
  else while (ring.pending()) ring.spin();
  const auto elapsed = loom_spike::now_ns() - start;
  const auto cpu_ns = loom_spike::thread_cpu_ns() - cpu_start;
  const auto hash = ring.verify(jobs);
  const auto& counters = ring.counters();
  loom_spike::Record record;
  record.family = "abi"; record.variant = variant; record.unit = "visit";
  record.repeat = repeat; record.elapsed_ns = elapsed; record.items = counters.visits;
  record.verified = sched_getcpu() == cpu;
  record.parameters = {{"calling_convention", convention()}, {"vectors", std::to_string(V)},
    {"scalar_payloads", "10"}, {"pointer_arguments", "2"}, {"ring", std::to_string(width)},
    {"jobs", std::to_string(jobs)}, {"steps_per_job", std::to_string(steps)},
    {"heterogeneous", heterogeneous ? "1" : "0"}, {"outer_visit_budget", "128"},
    {"requested_cpu", std::to_string(cpu)}, {"actual_cpu", std::to_string(sched_getcpu())},
    {"slot_bytes", std::to_string(ring.slot_bytes())},
    {"payload", "synthetic independent live scalar/vector arithmetic"},
    {"timed_region", "execute and switch; storage initialization and oracle excluded"}};
  record.counters = {{"visits", counters.visits}, {"completed", counters.completed},
    {"spins", counters.spins}, {"budget_stops", counters.budget_stops},
    {"skipped_slots", counters.skipped_slots}, {"payload_hash", hash}, {"cpu_ns", cpu_ns}};
  return record;
}

template <std::size_t V> std::uint64_t check_family(int cpu) {
  std::uint64_t cases = 0;
  for (const auto width : {1U, 16U, 32U})
    for (bool heterogeneous : {false, true}) {
      std::uint64_t expected_hash = 0;
      for (const auto* variant : {"direct", "manual", "ring"}) {
        const auto record = run_case<V>(variant, width, width, 37, heterogeneous, 0, cpu);
        if (!record.verified) throw std::runtime_error("ABI CPU placement changed");
        const auto hash = record.counters.at("payload_hash");
        if (std::string(variant) == "direct") expected_hash = hash;
        else if (hash != expected_hash) throw std::runtime_error("ABI controls disagree");
        ++cases;
      }
      if (width > 1) {
        const auto manual = run_case<V>("manual", width, 1, 257, heterogeneous, 0, cpu);
        const auto tail = run_case<V>("ring", width, 1, 257, heterogeneous, 0, cpu);
        if (manual.counters.at("payload_hash") != tail.counters.at("payload_hash") ||
            !tail.counters.at("skipped_slots") || tail.counters.at("spins") != 3)
          throw std::runtime_error("ABI underfill or outer suspension failed");
        cases += 2;
      }
    }
  return cases;
}

template <std::size_t V>
[[gnu::noinline]] void check_realigned_caller(int cpu, unsigned dynamic_count) {
  alignas(64) volatile std::uint64_t fixed[64];
  auto* dynamic = static_cast<volatile std::uint64_t*>(
    __builtin_alloca(dynamic_count * sizeof(std::uint64_t)));
  for (unsigned i = 0; i != 64; ++i) fixed[i] = i + 3;
  for (unsigned i = 0; i != dynamic_count; ++i) dynamic[i] = i + 7;
  const auto record = run_case<V>("ring", 16, 16, 37, true, 0, cpu);
  if (!record.verified) throw std::runtime_error("realigned ABI caller moved CPU");
  for (unsigned i = 0; i != 64; ++i)
    if (fixed[i] != i + 3) throw std::runtime_error("ABI caller fixed frame corrupted");
  for (unsigned i = 0; i != dynamic_count; ++i)
    if (dynamic[i] != i + 7) throw std::runtime_error("ABI caller dynamic frame corrupted");
}

} // namespace

int main(int argc, char** argv) {
  try {
    const loom_spike::Options options(argc, argv);
    const auto cpus = options.cpus();
    const int cpu = cpus.front();
    loom_spike::pin(cpu);
    const auto repeat = options.number("repeat", 0);
    if (options.has("check")) {
      const auto cases = check_family<0>(cpu) + check_family<2>(cpu) +
        check_family<8>(cpu) + check_family<12>(cpu) + 4;
      check_realigned_caller<0>(cpu, 23);
      check_realigned_caller<2>(cpu, 23);
      check_realigned_caller<8>(cpu, 23);
      check_realigned_caller<12>(cpu, 23);
      loom_spike::Record record;
      record.family = "abi"; record.variant = "check"; record.unit = "case";
      record.repeat = repeat; record.items = cases; record.verified = true;
      record.parameters["calling_convention"] = convention();
      record.counters["checked_cases"] = cases;
      loom_spike::emit(record);
      return 0;
    }
    const auto variant = options.get("variant", "ring");
    if (variant != "ring" && variant != "manual" && variant != "direct")
      throw std::invalid_argument("ABI variant must be ring/manual/direct");
    const auto width = options.number("ring", 16);
    const auto jobs = options.number("jobs", width);
    const auto steps = options.number("steps", 1024);
    const auto vectors = options.number("vectors", 2);
    if (!width || width > 65536 || !jobs || jobs > width || !steps ||
        steps > 100000000 / jobs)
      throw std::invalid_argument("ABI geometry exceeds bounded storage/work");
    const bool heterogeneous = options.has("heterogeneous");
    loom_spike::Record record;
    switch (vectors) {
      case 0: record = run_case<0>(variant, width, jobs, steps, heterogeneous, repeat, cpu); break;
      case 2: record = run_case<2>(variant, width, jobs, steps, heterogeneous, repeat, cpu); break;
      case 8: record = run_case<8>(variant, width, jobs, steps, heterogeneous, repeat, cpu); break;
      case 12: record = run_case<12>(variant, width, jobs, steps, heterogeneous, repeat, cpu); break;
      default: throw std::invalid_argument("ABI vectors must be 0,2,8,12");
    }
    loom_spike::emit(record);
    return record.verified ? 0 : 1;
  } catch (const std::exception& error) {
    loom_spike::Record record;
    record.family = "abi_error"; record.variant = convention(); record.unit = "case";
    record.parameters["error"] = error.what(); record.verified = false;
    loom_spike::emit(record);
    return 2;
  }
}
