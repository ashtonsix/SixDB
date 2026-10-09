#include "consumer.h"
#include "fixture.h"
#include "detail.h" // Experiment-only fused caller; ordinary consumer does not include it.
#include <array>
#include <cstring>
#include <cstdio>
#include <sched.h>

namespace {
std::uint64_t use(const std::byte* bytes) {
  asm volatile("" : "+r"(bytes) :: "memory");
  std::uint64_t value; std::memcpy(&value, bytes, sizeof(value)); return value;
}

// Fixed legal word body only: diagnose a fused boundary, not a second public API.
inline std::uint64_t fused(selected::Selection selection) {
  const auto& rep = selection.representation();
  const auto& p = rep.data->parts.front();
  assert(rep.data->format == 1 && p.id == 0 && p.native && p.body().size() >= 8);
  return use(p.body().data());
}

template<class F> void measure(const char* name, unsigned trial, F body, std::uint64_t expected) {
  constexpr unsigned iterations = 250000;
  std::uint64_t sum{};
  const auto begin = std::chrono::steady_clock::now();
  for (unsigned n = 0; n < iterations; ++n) sum += body(n);
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  assert(sum == expected * iterations);
  std::printf("{\"case\":\"%s\",\"trial\":%u,\"iterations\":%u,\"ns_per_call\":%.4f,\"checksum\":%llu}\n",
      name, trial, iterations, std::chrono::duration<double, std::nano>(elapsed).count() / iterations,
      static_cast<unsigned long long>(sum));
}
}

int main() {
  cpu_set_t available; CPU_ZERO(&available); assert(sched_getaffinity(0, sizeof(available), &available) == 0);
  int cpu = 0; while (!CPU_ISSET(cpu, &available)) ++cpu;
  cpu_set_t pin; CPU_ZERO(&pin); CPU_SET(cpu, &pin); assert(sched_setaffinity(0, sizeof(pin), &pin) == 0);
  using namespace selected;
  fixture::Memory memory;
  Store store;
  constexpr std::uint64_t word = 0x0102030405060708ULL;
  auto bytes = std::as_bytes(std::span(&word, 1));
  auto rep = fixture::representation(memory, {1, 1}, 1, 1, bytes);
  fixture::publish(store, rep, 1);
  auto context = store.read(); auto selection = bind(context, 1); assert(selection);
  auto view = ready(*selection, 0, {1, 0, 8}); assert(view);
  auto alternative = fixture::representation(memory, {1, 1}, 2, 1, bytes);
  assert(store.install_representation(context, alternative));
  std::printf("{\"context_bytes\":%zu,\"selection_bytes\":%zu,\"view_bytes\":%zu,\"retained_bytes\":%zu,\"requirements_bytes\":%zu,\"view_result_bytes\":%zu,\"cpu\":%d,\"compiler\":\"%s\"}\n",
      sizeof(ReadContext), sizeof(Selection), sizeof(View), sizeof(Retained), sizeof(Requirements),
      sizeof(Result<View>), cpu, __clang_version__);
  for (unsigned trial = 0; trial < 7; ++trial) {
    measure("native", trial, [&](unsigned) { return use(view->bytes().data()); }, word);
    measure("fused-fixed-word", trial, [&](unsigned) { return fused(*selection); }, word);
    measure("reused-binding", trial, [&](unsigned) { auto v = consumer::word(*selection, 0, 1); assert(v); return *v; }, word);
    measure("reused-context", trial, [&](unsigned) { auto v = consumer::word(context, 1, 0, 1); assert(v); return *v; }, word);
    measure("fresh-borrow", trial, [&](unsigned) { auto c = store.borrow(); auto v = consumer::word(c, 1, 0, 1); assert(v); return *v; }, word);
    measure("fresh-owned", trial, [&](unsigned) { auto c = store.read(); auto v = consumer::word(c, 1, 0, 1); assert(v); return *v; }, word);
    measure("batch64-owned", trial, [&](unsigned n) {
      if (n % 64 == 0) context = store.read();
      auto v = consumer::word(context, 1, 0, 1); assert(v); return *v;
    }, word);
    measure("capture-alternative", trial, [&](unsigned) {
      auto s = bind(context, 1, alternative); assert(s); auto r = retain(context, *s); assert(r);
      auto v = consumer::word(*s, 0, 1); assert(v); return *v;
    }, word);
  }
}
