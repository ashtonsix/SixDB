#include "population_fixture.h"
#include <loom/ring.h>
#include <string_view>
using namespace streampack_population;
using Clock = std::chrono::steady_clock;

#if defined(STREAMPACK_INLINE_HINTS) && STREAMPACK_INLINE_HINTS
#define STREAMPACK_HINT_CALL [[clang::always_inline]]
#else
#define STREAMPACK_HINT_CALL
#endif

// These are fixture-specific physical dependencies. Each emitted address lies
// in an initialized, stable plane. No envelope spans the gap between planes.
template<class Reader, class Emit> inline void metadata(const Reader& r, unsigned row, Emit emit) {
  if constexpr (requires { r.ends; r.shift; }) {
    const auto block = row >> r.shift;
    STREAMPACK_HINT_CALL emit(r.ends + row + block);
    STREAMPACK_HINT_CALL emit(r.bases + block);
  } else if constexpr (requires { r.widths; }) {
    STREAMPACK_HINT_CALL emit(r.widths + (row & ~15u));
    STREAMPACK_HINT_CALL emit(r.bases + row / 16);
  } else if constexpr (requires { r.locations; }) { STREAMPACK_HINT_CALL emit(r.locations + row); }
  else if constexpr (requires { r.strings; }) { STREAMPACK_HINT_CALL emit(r.strings + row); }
}

template<unsigned C, bool Hints, class Reader>
__attribute__((noinline)) Result<C> manual(const Reader& reader, unsigned count,
    const loom::HintPolicy& policy, loom::DemandClass demand) {
  Result<C> result; std::array<const std::byte*, C> tails;
  const auto hint = [&](const void* p) { if constexpr (Hints) loom::prefetch(policy, demand, p); };
  for (unsigned j = 0; j < C; ++j) {
    result.rows[j] = permute(j * (count / C), count - 1);
    STREAMPACK_HINT_CALL metadata(reader, result.rows[j], hint);
  }
  for (unsigned i = 0; i < count / C; ++i) {
    for (unsigned j = 0; j < C; ++j) {
      const auto value = reader.value(result.rows[j]); result.lengths += value.size();
      tails[j] = value.data() + value.size() - 4;
      STREAMPACK_HINT_CALL hint(tails[j]);
    }
    for (unsigned j = 0; j < C; ++j) {
      std::memcpy(&result.rows[j], tails[j], 4);
      if (i + 1 < count / C) { STREAMPACK_HINT_CALL metadata(reader, result.rows[j], hint); }
    }
  }
  return result;
}

template<class Reader> struct Job {
  const Reader* reader{};
  loom::DemandClass demand;
  std::uint64_t sum{};
  unsigned row{};
};
template<class Reader> using Ring = loom::Ring<Job<Reader>*, unsigned, unsigned, std::uint64_t, const std::byte*>;

template<bool Hints, class Reader>
LOOM_NATIVE_CC void payload(typename Ring<Reader>::Cursor, typename Ring<Reader>::Slot*, Job<Reader>*, unsigned,
    unsigned, std::uint64_t, const std::byte*);

template<bool Hints, class Reader>
LOOM_NATIVE_CC void geometry(typename Ring<Reader>::Cursor ring, typename Ring<Reader>::Slot* slot,
    Job<Reader>* job, unsigned row, unsigned remaining, std::uint64_t sum, const std::byte*) {
  const auto value = job->reader->value(row);
  const auto* tail = value.data() + value.size() - 4;
  if constexpr (Hints) ring->prefetch(job->demand, tail);
  slot->function = payload<Hints, Reader>;
  [[clang::musttail]] return Ring<Reader>::yield(ring, slot, job, row, remaining, sum + value.size(), tail);
}

template<bool Hints, class Reader>
LOOM_NATIVE_CC void payload(typename Ring<Reader>::Cursor ring, typename Ring<Reader>::Slot* slot,
    Job<Reader>* job, unsigned row, unsigned remaining, std::uint64_t sum, const std::byte* tail) {
  std::memcpy(&row, tail, 4);
  if (!--remaining) {
    job->sum = sum; job->row = row; ring->complete(*slot);
  } else {
    if constexpr (Hints) { STREAMPACK_HINT_CALL metadata(*job->reader, row, [&](const void* p) { ring->prefetch(job->demand, p); }); }
    slot->function = geometry<Hints, Reader>;
  }
  [[clang::musttail]] return Ring<Reader>::yield(ring, slot, job, row, remaining, sum, tail);
}

template<unsigned C, bool Hints, class Reader>
__attribute__((noinline)) Result<C> scheduled(Ring<Reader>& ring, const Reader& reader,
    unsigned count, loom::DemandClass demand) {
  // Stack frames belong to the complete operation, never an individual callback.
  // They remain alive across every spin and until pending() reaches zero.
  std::array<Job<Reader>, C> jobs;
  for (unsigned j = 0; j < C; ++j) {
    jobs[j] = {&reader, demand}; const auto row = permute(j * (count / C), count - 1);
    if constexpr (Hints) { STREAMPACK_HINT_CALL metadata(reader, row, [&](const void* p) { ring.prefetch(demand, p); }); }
    require(ring.push(geometry<Hints, Reader>, {jobs.data() + j, row, count / C, 0, nullptr}), "bounded Ring admission");
  }
  while (ring.pending()) ring.spin({4096});
  Result<C> result;
  for (unsigned j = 0; j < C; ++j) { result.lengths += jobs[j].sum; result.rows[j] = jobs[j].row; }
  return result;
}

