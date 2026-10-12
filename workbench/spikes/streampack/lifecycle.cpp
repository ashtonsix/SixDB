#include "rewrite.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

namespace sm = streampack_material;
namespace sn = streampack_next;
namespace oa = orbital::allocation;
using orbital::Error;
using orbital::Result;
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "setup result"); return std::move(*r); }
double elapsed(Clock::time_point start) { return std::chrono::duration<double, std::nano>(Clock::now() - start).count(); }
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
orbital::ObjectId id(unsigned object) { return {31, object + 1}; }

// Independent logical history: each epoch edits one of cap/batch disjoint row
// batches. Values depend only on object, logical row and last editing epoch.
unsigned version(unsigned row, unsigned cap, unsigned epoch, unsigned batch) {
  const unsigned first = row / batch + 1, period = cap / batch;
  return epoch < first ? 0 : first + ((epoch - first) / period) * period;
}
std::array<std::byte, 112> expected(unsigned object, unsigned row, unsigned epoch) {
  std::array<std::byte, 112> bytes;
  for (unsigned i = 0; i < bytes.size(); ++i) bytes[i] = std::byte((object * 31 + row * 17 + epoch * 7 + i) & 255);
  // Preserve complete identity even when the repeating fill bytes collide.
  std::memcpy(bytes.data(), &object, 4); std::memcpy(bytes.data() + 4, &row, 4); std::memcpy(bytes.data() + 8, &epoch, 4);
  return bytes;
}
unsigned length(unsigned row, unsigned epoch) { return 64 + ((row * 13 + epoch * 7) & 31); }
Result<sm::Selection> select(sm::Store::Scope& scope, unsigned object) {
  auto alternative = scope.bind_alternative(id(object));
  if (alternative) return alternative;
  if (alternative.error() != Error::absent) return std::unexpected(alternative.error());
  return scope.bind(id(object));
}
Result<sm::Store::Owner> capture(sm::Store& store, sm::Draft& draft) {
  const auto sources = draft.sources(); return store.capture(draft.control, sources);
}
Result<void> publish(sm::Store& store, sm::Store::Scope& basis, unsigned object, sm::Store::Owner owner) {
  const sm::Store::Change change{id(object), std::move(owner)}; sm::Store::Prepared prepared(store);
  auto result = store.prepare(basis, {&change, 1}, prepared);
  if (!result) return result;
  return store.install(prepared);
}
template<bool Widths> auto read(sm::Selection s, unsigned block = 0) {
  if constexpr (Widths) return sm::read_widths(s, block);
  else return sm::read(s, block);
}
template<bool Widths> auto write(sm::Draft& d, unsigned block = 0) {
  if constexpr (Widths) return d.write_widths(block);
  else return d.write(block);
}
template<bool Widths> void check(sm::Selection s, unsigned object, unsigned cap, unsigned epoch, unsigned batch) {
  require(sm::holders(s).size() == cap, "holder extent"); sm::Summary expected_summary;
  for (unsigned b = 0; b < s.control().blocks; ++b) require(read<Widths>(s, b).admit() == sn::Error::none, "published geometry");
  for (unsigned row = 0; row < cap; ++row) {
    const auto x = sm::holders(s)[row]; require(x != sm::absent && sm::block_of(x) < s.control().blocks, "published holder");
    const auto view = read<Widths>(s, sm::block_of(x)); require(sm::rank_of(x) < view.size() && view.live(sm::rank_of(x)), "live holder target");
    const auto value = view.value(sm::rank_of(x)); const auto generation = version(row, cap, epoch, batch);
    const auto want = expected(object, row, generation);
    require(value.size() == length(row, generation) && std::equal(value.begin(), value.end(), want.begin()), "exact independent logical history");
    ++expected_summary.live_rows; expected_summary.used_bytes += length(row, generation);
    for (unsigned j = 0; j < length(row, generation); ++j) {
      expected_summary.minimum = std::min(expected_summary.minimum, std::to_integer<std::uint8_t>(want[j]));
      expected_summary.maximum = std::max(expected_summary.maximum, std::to_integer<std::uint8_t>(want[j]));
    }
  }
  require(s.control().summary == expected_summary, "independent history summary");
}
struct Accounting {
  std::uint64_t peak_charge{}, peak_mapped{}, copies{}, issued{}, repairs{}, rewrites{}, edits{}, refusals{};
  double rewrite_ns{}, edit_ns{}, traversal_ns{}, maintenance_ns{}, admission_ns{}, wall_ns{}, oracle_ns{};
  bool sample_phases{std::getenv("STREAMPACK_ACCOUNTING") != nullptr};
  bool time_phases{std::getenv("STREAMPACK_PHASE_TIMING") != nullptr};
  std::array<double, 7> phase_ns{};
  template<class F> auto measure(unsigned stage, F&& operation) {
    if (!time_phases) return operation();
    const auto start = Clock::now();
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      operation(); phase_ns[stage] += elapsed(start);
    } else {
      auto result = operation(); phase_ns[stage] += elapsed(start); return result;
    }
  }
  void sample(oa::Pool& pool, bool boundary = false) {
    if (!sample_phases && !boundary) return;
    peak_charge = std::max(peak_charge, pool.arena->charged_bytes.load());
    peak_mapped = std::max(peak_mapped, oa::statistics(pool).mapped_bytes);
  }
};
template<bool Widths> unsigned project(sm::Selection s, std::span<std::byte> output) {
  const std::array views{read<Widths>(s, 0), read<Widths>(s, 1)}; unsigned offset = 0;
#if defined(STREAMPACK_PREPARED_PROJECTION) && STREAMPACK_PREPARED_PROJECTION
  // Both recipes pay the same bounded preparation policy. It lasts only for
  // this projection and includes every assigned reservation, including dead.
  std::array<std::array<std::uint32_t, 512>, 2> starts;
  for (unsigned b = 0; b < views.size(); ++b) {
    std::uint32_t start = 0;
    for (unsigned r = 0; r < views[b].size(); ++r) {
      starts[b][r] = start; start += views[b].reserved(r);
    }
  }
#endif
  for (const auto x : sm::holders(s)) {
#if defined(STREAMPACK_PREPARED_PROJECTION) && STREAMPACK_PREPARED_PROJECTION
    const auto b = sm::block_of(x), r = sm::rank_of(x);
    const auto value = views[b].body.subspan(starts[b][r], views[b].length(r));
#else
    const auto value = views[sm::block_of(x)].value(sm::rank_of(x));
#endif
    std::memcpy(output.data() + offset, value.data(), value.size()); offset += value.size();
  }
  return offset;
}
void check_projection(std::span<const std::byte> output, unsigned object, unsigned cap, unsigned epoch, unsigned batch) {
  unsigned offset = 0;
  for (unsigned row = 0; row < cap; ++row) {
    const auto v = version(row, cap, epoch, batch), n = length(row, v); const auto want = expected(object, row, v);
    require(offset + n <= output.size() && std::equal(want.begin(), want.begin() + n, output.begin() + offset), "exact concatenated projection");
    offset += n;
  }
  require(offset == output.size(), "projected output extent");
}
std::uint64_t extent(sm::Selection s) {
  std::uint64_t bytes = 0; for (unsigned i = 0; i < s.control().full_parts(); ++i) bytes += *s.control().extent(i); return bytes;
}
template<bool Widths> Result<void> initialize(sm::Store& store, oa::Pool& pool, unsigned object, unsigned cap, Accounting& stats) {
  auto& memory = *std::pmr::new_delete_resource(); sm::Control c{&memory, 0, std::uint16_t(cap), std::uint16_t(cap), {}}; c.blocks = 2; c.geometry = Widths ? sm::Geometry::widths8 : sm::Geometry::direct32;
  auto draft = sm::Draft::create(pool, c, std::array<std::size_t, 2>{cap * 160, cap * 160});
  if (!draft) return std::unexpected(draft.error());
  stats.sample(pool); auto writer = write<Widths>(*draft); auto holders = draft->holder_view(); ikea::no_coverage ignored;
  // Invert the logical-row permutation without an auxiliary resident model.
  unsigned inverse = 1; while ((inverse * 17) % cap != 1) ++inverse;
  for (unsigned rank = 0; rank < cap; ++rank) {
    const unsigned row = (rank * inverse) % cap; const auto input = expected(object, row, 0);
    if (writer.append(std::span(input).first(length(row, 0)), 128, ignored) != sn::Error::none) return std::unexpected(Error::invalid_argument);
    holders.rows[row] = sm::location(0, rank);
  }
  draft->finish(); auto owner = capture(store, *draft); if (!owner) return std::unexpected(owner.error());
  sm::Store::Scope basis(store); return publish(store, basis, object, std::move(*owner));
}
template<bool Widths> Result<bool> needs_rewrite(sm::Store& store, unsigned object, unsigned cap, unsigned batch) {
  sm::Store::Scope basis(store); auto source = select(basis, object); if (!source) return std::unexpected(source.error());
  const auto free = 2 * cap - read<Widths>(*source).size() - read<Widths>(*source, 1).size();
  return free < batch; // conservative complete batch admission
}
Result<void> rewrite(sm::Store& store, oa::Pool& pool, unsigned object, bool traversal, Accounting& stats) {
  sm::Store::Scope basis(store); auto source = select(basis, object); if (!source) return std::unexpected(source.error());
  auto target = store.conversion(basis, id(object)); if (!target) return std::unexpected(target.error());
  auto built = sm::hard(pool, *source, traversal, false); if (!built) return std::unexpected(built.error());
  stats.sample(pool);
  auto owner = capture(store, built->draft); if (!owner) return std::unexpected(owner.error());
  auto adopted = store.adopt(*target, *owner); if (!adopted) return adopted;
  // These count issued construction/repair work, not memory-bus traffic.
  stats.copies += owner->selection().control().body_extent + owner->selection().control().second_body_extent;
  stats.repairs += owner->selection().control().holders; ++stats.rewrites;
  return {};
}
template<bool Widths> Result<void> edit(sm::Store& store, oa::Pool& pool, unsigned object, unsigned cap, unsigned epoch, unsigned batch, bool damage_path, Accounting& stats) {
  sm::Store::Scope basis(store); auto source = select(basis, object); if (!source) return std::unexpected(source.error());
  auto draft = stats.measure(0, [&] { return sm::Draft::clone(pool, *source, std::array<std::size_t, 2>{cap * 160, cap * 160}); });
  if (!draft) return std::unexpected(draft.error());
  stats.sample(pool); stats.copies += extent(*source);
  std::array writes{write<Widths>(*draft, 0), write<Widths>(*draft, 1)}; auto holders = draft->holder_view();
  std::array<ikea::owner_write, 256> entries{}; ikea::source_write_journal effects{entries};
  const auto mutation_start = stats.time_phases ? Clock::now() : Clock::time_point{};
  const unsigned first = ((epoch - 1) % (cap / batch)) * batch;
  for (unsigned row = first; row < first + batch; ++row) {
    const auto input = expected(object, row, epoch); const auto bytes = std::span(input).first(length(row, epoch));
    const auto previous = holders.rows[row]; auto& old = writes[sm::block_of(previous)];
    if (row % 4 == 0) {
      if (old.replace(sm::rank_of(previous), bytes, effects) != sn::Error::none) return std::unexpected(Error::invalid_argument);
    } else {
      const unsigned b = writes[1].count < cap ? 1 : 0, rank = writes[b].count;
      if (writes[b].append(bytes, 128 + (epoch % 3) * 16, effects) != sn::Error::none) return std::unexpected(Error::invalid_argument);
      effects.before(holders, {0, row * 4, 4}); holders.rows[row] = sm::location(b, rank);
      if (old.retire(sm::rank_of(previous), effects) != sn::Error::none) return std::unexpected(Error::invalid_argument);
    }
  }
  if (stats.time_phases) stats.phase_ns[1] += elapsed(mutation_start);
  for (const auto& entry : effects.entries()) stats.issued += entry.bytes.size;
  sm::Store::Owner owner;
  if (damage_path) {
    auto damage = stats.measure(3, [&] { return sm::freeze_damage(*source, *draft, writes, holders, effects.entries()); });
    if (!damage) return std::unexpected(damage.error());
    draft = {}; // afterimages retain bytes; release original private allocation
    auto restored = stats.measure(4, [&] { return sm::reconstruct(pool, *source, *damage, {cap * 160, cap * 160}); }); if (!restored) return std::unexpected(restored.error());
    stats.sample(pool); stats.copies += extent(*source);
    auto captured = stats.measure(5, [&] { return capture(store, *restored); }); if (!captured) return std::unexpected(captured.error()); owner = std::move(*captured);
  } else {
    stats.measure(2, [&] { draft->finish(); }); auto captured = stats.measure(5, [&] { return capture(store, *draft); }); if (!captured) return std::unexpected(captured.error()); owner = std::move(*captured);
  }
  auto result = stats.measure(6, [&] { return publish(store, basis, object, std::move(owner)); });
  if (result) stats.edits += batch;
  stats.sample(pool); return result;
}
bool maintenance(sm::Store& store, oa::Pool& pool, orbital::worker::Lane& lane, Accounting& stats, bool trim) {
  const auto start = Clock::now(); const auto deadline = start + std::chrono::seconds(10);
  while (store.drain(1024)) if (Clock::now() > deadline) return false;
  orbital::worker::poll(lane, 128);
  if (trim) for (;;) {
    auto progress = oa::trim_cold(pool, 0);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    if (progress.kind() == orbital::Progress::Kind::failed || Clock::now() > deadline) return false;
    orbital::worker::poll(lane, 128); std::this_thread::yield();
  }
  stats.maintenance_ns += elapsed(start); stats.sample(pool); return true;
}
std::size_t rss() {
  std::ifstream file("/proc/self/statm"); std::size_t pages, resident; if (!(file >> pages >> resident)) return 0;
  return resident * std::size_t(sysconf(_SC_PAGESIZE));
}
template<bool Widths> int run(int argc, char** argv) {
  require(argc >= 9 && argc <= 12, "direct|damage stable|traversal objects capacity epochs cadence read-passes arena-MiB [cadence-phase] [edits-per-publication] [direct32|widths8]");
  const bool damage_path = std::string_view(argv[1]) == "damage", traversal = std::string_view(argv[2]) == "traversal";
  require(damage_path || std::string_view(argv[1]) == "direct", "publication path");
  require(traversal || std::string_view(argv[2]) == "stable", "rewrite order");
  const unsigned objects = std::strtoul(argv[3], nullptr, 10), cap = std::strtoul(argv[4], nullptr, 10), epochs = std::strtoul(argv[5], nullptr, 10),
      cadence = std::strtoul(argv[6], nullptr, 10), passes = std::strtoul(argv[7], nullptr, 10), mib = std::strtoul(argv[8], nullptr, 10);
  const unsigned cadence_phase = argc >= 10 ? std::strtoul(argv[9], nullptr, 10) : 0;
  const unsigned batch = argc >= 11 ? std::strtoul(argv[10], nullptr, 10) : cap / 16;
  require(batch && batch <= cap / 16 && cap % batch == 0, "batch divides capacity and fits damage buffer");
  require(cadence ? cadence_phase < cadence : cadence_phase == 0, "cadence phase in period");
  require(std::has_single_bit(objects) && objects <= 65536 && (cap == 128 || cap == 256 || cap == 512) && epochs && passes, "trace bounds");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(std::uint64_t(mib) << 20)); auto* pool = take(oa::add_pool(*arena, lane)); Accounting stats;
  bool sustained = true; unsigned completed_epoch = 0;
  {
    sm::Store store(*std::pmr::new_delete_resource());
    for (unsigned object = 0; object < objects; ++object) if (!initialize<Widths>(store, *pool, object, cap, stats)) { sustained = false; break; }
    std::unique_ptr<sm::Store::Scope> reader;
    unsigned reader_epoch = 0;
    if (sustained) reader = std::make_unique<sm::Store::Scope>(store);
    std::vector<std::byte> output(cap * 112); // one actual projected output, not an oracle
    std::printf("epoch,edits,rewrites,refusals,edit_ns,rewrite_ns,traversal_ns,maintenance_ns,admission_ns,wall_ns,oracle_ns,issued_bytes,source_copy_and_rewrite_body_bytes,repaired_holders,sampled_peak_charge,current_charge,mapped,reserved,idle,rss\n");
    for (unsigned epoch = 1; sustained && epoch <= epochs; ++epoch) {
      const auto epoch_start = Clock::now();
      for (unsigned i = 0; i < objects; ++i) {
        const auto object = (i * 0x9e3779b9u) & (objects - 1);
        const auto admission_start = Clock::now();
        auto pressure = needs_rewrite<Widths>(store, object, cap, batch); require(bool(pressure), "query current rank capacity");
        stats.admission_ns += elapsed(admission_start);
        auto attempt = [&](auto operation, double& timing) {
          for (unsigned retry = 0; retry < 2; ++retry) {
            const auto start = Clock::now();
            Result<void> result;
            try { result = operation(); }
            catch (const std::bad_alloc&) {
              std::fprintf(stderr, "process scratch allocation failed outside Orbital Arena budget\n"); throw;
            }
            timing += elapsed(start);
            if (result) return true;
            require(result.error() == Error::capacity, "only capacity refusal is a recoverable trace outcome");
            ++stats.refusals;
            if (!maintenance(store, *pool, lane, stats, true)) return false;
          }
          return false; // prescribed reader schedule is never shortened to rescue the trace
        };
        if (*pressure || (cadence && epoch % cadence == cadence_phase))
          if (!attempt([&] { return rewrite(store, *pool, object, traversal, stats); }, stats.rewrite_ns)) { sustained = false; break; }
        if (!attempt([&] { return edit<Widths>(store, *pool, object, cap, epoch, batch, damage_path, stats); }, stats.edit_ns)) { sustained = false; break; }
        const auto routine_start = Clock::now(); store.drain(64); orbital::worker::poll(lane, 32);
        stats.maintenance_ns += elapsed(routine_start);
      }
      if (!sustained) break;
      {
        sm::Store::Scope current(store);
        const auto oracle_start = Clock::now();
        for (unsigned object = 0; object < objects; ++object) {
          const auto s = take(select(current, object)); check<Widths>(s, object, cap, epoch, batch);
          const auto size = project<Widths>(s, output); check_projection(std::span(output).first(size), object, cap, epoch, batch);
        }
        stats.oracle_ns += elapsed(oracle_start);
        const auto start = Clock::now();
        for (unsigned pass = 0; pass < passes; ++pass) for (unsigned i = 0; i < objects; ++i) {
          const auto object = (i * 0x9e3779b9u) & (objects - 1); const auto s = take(select(current, object));
          const auto offset = project<Widths>(s, output);
          escape(output.data()); escape(&offset);
        }
        stats.traversal_ns += elapsed(start);
      }
      // Delayed discovery by the stationary Scope, not cached per-object Owners.
      const auto oracle_start = Clock::now();
      for (unsigned object = 0; object < objects; ++object) check<Widths>(reader->bind(id(object)), object, cap, reader_epoch, batch);
      stats.oracle_ns += elapsed(oracle_start);
      if (epoch % 2 == 0) {
        reader.reset(); require(maintenance(store, *pool, lane, stats, true), "bounded live reclamation");
        reader = std::make_unique<sm::Store::Scope>(store); reader_epoch = epoch;
      }
      completed_epoch = epoch; stats.sample(*pool, true); const auto s = oa::statistics(*pool);
      stats.wall_ns += elapsed(epoch_start);
      std::printf("%u,%llu,%llu,%llu,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu\n", epoch,
          (unsigned long long)stats.edits, (unsigned long long)stats.rewrites, (unsigned long long)stats.refusals,
          stats.edit_ns, stats.rewrite_ns, stats.traversal_ns, stats.maintenance_ns, stats.admission_ns, stats.wall_ns, stats.oracle_ns,
          (unsigned long long)stats.issued, (unsigned long long)stats.copies, (unsigned long long)stats.repairs,
          (unsigned long long)stats.peak_charge, (unsigned long long)arena->charged_bytes.load(),
          (unsigned long long)s.mapped_bytes, (unsigned long long)s.reserved_bytes, (unsigned long long)s.idle_bytes, rss()); std::fflush(stdout);
    }
    reader.reset(); require(maintenance(store, *pool, lane, stats, true), "final retirement while Store remains live");
    std::fprintf(stderr, "phase_sampling=%d sustained=%d completed_epochs=%u sampled_peak_charge=%llu live_charge=%llu refusals=%llu\n", stats.sample_phases, sustained, completed_epoch,
        (unsigned long long)stats.peak_charge, (unsigned long long)arena->charged_bytes.load(), (unsigned long long)stats.refusals);
  }
  for (;;) { const auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "eventual settlement"); require(bool(oa::remove_pool(*pool)), "remove Pool"); arena.reset(); orbital::worker::relinquish(lane);
  if (stats.time_phases) {
    const std::array labels{"clone", "commands", "finish", "freeze-including-finish", "reconstruct", "capture", "prepare-install"};
    for (unsigned i = 0; i < labels.size(); ++i) std::fprintf(stderr, "phase_%s_ns=%.0f\n", labels[i], stats.phase_ns[i]);
  }
  // Unsustained pressure is a reported experiment outcome, not silent success.
  return sustained ? 0 : 2;
}

int main(int argc, char** argv) {
  const auto geometry = argc == 12 ? std::string_view(argv[11]) : std::string_view("direct32");
  require(geometry == "direct32" || geometry == "widths8", "geometry recipe");
  if (geometry == "widths8") return run<true>(argc, argv);
  return run<false>(argc, argv);
}
