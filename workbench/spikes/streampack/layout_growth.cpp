#include "layout.h"
#include <orbital/allocation.h>
#include <chrono>
#include <string>
#include <thread>

namespace oa = orbital::allocation;
namespace sl = streampack_layout;
using sl::require;
template<class R> auto take(R r) { require(bool(r), "Orbital result"); return std::move(*r); }
void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }
enum class Mode { compact, fixed_prefix, fixed_split, full };
struct Totals {
  std::uint64_t allocations{}, growths{}, relocations{}, allocator_preserved{}, repacked{}, initialized{}, issued{}, capacity{}, requested{};
  Totals& operator+=(const Totals& b) {
    allocations += b.allocations; growths += b.growths; relocations += b.relocations; allocator_preserved += b.allocator_preserved;
    repacked += b.repacked; initialized += b.initialized; issued += b.issued; capacity += b.capacity; requested += b.requested; return *this;
  }
};
// One concrete private metadata owner for the allocation experiment. R/U have
// the same per-16-slot organization in every treatment. No payload or MVCC.
template<unsigned N, unsigned Maximum, Mode M> struct Block {
  using W = std::conditional_t<(Maximum <= 255), std::uint8_t, std::uint16_t>;
  using C = std::conditional_t<((N - 16) * Maximum <= 65535), std::uint16_t, std::uint32_t>;
  // Align R/U to match the full-population read experiment. Directory bytes
  // and alignment padding are separately visible in requested allocation size.
  static constexpr unsigned length_group_bytes = 32 * sizeof(W);
  static constexpr bool split = M == Mode::fixed_split;
  oa::Allocation lengths_owner, control_owner;
  unsigned groups{}, count{}, extent{};
  Totals total;
  unsigned control_groups() const { return M == Mode::compact ? groups : N / 16; }
  unsigned control_bytes() const { return control_groups() * (2 + sizeof(C)); }
  unsigned length_origin() const { return split ? 0 : sl::align64(control_bytes()); }
  std::uint8_t* controls() const { return reinterpret_cast<std::uint8_t*>(split ? control_owner.address : lengths_owner.address); }
  std::uint8_t* lengths() const { return reinterpret_cast<std::uint8_t*>(lengths_owner.address) + length_origin(); }
  unsigned size(unsigned plane) const { return plane ? control_owner.size : lengths_owner.size; }
  std::uint8_t* data(unsigned plane) const { return reinterpret_cast<std::uint8_t*>(plane ? control_owner.address : lengths_owner.address); }
  unsigned cp_offset(unsigned group) const { return control_groups() * 2 + group * sizeof(C); }
  std::uint8_t* r(unsigned row) const { return lengths() + (row / 16 * 32 + row % 16) * sizeof(W); }
  std::uint8_t* u(unsigned row) const { return r(row) + 16 * sizeof(W); }
  template<class Journal> void before(Journal& j, unsigned plane, unsigned offset, unsigned n) { if (n) { j.before(*this, ikea::byte_write{plane, offset, n}); total.issued += n; } }
  template<class Journal> void zero(Journal& j, unsigned plane, unsigned offset, unsigned n) {
    before(j, plane, offset, n); std::memset(data(plane) + offset, 0, n); total.initialized += n;
  }
  template<class Journal> void move(Journal& j, unsigned dest, unsigned from, unsigned n) {
    before(j, 0, dest, n); std::memmove(data(0) + dest, data(0) + from, n); total.repacked += n;
  }
  template<class Journal> void ensure(oa::Pool& pool, unsigned needed, Journal& effects) {
    if (needed <= groups * 16) return;
    const auto old_groups = groups, old_origin = length_origin();
    groups = M == Mode::full ? N / 16 : std::min(N / 16, std::max(1u, groups * 2));
    const auto control = control_bytes(), origin = length_origin(), length_bytes = groups * length_group_bytes;
    if (!old_groups) {
      lengths_owner = take(oa::allocate(pool, {.bytes = length_bytes + origin, .alignment = 64})); ++total.allocations;
      if constexpr (split) { control_owner = take(oa::allocate(pool, {.bytes = control, .alignment = 64})); ++total.allocations; zero(effects, 1, 0, control); }
      zero(effects, 0, 0, lengths_owner.size); return;
    }
    const auto old_size = lengths_owner.size; const auto old_address = reinterpret_cast<std::uintptr_t>(lengths_owner.address);
    require(bool(oa::grow(lengths_owner, {.bytes = length_bytes + origin, .alignment = 64})), "private metadata growth"); ++total.growths;
    if (reinterpret_cast<std::uintptr_t>(lengths_owner.address) != old_address) { ++total.relocations; total.allocator_preserved += old_size; }
    if constexpr (M == Mode::compact) {
      if (origin != old_origin) move(effects, origin, old_origin, old_groups * length_group_bytes);
      move(effects, groups * 2, old_groups * 2, old_groups * sizeof(C));
      zero(effects, 0, old_groups * 2, (groups - old_groups) * 2);
      zero(effects, 0, groups * 2 + old_groups * sizeof(C), (groups - old_groups) * sizeof(C));
      if (origin != old_origin) zero(effects, 0, control, origin - control);
    }
    zero(effects, 0, origin + old_groups * length_group_bytes, (groups - old_groups) * length_group_bytes);
  }
  template<class Journal> void append(oa::Pool& pool, unsigned reserved, unsigned used, bool dead, Journal& effects) {
    require(count < N && used <= reserved && reserved <= Maximum, "admitted append"); ensure(pool, count + 1, effects);
    const auto origin = length_origin();
    const auto ro = origin + (count / 16 * 32 + count % 16) * sizeof(W);
    before(effects, 0, ro, sizeof(W)); sl::store<W>(data(0) + ro, reserved);
    before(effects, 0, ro + 16 * sizeof(W), sizeof(W)); sl::store<W>(data(0) + ro + 16 * sizeof(W), used);
    before(effects, split ? 1 : 0, count / 8, 1);
    auto* d = controls() + count / 8; const auto bit = std::uint8_t(1u << (count % 8)); *d = (*d & ~bit) | (dead ? bit : 0);
    if (count % 16 == 0) { before(effects, split ? 1 : 0, cp_offset(count / 16), sizeof(C)); sl::store<C>(controls() + cp_offset(count / 16), extent); }
    extent += reserved; ++count;
  }
  Totals finish() { total.capacity = oa::capacity(lengths_owner) + (split ? oa::capacity(control_owner) : 0); total.requested = lengths_owner.size + control_owner.size; return total; }
};
unsigned reserved(unsigned row, unsigned maximum) { return row % 5 == 0 ? 0 : row % 11 == 0 ? maximum : (row * 0x9e3779b9u) % (maximum + 1); }
unsigned used(unsigned row, unsigned r) { return r ? (row * 0x85ebca6bu) % (r + 1) : 0; }
template<class B, unsigned N, unsigned Maximum> void check(oa::Pool& pool, unsigned pattern) {
  B block;
  auto reservation = [&](unsigned row) { return pattern == 1 ? Maximum : pattern == 2 ? 0 : reserved(row, Maximum); };
  for (unsigned row = 0; row < N; ++row) {
    std::array<std::vector<std::uint8_t>, 2> before, replay;
    for (unsigned p = 0; p < 2; ++p) if (block.size(p)) before[p].assign(block.data(p), block.data(p) + block.size(p));
    std::array<ikea::owner_write, 16> records{}; ikea::source_write_journal effects{records};
    const auto r = reservation(row); block.append(pool, r, used(row, r), row % 7 == 0, effects);
    replay = before; for (unsigned p = 0; p < 2; ++p) replay[p].resize(block.size(p), 0xa5);
    for (auto e : effects.entries()) {
      require(e.source == &block && e.bytes.plane < 2 && e.bytes.offset + e.bytes.size <= block.size(e.bytes.plane), "append source/physical damage bounds");
      std::memcpy(replay[e.bytes.plane].data() + e.bytes.offset, block.data(e.bytes.plane) + e.bytes.offset, e.bytes.size);
    }
    for (unsigned p = 0; p < 2; ++p) require(std::equal(replay[p].begin(), replay[p].end(), block.data(p)), "grow/append afterimage reconstruction");
    unsigned extent = 0;
    for (unsigned i = 0; i <= row; ++i) {
      if (i % 16 == 0) require(sl::load<typename B::C>(block.controls() + block.cp_offset(i / 16)) == extent, "checkpoint remains schema-wide and exact");
      const auto r = reservation(i);
      require(sl::load<typename B::W>(block.r(i)) == r && sl::load<typename B::W>(block.u(i)) == used(i, r), "growth preserves exact mixed R/U lanes");
      require(bool((block.controls()[i / 8] >> (i % 8)) & 1) == (i % 7 == 0), "growth preserves tombstones"); extent += r;
    }
    require(extent == block.extent && block.count == row + 1 && block.groups * 16 >= block.count, "coherent count/extent/capacity");
  }
}
template<unsigned N, unsigned Maximum, Mode M> void run(oa::Pool& pool, const char* name, unsigned stop, bool checking) {
  using B = Block<N, Maximum, M>;
  if (checking) { for (unsigned pattern = 0; pattern < 3; ++pattern) check<B, N, Maximum>(pool, pattern); return; }
  auto execute = [&] {
    Totals total;
    for (unsigned b = 0; b < 256; ++b) {
      B block; std::array<ikea::owner_write, 16> records{};
      for (unsigned row = 0; row < stop; ++row) {
        ikea::source_write_journal effects{records}; const auto r = reserved(row, Maximum);
        block.append(pool, r, used(row, r), row % 7 == 0, effects); escape(records.data());
      }
      escape(block.lengths_owner.address); escape(block.control_owner.address); total += block.finish();
    }
    return total;
  };
  execute();
  for (unsigned rep = 0; rep < 3; ++rep) {
    const auto begin = std::chrono::steady_clock::now(); unsigned loops = 0; Totals result; double ns;
    do { result += execute(); ++loops; ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count(); } while (ns < 10e6);
    std::printf("%s,%u,%u,%u,%u,%.5f,%u,64,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", name, N, Maximum, stop, rep, ns / loops / 256 / stop, unsigned(sizeof(typename B::C)),
      double(result.allocations) / loops / 256, double(result.growths) / loops / 256, double(result.relocations) / loops / 256,
      double(result.allocator_preserved) / loops / 256, double(result.repacked) / loops / 256, double(result.initialized) / loops / 256,
      double(result.issued) / loops / 256, double(result.capacity) / loops / 256, double(result.requested) / loops / 256);
  }
}
template<unsigned N, unsigned Maximum> void cases(oa::Pool& p, bool checking, bool reverse) {
  for (unsigned n : {1u, 17u, 64u, N}) {
    if (checking && n != N) continue;
    if (!reverse) {
      run<N, Maximum, Mode::compact>(p,"compact",n,checking); run<N, Maximum, Mode::fixed_prefix>(p,"fixed-prefix",n,checking);
      run<N, Maximum, Mode::fixed_split>(p,"fixed-split",n,checking); run<N, Maximum, Mode::full>(p,"full",n,checking);
    } else {
      run<N, Maximum, Mode::full>(p,"full",n,checking); run<N, Maximum, Mode::fixed_split>(p,"fixed-split",n,checking);
      run<N, Maximum, Mode::fixed_prefix>(p,"fixed-prefix",n,checking); run<N, Maximum, Mode::compact>(p,"compact",n,checking);
    }
  }
}
int main(int argc, char** argv) {
  const bool checking = argc == 2 && std::string(argv[1]) == "check", reverse = argc == 2 && std::string(argv[1]) == "reverse";
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(64u << 20)); auto* pool = take(oa::add_pool(*arena, lane));
  if (!checking) std::puts("mode,N,maximum,count,repetition,ns_per_append,checkpoint_bytes,length_alignment,allocations,growths,relocations,preserved_bytes,repacked_bytes,initialized_bytes,issued_bytes,capacity_bytes,requested_bytes");
  cases<128,255>(*pool,checking,reverse); cases<256,255>(*pool,checking,reverse); cases<512,255>(*pool,checking,reverse);
  cases<128,4095>(*pool,checking,reverse); cases<256,4095>(*pool,checking,reverse); cases<512,4095>(*pool,checking,reverse);
  for (;;) { auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane,128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "refunded metadata backing"); require(bool(oa::remove_pool(*pool)), "remove Pool"); arena.reset(); orbital::worker::relinquish(lane);
  if (checking) std::puts("growth checks passed");
}
