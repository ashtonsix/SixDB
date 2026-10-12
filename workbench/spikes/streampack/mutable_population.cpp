#include "grouped.h"
#include "byte_search.h"
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace sn = streampack_next;
namespace oa = orbital::allocation;
void require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital allocation"); return std::move(*r); }
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
unsigned mix(unsigned x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
unsigned permute(unsigned x, unsigned mask) { x = x * 0x85ebca6bu & mask; x ^= x >> 13; x = x * 0xc2b2ae35u & mask; return x ^ (x >> 7); }
unsigned reservation(unsigned row) { return mix(row) & 63; }
unsigned used_length(unsigned row, bool sparse, bool edited) {
  auto n = reservation(row);
  if (sparse) n = n ? (mix(row + 104729) % (n + 1)) : 0;
  if (edited && row % 4 == 1) n /= 2;
  return n;
}
bool dead(unsigned row, bool sparse) { return sparse && row % 4 == 0; }
unsigned seed(unsigned row, bool edited) { return (mix(row) + (edited && row % 4 == 1 ? 117 : 0)) & 255; }
enum class Method { direct16, direct32, widths8, widths8_native_reverse };

template<bool Random, bool Prepared = false, unsigned Sample = 1, class View>
unsigned project_block(const View& v, std::span<std::byte> bytes, std::span<std::uint32_t> ends) {
  unsigned out = 0, cursor = 0;
  const unsigned count = v.size(), mask = count - 1;
  std::array<std::uint32_t, 512> starts;
  if constexpr (Prepared) {
    unsigned offset = 0;
    for (unsigned r = 0; r < count; ++r) {
      starts[r] = offset; offset += v.reserved(r);
    }
  }
  for (unsigned i = 0; i < count / Sample; ++i) {
    const auto r = Random ? permute(i, mask) : i;
    if (v.live(r)) {
      const auto n = v.length(r);
      const auto start = Prepared ? starts[r] : Random ? v.start(r) : cursor;
      if (n) std::memcpy(bytes.data() + out, v.body.data() + start, n);
      out += n;
    }
    ends[i] = out;
    if constexpr (!Random) {
      if constexpr (requires { v.ends; }) cursor = v.ends[r + 1];
      else cursor += v.reserved(r);
    }
  }
  return out;
}

// This is an experiment-owned collection of borrowed blocks, not an allocation
// abstraction. Each plane is one actual Orbital Allocation. Body address derives
// from a common fixed capacity (64 bytes/rank), equally charged to every method.
// Allocator ownership and MVCC publication are tested separately by lifecycle.
template<Method M> struct Population {
  static constexpr bool widths = M == Method::widths8 || M == Method::widths8_native_reverse;
  static constexpr bool native_reverse = M == Method::widths8_native_reverse;
  // Keep the named scalar control after selecting native reverse for the
  // ordinary grouped view. Both benchmark labels retain their original work.
  template<class View> static auto owner(const View& v, unsigned x) {
    if constexpr (widths && !native_reverse) return v.reverse_scalar(x);
    else return v.reverse(x);
  }
  unsigned rows, cap, blocks;
  bool sparse;
  oa::Allocation body, geometry, checkpoints, used, tombs, counts;
  std::size_t body_bytes, geometry_bytes, checkpoint_bytes, shared_bytes;
  std::array<std::array<std::byte, 64>, 256> inputs;
  Population(oa::Pool& pool, unsigned n, unsigned c, bool holes) : rows(n), cap(c), blocks(n / c), sparse(holes) {
    body_bytes = std::size_t(rows) * 64;
    geometry_bytes = widths ? rows : std::size_t(rows + blocks) * (M == Method::direct16 ? 2 : 4);
    checkpoint_bytes = widths ? std::size_t(rows / 16 + blocks) * 4 : 0;
    shared_bytes = std::size_t(rows) * 2 + rows / 8 + blocks * 2;
    body = take(oa::allocate(pool, {.bytes = body_bytes, .alignment = 64}));
    geometry = take(oa::allocate(pool, {.bytes = geometry_bytes, .alignment = 64}));
    if (checkpoint_bytes) checkpoints = take(oa::allocate(pool, {.bytes = checkpoint_bytes, .alignment = 64}));
    used = take(oa::allocate(pool, {.bytes = std::size_t(rows) * 2, .alignment = 64}));
    tombs = take(oa::allocate(pool, {.bytes = rows / 8, .alignment = 64}));
    counts = take(oa::allocate(pool, {.bytes = blocks * 2, .alignment = 64}));
    // Initialize all allocated bytes for repeatable snapshots and ASan-friendly
    // cold admission. This is setup, not packed format extent or append traffic.
    for (auto* a : {&body, &geometry, &checkpoints, &used, &tombs, &counts})
      if (a->address) std::memset(a->address, 0, a->size);
    for (unsigned s = 0; s < 256; ++s) for (unsigned j = 0; j < 64; ++j) inputs[s][j] = std::byte((s + j * 31) & 255);
  }
  auto writer(unsigned b) {
    auto payload = std::span(body.address + std::size_t(b) * cap * 64, cap * 64);
    auto lengths = std::span(reinterpret_cast<std::uint16_t*>(used.address) + b * cap, cap);
    auto dead_bits = std::span(reinterpret_cast<std::uint64_t*>(tombs.address) + b * (cap / 64), cap / 64);
    auto& count = reinterpret_cast<std::uint16_t*>(counts.address)[b];
    if constexpr (widths)
      return sn::MutableWidths<std::uint8_t>{
        {reinterpret_cast<std::uint8_t*>(geometry.address) + b * cap, cap},
        {reinterpret_cast<std::uint32_t*>(checkpoints.address) + b * (cap / 16 + 1), cap / 16 + 1},
        payload, lengths, dead_bits, count, 63};
    else {
      using End = std::conditional_t<M == Method::direct16, std::uint16_t, std::uint32_t>;
      return sn::MutableDirect<End>{
        {reinterpret_cast<End*>(geometry.address) + b * (cap + 1), cap + 1}, payload, lengths, dead_bits, count, 63};
    }
  }
  sn::Bytes input(unsigned row, bool edited) const { return std::span(inputs[seed(row, edited)]).first(used_length(row, sparse, edited)); }
  void verify(bool edited, bool packed) {
    std::array<std::byte, 32768> actual{}, expected{};
    std::array<std::uint32_t, 512> ends{}, expected_ends{};
    std::array<unsigned, 513> oracle_ends{};
    for (unsigned b = 0; b < blocks; ++b) {
      auto w = writer(b); const auto v = w.read();
      require(v.size() == cap && v.admit(63) == sn::Error::none, "admitted current block");
      unsigned offset = 0, out = 0;
      for (unsigned r = 0; r < cap; ++r) {
        const unsigned row = b * cap + r;
        const bool gone = dead(row, sparse);
        const auto length = packed && gone ? 0 : used_length(row, sparse, edited);
        const auto width = packed ? (gone ? 0 : length) : reservation(row);
        require(v.start(r) == offset && v.reserved(r) == width && v.length(r) == length && v.live(r) == !gone, "independent geometry and state");
        const auto bytes = v.value(r);
        require(bytes.data() == v.body.data() + offset, "exact physical address");
        const auto expected_value = input(row, edited);
        for (unsigned j = 0; j < length; ++j) require(bytes[j] == expected_value[j], "exact current bytes");
        if (!gone) {
          if (length) { std::memcpy(actual.data() + out, bytes.data(), length); std::memcpy(expected.data() + out, expected_value.data(), length); }
          out += length;
        }
        if (width) {
          // First/last/interior include slack and dead ownership. Small fixtures
          // additionally test every byte; no expanded owner oracle stays resident.
          const auto check = [&](unsigned x) {
            const auto owner = Population::owner(v, x);
            require(owner && owner->rank == r && owner->tombstone == gone, "independent reverse byte owner");
          };
          check(offset); check(offset + width - 1); check(offset + width / 2);
          if (rows <= 16384) for (unsigned j = 0; j < width; ++j) check(offset + j);
        }
        offset += width;
        oracle_ends[r + 1] = offset;
      }
      require(v.extent() == offset && !v.reverse(offset) && !v.reverse(cap * 64), "extent and capacity have no owner");
      for (unsigned j = 0; j < cap; ++j) {
        const auto x = offset ? mix(b * cap + j) % offset : 0;
        const auto owner = Population::owner(v, x);
        if (!offset) require(!owner, "empty body probe");
        else {
          const auto rank = unsigned(std::upper_bound(oracle_ends.begin() + 1, oracle_ends.begin() + cap + 1, x) - oracle_ends.begin() - 1);
          require(owner && owner->rank == rank && owner->tombstone == dead(b * cap + rank, sparse), "exact timed reverse probe oracle");
        }
      }
      for (const bool random : {false, true}) {
        unsigned expected_size = 0;
        for (unsigned i = 0; i < cap; ++i) {
          const auto r = random ? permute(i, cap - 1) : i, row = b * cap + r;
          if (!dead(row, sparse)) {
            const auto value = input(row, edited);
            if (!value.empty()) std::memcpy(expected.data() + expected_size, value.data(), value.size());
            expected_size += value.size();
          }
          expected_ends[i] = expected_size;
        }
        const auto actual_size = random ? project_block<true>(v, actual, ends) : project_block<false>(v, actual, ends);
        require(actual_size == expected_size && std::equal(ends.begin(), ends.begin() + cap, expected_ends.begin()) &&
          std::equal(actual.begin(), actual.begin() + actual_size, expected.begin()), "exact ready projection bytes and row boundaries");
        if (random) {
          const auto prepared_size = project_block<true, true>(v, actual, ends);
          require(prepared_size == expected_size && std::equal(ends.begin(), ends.begin() + cap, expected_ends.begin()) &&
            std::equal(actual.begin(), actual.begin() + prepared_size, expected.begin()), "exact paid prepared projection");
          const auto selected_size = project_block<true, false, 16>(v, actual, ends);
          require(selected_size == expected_ends[cap / 16 - 1] &&
            std::equal(ends.begin(), ends.begin() + cap / 16, expected_ends.begin()) &&
            std::equal(actual.begin(), actual.begin() + selected_size, expected.begin()), "exact sparse selected projection");
          const auto selected_prepared = project_block<true, true, 16>(v, actual, ends);
          require(selected_prepared == selected_size &&
            std::equal(ends.begin(), ends.begin() + cap / 16, expected_ends.begin()) &&
            std::equal(actual.begin(), actual.begin() + selected_prepared, expected.begin()), "exact paid sparse selection");
        }
      }
    }
  }
};

struct WriteResult { std::uint64_t commands{}, records{}; bool operator==(const WriteResult&) const = default; };
// Journal storage is reused per command and escaped while the actual source
// binding lives. This prices issued/materialized damage records, not resolution,
// frozen afterimages or MVCC. The geometry check independently replays records.
template<class F> __attribute__((noinline)) WriteResult write_trace(F& f, unsigned operation) {
  std::array<ikea::owner_write, 2048> storage;
  WriteResult result;
  for (unsigned b = 0; b < f.blocks; ++b) {
    auto w = f.writer(b);
    const auto command = [&](auto run) {
      ikea::source_write_journal effects{storage};
      require(run(effects) == sn::Error::none, "issued mutation");
      escape(effects.entries().data()); result.records += effects.entries().size(); ++result.commands;
    };
    if (operation == 0) {
      command([&](auto& e) { return w.initialize(e); });
      for (unsigned r = 0; r < f.cap; ++r) {
        const auto row = b * f.cap + r;
        command([&](auto& e) { return w.append(f.input(row, false), reservation(row), e); });
        if (dead(row, f.sparse)) command([&](auto& e) { return w.retire(r, e); });
      }
    } else if (operation == 1) {
      for (unsigned r = 1; r < f.cap; r += 4) {
        const auto row = b * f.cap + r;
        command([&](auto& e) { return w.replace(r, f.input(row, true), e); });
      }
    } else command([&](auto& e) { return sn::soft_pack(w, true, e); });
  }
  return result;
}

// Full-value output bytes and each value's output boundary are materialized.
// A bounded reusable output buffer models a streaming consumer, not an extra
// whole-population result copy. Running offsets are the native sequential width
// scan. Random order compares ready points against paid per-block preparation.
template<bool Random, bool Prepared = false, unsigned Sample = 1, class F> __attribute__((noinline)) std::uint64_t project(F& f) {
  std::array<std::byte, 32768> bytes;
  std::array<std::uint32_t, 512> ends;
  std::uint64_t total = 0;
  for (unsigned b = 0; b < f.blocks; ++b) {
    const auto v = f.writer(Random ? permute(b, f.blocks - 1) : b).read();
    const auto out = project_block<Random, Prepared, Sample>(v, bytes, ends);
    escape(bytes.data()); escape(ends.data()); total += out;
  }
  return total;
}

// One uniformly distributed probe per rank across each block's occupied body.
// Blocks are visited by a whole-domain permutation. This touches geometry/dead
// state only; allocated payload is deliberately not labeled a touched footprint.
template<class F> __attribute__((noinline)) std::uint64_t reverse_trace(F& f) {
  std::uint64_t sum = 0;
  std::array<unsigned, 512> owners;
  for (unsigned i = 0; i < f.blocks; ++i) {
    const auto b = permute(i, f.blocks - 1);
    const auto v = f.writer(b).read();
    const auto extent = v.extent();
    for (unsigned j = 0; j < f.cap; ++j) {
      const auto offset = extent ? mix(b * f.cap + j) % extent : 0;
      const auto owner = F::owner(v, offset);
      owners[j] = owner ? owner->rank | (unsigned(owner->tombstone) << 16) : ~0u;
      sum += owners[j];
    }
    escape(owners.data());
  }
  return sum;
}

// Exactly the same (block, query) population as reverse_trace, now permuted
// across the whole row domain rather than amortizing geometry over N probes.
// Odd multiplication and right-xorshift are bijections on the power-of-two
// domain. verify() independently checks every query's owner before timing;
// no retained request/answer array competes with the measured geometry.
template<class F> __attribute__((noinline)) std::uint64_t reverse_scattered(F& f) {
  std::uint64_t sum = 0;
  std::array<unsigned, 512> owners;
  for (unsigned i = 0; i < f.rows; i += 512) {
    for (unsigned j = 0; j < 512; ++j) {
      const auto query = permute(i + j, f.rows - 1);
      const auto v = f.writer(query / f.cap).read();
      const auto extent = v.extent();
      const auto offset = extent ? mix(query) % extent : 0;
      const auto owner = F::owner(v, offset);
      owners[j] = owner ? owner->rank | (unsigned(owner->tombstone) << 16) : ~0u;
      sum += owners[j];
    }
    escape(owners.data());
  }
  return sum;
}

template<unsigned Method, class F> __attribute__((noinline)) std::uint64_t scan(F& f, std::byte needle) {
  std::uint64_t count = 0;
  for (unsigned b = 0; b < f.blocks; ++b) {
    const auto v = f.writer(b).read();
    const auto occurrence = [&](auto, auto) { ++count; return true; };
    if constexpr (Method == 0) sn::byte_per_value(v, needle, occurrence);
    if constexpr (Method == 1) sn::byte_sparse(v, needle, occurrence);
    if constexpr (Method == 2) sn::byte_segmented(v, needle, [&](auto, auto, auto, auto mask) { count += mask.population(); return true; });
  }
  return count;
}

using Clock = std::chrono::steady_clock;
template<class F> double time_ns(F run) { const auto before = Clock::now(); run(); return std::chrono::duration<double, std::nano>(Clock::now() - before).count(); }
template<Method M> void run(oa::Pool& pool, const char* name, unsigned rows, unsigned cap, bool sparse, unsigned reps, const std::string& receipt, const std::string& subset) {
  const bool scans_only = subset == "scans", reverse_only = subset == "reverse", mutation_only = subset == "mutation";
  Population<M> f(pool, rows, cap, sparse);
  const auto row = [&](const char* op, unsigned rep, unsigned iterations, double elapsed, std::uint64_t commands, std::uint64_t records) {
    std::printf("%s,%s,%u,%u,%s,%u,%u,%.4f,%llu,%llu,%zu,%zu,%zu\n", name, sparse ? "slack-dead-empty" : "full-live-empty", rows, cap, op, rep, iterations,
      elapsed / (double(rows) * iterations), (unsigned long long)commands, (unsigned long long)records, f.body_bytes, f.geometry_bytes + f.checkpoint_bytes, f.shared_bytes);
  };
  // Validation is outside timers. Each destructive operation starts from a real
  // prescribed history; no repetition times the already-compacted fixed point.
  for (unsigned rep = 0; rep < reps; ++rep) {
    WriteResult result;
    const auto append_ns = time_ns([&] { result = write_trace(f, 0); });
    if (subset.empty() || mutation_only) row("initialize-append-retire", rep, 1, append_ns, result.commands, result.records);
    f.verify(false, false);
    const auto replace_ns = time_ns([&] { result = write_trace(f, 1); });
    if (subset.empty() || mutation_only) row("same-reservation-edits", rep, 1, replace_ns, result.commands, result.records);
    f.verify(true, false);
    const auto reads = [&](const char* state) {
      const auto measure = [&](const char* op, auto operation) {
        const auto expected = operation(); escape(&expected);
        unsigned iterations = 1;
        for (;;) {
          auto result = expected;
          const auto elapsed = time_ns([&] { for (unsigned i = 0; i < iterations; ++i) { result = operation(); escape(&result); } });
          require(result == expected, "repeatable read");
          if (elapsed >= 20000000 || iterations >= 1024) {
            const auto label = std::string(state) + op; row(label.c_str(), rep, iterations, elapsed, 0, 0); break;
          }
          iterations *= 2;
        }
      };
      if (!scans_only) {
        if (!reverse_only) {
          measure("-project-physical", [&] { return project<false>(f); });
          measure("-project-shuffled-blocks-ranks", [&] { return project<true>(f); });
          if (mutation_only) {
            measure("-project-shuffled-prepared", [&] { return project<true, true>(f); });
            // ns_per_input_row retains population normalization. Multiply by 16
            // to obtain ns per selected row, including all preparation work.
            measure("-project-selected-1of16", [&] { return project<true, false, 16>(f); });
            measure("-project-selected-1of16-prepared", [&] { return project<true, true, 16>(f); });
          }
        }
        if (!mutation_only) {
        measure("-reverse-geometry", [&] { return reverse_trace(f); });
        require(reverse_scattered(f) == reverse_trace(f), "same exact queries in scattered order");
        measure("-reverse-scattered-geometry", [&] { return reverse_scattered(f); });
        }
      }
      if (!reverse_only && !mutation_only) for (const auto q : {std::byte{0}, std::byte{255}}) {
        std::uint64_t expected = 0;
        for (unsigned r = 0; r < rows; ++r) if (!dead(r, sparse)) for (const auto x : f.input(r, true)) expected += x == q;
        const auto trial = [&]<unsigned Scanner>(const char* label) {
          require(scan<Scanner>(f, q) == expected, "independent logical occurrence count");
          const auto op = std::string(q == std::byte{0} ? "-zero-" : "-255-") + label;
          measure(op.c_str(), [&] { return scan<Scanner>(f, q); });
        };
        trial.template operator()<0>("libc-per-value"); trial.template operator()<1>("native-sparse"); trial.template operator()<2>("native-segmented");
      }
    };
    reads("before-soft");
    const auto soft_ns = time_ns([&] { result = write_trace(f, 2); });
    if (subset.empty() || mutation_only) row("soft-shrink", rep, 1, soft_ns, result.commands, result.records);
    f.verify(true, true);
    reads("after-soft");
  }
  if (!receipt.empty()) {
    for (const auto* file : {"smaps_rollup", "smaps", "numa_maps"}) {
      std::ifstream in(std::string("/proc/self/") + file); std::ofstream out(receipt + "." + file);
      if (in) out << in.rdbuf(); else out << "UNAVAILABLE\n";
    }
    const auto stats = oa::statistics(pool); std::ofstream out(receipt + ".allocation.txt");
    out << "mapped_bytes " << stats.mapped_bytes << "\nreserved_bytes " << stats.reserved_bytes << "\nmetadata_bytes " << stats.metadata_bytes << "\nidle_bytes " << stats.idle_bytes << '\n';
    for (auto* a : {&f.body, &f.geometry, &f.checkpoints, &f.used, &f.tombs, &f.counts})
      if (a->address) out << reinterpret_cast<std::uintptr_t>(a->address) << ' ' << a->size << ' ' << oa::capacity(*a) << '\n';
  }
}

int main(int argc, char** argv) {
  require(argc >= 6, "method rows capacity sparse repeats [receipt-prefix] [scans|reverse|mutation]");
  const std::string method = argv[1];
  const unsigned rows = std::strtoul(argv[2], nullptr, 10), cap = std::strtoul(argv[3], nullptr, 10), sparse = std::strtoul(argv[4], nullptr, 10), reps = std::strtoul(argv[5], nullptr, 10);
  require(std::has_single_bit(rows) && rows >= 512 && rows <= (1u << 24) && (cap == 128 || cap == 256 || cap == 512) && sparse <= 1 && reps, "experiment bounds");
  const std::string receipt = argc > 6 ? argv[6] : "";
  const std::string subset = argc > 7 ? argv[7] : "";
  require(argc <= 8 && (subset.empty() || subset == "scans" || subset == "reverse" || subset == "mutation"), "known operation subset");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(1536ull << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  std::puts("method,population,rows,capacity,operation,repetition,iterations,ns_per_input_row,commands,damage_records,body_capacity_bytes,geometry_bytes,used_dead_count_bytes");
  if (method == "direct16") run<Method::direct16>(*pool, method.c_str(), rows, cap, sparse, reps, receipt, subset);
  else if (method == "direct32") run<Method::direct32>(*pool, method.c_str(), rows, cap, sparse, reps, receipt, subset);
  else if (method == "widths8") run<Method::widths8>(*pool, method.c_str(), rows, cap, sparse, reps, receipt, subset);
  else if (method == "widths8-native-reverse") run<Method::widths8_native_reverse>(*pool, method.c_str(), rows, cap, sparse, reps, receipt, subset);
  else require(false, "known method");
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "mutable sweep refund"); require(bool(oa::remove_pool(*pool)), "remove Pool");
  arena.reset(); orbital::worker::relinquish(lane);
}