template<Method M, unsigned C> void run(loom::Worker& worker, const char* name, unsigned count,
    unsigned cap, unsigned reps, const std::string_view variant) {
  Fixture<M> fixture(*worker.memory, count, cap); const auto reader = fixture.reader();
  // Every dependency has at least four occupied bytes. Binding class zero once
  // authorizes a hint at its first byte; it grants no cache readiness/read rights.
  const auto demand = take(loom::bind_demand({loom::MemoryPattern::irregular, 4}, 4));
  Ring<std::remove_cv_t<decltype(reader)>> ring;
  const auto setup_start = Clock::now();
  if (variant.starts_with("ring")) require(bool(ring.configure(worker, {C, 0})), "configure Ring");
  const auto setup_ns = std::chrono::duration<double, std::nano>(Clock::now() - setup_start).count();
  Result<C> expected; expected.lengths = fixture.logical_bytes;
  for (unsigned j = 0; j < C; ++j) expected.rows[j] = permute(((j + 1) * (count / C)) & (count - 1), count - 1);
  const auto operation = [&] {
    if (variant == "direct") return chase<C>(reader, count);
    if (variant == "manual") return manual<C, false>(reader, count, worker.hints, demand);
    if (variant == "manual-hints") return manual<C, true>(reader, count, worker.hints, demand);
    if (variant == "ring") return scheduled<C, false>(ring, reader, count, demand);
    return scheduled<C, true>(ring, reader, count, demand);
  };
  require(operation() == expected, "independent complete cycle result");
  unsigned iterations = 1;
  for (;;) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) { const auto value = operation(); escape(&value); }
    if (Clock::now() - start >= std::chrono::milliseconds(20) || iterations >= 65536) break;
    iterations *= 2;
  }
  for (unsigned rep = 0; rep < reps; ++rep) {
    Result<C> value; const auto visits_before = ring.counters().visits;
    const auto start = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) { value = operation(); escape(&value); }
    const auto ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    require(value == expected, "timed result and terminal ranks");
    const auto visits = ring.counters().visits - visits_before;
    if (variant.starts_with("ring")) require(visits == std::uint64_t(count) * iterations * 2, "exact scheduled stage count");
    std::printf("%s,%.*s,%u,%u,%u,%u,%u,%.4f,%.0f,%zu,%llu,%zu,%zu,%zu,%u\n", name,
      int(variant.size()), variant.data(), count, cap, C, rep, iterations, ns / (double(count) * iterations),
      setup_ns, ring.configured() ? ring.storage_bytes() : 0, (unsigned long long)visits,
      fixture.payload_bytes, fixture.metadata_bytes, fixture.base_bytes, unsigned(worker.hints.codes[demand.code()]));
  }
  if (ring.configured()) require(bool(ring.close()), "drained Ring close");
}

template<Method M> void dispatch(loom::Worker& worker, const char* name, unsigned count,
    unsigned cap, unsigned chains, unsigned reps, std::string_view variant) {
  if (chains == 1) run<M, 1>(worker, name, count, cap, reps, variant);
  else if (chains == 8) run<M, 8>(worker, name, count, cap, reps, variant);
  else run<M, 32>(worker, name, count, cap, reps, variant);
}
int main(int argc, char** argv) {
  require(argc == 7, "method rows capacity chains repetitions direct|manual|manual-hints|ring|ring-hints");
  const std::string method = argv[1]; const unsigned count = std::strtoul(argv[2], nullptr, 10),
    cap = std::strtoul(argv[3], nullptr, 10), chains = std::strtoul(argv[4], nullptr, 10), reps = std::strtoul(argv[5], nullptr, 10);
  const std::string_view variant = argv[6];
  require(std::has_single_bit(count) && count >= 512 && count <= (1u << 26) &&
    (cap == 128 || cap == 256 || cap == 512) && (chains == 1 || chains == 8 || chains == 32) && reps, "bounded fixture");
  require(variant == "direct" || variant == "manual" || variant == "manual-hints" || variant == "ring" || variant == "ring-hints", "execution variant");
  loom::Worker worker; require(bool(loom::adopt(worker)), "adopt Worker");
  auto arena = take(oa::open_arena((count > (1u << 24) ? 3072ull : 1536ull) << 20));
  require(bool(loom::configure(worker, std::move(arena), 0)), "configure Worker");
  std::puts("method,execution,rows,capacity,chains,repetition,iterations,ns_per_value,ring_setup_ns,ring_storage_bytes,ring_visits,payload_bytes,metadata_bytes,base_bytes,hint_code");
  if (method == "direct16") dispatch<Method::direct16>(worker, method.c_str(), count, cap, chains, reps, variant);
  else if (method == "widths8-native") dispatch<Method::widths8_native>(worker, method.c_str(), count, cap, chains, reps, variant);
  else if (method == "location8") dispatch<Method::location8>(worker, method.c_str(), count, cap, chains, reps, variant);
  else if (method == "duckdb16") dispatch<Method::duckdb16>(worker, method.c_str(), count, cap, chains, reps, variant);
  else require(false, "known method");
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto result = loom::shutdown(worker);
    if (result.kind() == orbital::Progress::Kind::done) break;
    require(result.kind() != orbital::Progress::Kind::failed && Clock::now() < deadline, "bounded Worker shutdown");
    loom::poll(worker, {128, 0}); std::this_thread::yield();
  }
  loom::relinquish(worker);
}
