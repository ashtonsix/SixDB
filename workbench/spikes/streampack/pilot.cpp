#include "byte_search.h"
#include <duckdb/common/types/string_type.hpp>
#include <orbital/allocation.h>
#include <ikea/seriespack.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace sn = streampack_next;
namespace oa = orbital::allocation;
using Clock = std::chrono::steady_clock;
using Values = std::vector<std::vector<std::byte>>;

void require(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::abort(); }
}
template<class Result> auto take(Result result) {
  require(bool(result), "Orbital operation"); return std::move(*result);
}
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }

// A ready-read experiment. These owners are fixtures, not a proposed container.
// The immutable full-value recipe deliberately has no liveness/used planes.
template<class End> struct Block {
  const End* ends;
  const std::byte* body;
};
template<class End> struct Dense {
  const Block<End>* blocks;
  unsigned shift, mask;
  std::size_t length(unsigned row) const {
    const auto* ends = blocks[row >> shift].ends;
    const auto r = row & mask;
    return ends[r + 1] - ends[r];
  }
  sn::Bytes value(unsigned row) const {
    const auto& b = blocks[row >> shift]; const auto r = row & mask;
    return {b.body + b.ends[r], std::size_t(b.ends[r + 1] - b.ends[r])};
  }
};
struct Flat {
  const std::uint32_t* ends;
  const std::byte* body;
  std::size_t length(unsigned row) const { return ends[row + 1] - ends[row]; }
  sn::Bytes value(unsigned row) const { return {body + ends[row], length(row)}; }
};
struct Location { std::uint32_t offset; std::uint16_t length, unused; };
static_assert(sizeof(Location) == 8);
struct Descriptors {
  const Location* locations;
  const std::byte* body;
  std::size_t length(unsigned row) const { return locations[row].length; }
  sn::Bytes value(unsigned row) const {
    const auto d = locations[row]; return {body + d.offset, d.length};
  }
};
struct Duck {
  const duckdb::string_t* strings;
  std::size_t length(unsigned row) const { return strings[row].GetSize(); }
  sn::Bytes value(unsigned row) const {
    return {reinterpret_cast<const std::byte*>(strings[row].GetData()), length(row)};
  }
};
struct NativeIndex {
  const unsigned* rows;
  unsigned get(unsigned r) const { return rows[r]; }
  void read(unsigned first, unsigned count, unsigned* out) const { std::memcpy(out, rows + first, count * sizeof(unsigned)); }
};
struct PackedIndex {
  ikea::seriespack::erased_reader<unsigned> decoder;
  unsigned get(unsigned r) const { return decoder.get_unchecked(r); }
  void read(unsigned first, unsigned count, unsigned* out) const { decoder.read_unchecked(first, count, out); }
};
template<class Reader, class Index> struct Mapped {
  Reader base;
  Index index;
  auto length(unsigned row) const { return base.length(index.get(row)); }
  auto value(unsigned row) const { return base.value(index.get(row)); }
};
struct ReadFixture {
  Values oracle;
  oa::Allocation body, duck_body;
  std::vector<std::uint32_t> flat, ends32;
  std::vector<std::uint16_t> ends16;
  std::vector<Block<std::uint16_t>> blocks16;
  std::vector<Block<std::uint32_t>> blocks32;
  std::vector<Location> descriptors;
  std::vector<duckdb::string_t> strings;
  unsigned capacity;
  std::size_t outlined_bytes{};

  ReadFixture(oa::Pool& pool, unsigned count, unsigned capacity, bool tiny) : capacity(capacity) {
    oracle.resize(count); flat.push_back(0);
    std::mt19937 rng(21351);
    for (unsigned row = 0; row < count; ++row) {
      auto& value = oracle[row];
      value.resize(tiny ? row % 16 : (row % 16 == 0 ? 1024 : row % 127));
      for (auto& b : value) b = std::byte(rng() & 255);
      flat.push_back(flat.back() + value.size());
      if (value.size() > 12) outlined_bytes += value.size();
    }
    body = take(oa::allocate(pool, {.bytes = flat.back(), .alignment = 64}));
    if (outlined_bytes) duck_body = take(oa::allocate(pool, {.bytes = outlined_bytes, .alignment = 64}));
    std::size_t duck_offset = 0;
    ends16.reserve(count + count / capacity); ends32.reserve(count + count / capacity);
    for (unsigned row = 0; row < count; ++row) {
      if (row % capacity == 0) { ends16.push_back(0); ends32.push_back(0); }
      const auto offset = flat[row]; const auto& value = oracle[row];
      if (!value.empty()) std::memcpy(body.address + offset, value.data(), value.size());
      const auto end = flat[row + 1] - flat[row - row % capacity];
      require(end <= 65535, "direct16 identical budget");
      ends16.push_back(end); ends32.push_back(end);
      descriptors.push_back({offset, std::uint16_t(value.size()), 0});
      if (value.size() > 12) {
        std::memcpy(duck_body.address + duck_offset, value.data(), value.size());
        strings.emplace_back(reinterpret_cast<const char*>(duck_body.address + duck_offset), value.size());
        duck_offset += value.size();
      } else strings.emplace_back(reinterpret_cast<const char*>(body.address + offset), value.size());
    }
    for (unsigned row = 0; row < count; row += capacity) {
      const auto offset = (row / capacity) * (capacity + 1);
      blocks16.push_back({ends16.data() + offset, body.address + flat[row]});
      blocks32.push_back({ends32.data() + offset, body.address + flat[row]});
    }
  }
  template<class End> auto dense() const {
    if constexpr (sizeof(End) == 2)
      return Dense<End>{blocks16.data(), unsigned(std::countr_zero(capacity)), capacity - 1};
    else
      return Dense<End>{blocks32.data(), unsigned(std::countr_zero(capacity)), capacity - 1};
  }
};

enum class ReadOp { length, touch, project };
const char* read_name(ReadOp op) {
  switch (op) {
    case ReadOp::length: return "length";
    case ReadOp::touch: return "first-last";
    case ReadOp::project: return "project";
  }
  std::abort();
}
// All candidates write the same natural ordered output. Projection copies bytes;
// lightweight operations avoid making an unnecessary span or hashing the value.
template<ReadOp Op, class Reader>
__attribute__((noinline)) void read_rows(Reader reader, std::span<const unsigned> rows,
    std::span<std::uint32_t> output, std::span<std::byte> projected) {
  std::size_t into = 0;
  for (unsigned i = 0; i < rows.size(); ++i) {
    if constexpr (Op == ReadOp::length) output[i] = reader.length(rows[i]);
    else {
      const auto value = reader.value(rows[i]);
      if constexpr (Op == ReadOp::touch)
        output[i] = value.empty() ? 0 : unsigned(value.size()) |
            (std::to_integer<unsigned>(value.front()) << 16) | (std::to_integer<unsigned>(value.back()) << 24);
      else {
        if (!value.empty()) std::memcpy(projected.data() + into, value.data(), value.size());
        into += value.size(); output[i] = into;
      }
    }
  }
}

// Explicit contiguous-range contract; bind geometry once per physical block.
// Packed all-full bodies permit run copies. DuckDB retains its own inline law.
template<ReadOp Op, class Reader>
__attribute__((noinline)) void read_range(Reader reader, unsigned count,
    std::span<std::uint32_t> output, std::span<std::byte> projected) {
  std::size_t into = 0;
  const auto one = [&](auto local, unsigned r, unsigned output_row) {
    if constexpr (Op == ReadOp::length) output[output_row] = local.length(r);
    if constexpr (Op == ReadOp::touch) {
      const auto v = local.value(r);
      output[output_row] = v.empty() ? 0 : unsigned(v.size()) |
          (std::to_integer<unsigned>(v.front()) << 16) | (std::to_integer<unsigned>(v.back()) << 24);
    }
    if constexpr (Op == ReadOp::project) { into += local.length(r); output[output_row] = into; }
  };
  if constexpr (requires { reader.index; }) {
    alignas(64) std::array<unsigned, 128> locations;
    for (unsigned first = 0; first < count; first += locations.size()) {
      const auto n = std::min<unsigned>(count - first, locations.size());
      reader.index.read(first, n, locations.data());
      for (unsigned i = 0; i < n; ++i) {
        if constexpr (Op == ReadOp::project) {
          const auto v = reader.base.value(locations[i]);
          if (!v.empty()) std::memcpy(projected.data() + into, v.data(), v.size());
          into += v.size(); output[first + i] = into;
        } else one(reader.base, locations[i], first + i);
      }
    }
  } else if constexpr (requires { reader.blocks; }) {
    for (unsigned first = 0; first < count; first += reader.mask + 1) {
      const auto& block = reader.blocks[first >> reader.shift];
      const unsigned n = std::min(count - first, reader.mask + 1);
      // Direct block loop keeps concrete End type and one body address visible.
      for (unsigned r = 0; r < n; ++r) {
        const auto length = block.ends[r + 1] - block.ends[r];
        if constexpr (Op == ReadOp::length) output[first + r] = length;
        if constexpr (Op == ReadOp::project) output[first + r] = into + block.ends[r + 1];
        if constexpr (Op == ReadOp::touch) {
          const auto* p = block.body + block.ends[r];
          output[first + r] = length ? unsigned(length) | (std::to_integer<unsigned>(p[0]) << 16) |
              (std::to_integer<unsigned>(p[length - 1]) << 24) : 0;
        }
      }
      if constexpr (Op == ReadOp::project) {
        const auto bytes = block.ends[n];
        if (bytes) std::memcpy(projected.data() + into, block.body, bytes);
        into += bytes;
      }
    }
  } else if constexpr ((std::is_same_v<Reader, Flat> || std::is_same_v<Reader, Descriptors>) && Op == ReadOp::project) {
    // These fixture recipes are admitted ordered/all-full too. Their explicit
    // offsets already supply projection ends: do not handicap them with a sum.
    for (unsigned r = 0; r < count; ++r) {
      if constexpr (std::is_same_v<Reader, Flat>) output[r] = reader.ends[r + 1];
      else output[r] = reader.locations[r].offset + reader.locations[r].length;
    }
    if (count && output[count - 1]) std::memcpy(projected.data(), reader.body, output[count - 1]);
  } else if constexpr (std::is_same_v<Reader, Duck> && Op == ReadOp::project) {
    for (unsigned r = 0; r < count; ++r) {
      const auto v = reader.value(r);
      if (!v.empty()) std::memcpy(projected.data() + into, v.data(), v.size());
      into += v.size(); output[r] = into;
    }
  } else {
    for (unsigned r = 0; r < count; ++r) one(reader, r, r);
    if constexpr (Op == ReadOp::project)
      if (into) std::memcpy(projected.data(), reader.body, into);
  }
}

struct Samples {
  unsigned iterations;
  double elapsed_ns;
};
template<class Run> Samples time_operation(Run&& run) {
  unsigned iterations = 1;
  for (;;) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) run();
    const double ns = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    if (ns >= 8000000 || iterations >= 1048576) return {iterations, ns};
    iterations *= 2;
  }
}

template<ReadOp Op, bool Range = false, class Reader>
void read_trial(const char* dataset, const char* order, const char* method, Reader reader,
    ReadFixture& fixture, std::span<const unsigned> rows, unsigned repetition,
    std::size_t metadata_bytes, std::size_t payload_bytes, std::span<const unsigned> mapping = {}) {
  std::vector<std::uint32_t> expected(rows.size()), output(rows.size(), 0xdead);
  std::vector<std::byte> expected_bytes;
  for (unsigned i = 0; i < rows.size(); ++i) {
    const auto& v = fixture.oracle[mapping.empty() ? rows[i] : mapping[rows[i]]];
    if constexpr (Op == ReadOp::length) expected[i] = v.size();
    if constexpr (Op == ReadOp::touch) expected[i] = v.empty() ? 0 : unsigned(v.size()) |
        (std::to_integer<unsigned>(v.front()) << 16) | (std::to_integer<unsigned>(v.back()) << 24);
    if constexpr (Op == ReadOp::project) {
      expected_bytes.insert(expected_bytes.end(), v.begin(), v.end()); expected[i] = expected_bytes.size();
    }
  }
  std::vector<std::byte> projected(expected_bytes.size());
  const auto operation = [&] {
    if constexpr (Range) read_range<Op>(reader, rows.size(), output, projected);
    else read_rows<Op>(reader, rows, output, projected);
    escape(output.data()); escape(projected.data());
  };
  operation();
  require(output == expected && projected == expected_bytes, "exact read operation outputs");
  const auto sample = time_operation(operation);
  require(output == expected && projected == expected_bytes, "post timing exact read outputs");
  std::printf("read,%s,%u,%s,%s,%s,%u,%zu,%u,%.3f,%zu,%zu\n", dataset,
      fixture.capacity, order, read_name(Op), method, repetition, rows.size(), sample.iterations,
      sample.elapsed_ns / sample.iterations / rows.size(), metadata_bytes, payload_bytes);
}

void holder_suite(oa::Pool& pool, unsigned repeats) {
  namespace sp = ikea::seriespack;
#if defined(__aarch64__)
  using F = sp::preset_format<12, sp::preset::bulk_arm>;
#else
  using F = sp::preset_format<12, sp::preset::bulk_x86>;
#endif
  constexpr unsigned count = 4096;
  std::vector<unsigned> mapping(count), requests(count);
  std::iota(mapping.begin(), mapping.end(), 0); std::iota(requests.begin(), requests.end(), 0);
  std::mt19937 rng(9274); std::shuffle(mapping.begin(), mapping.end(), rng);
  // Some shared physical values: the logical oracle follows each actual holder.
  for (unsigned i = 0; i < count; i += 7) mapping[i] = mapping[0];
  auto encoded = take(oa::allocate(pool, {.bytes = count / F::tile_rows * F::tile_bytes, .alignment = 64}));
  auto placed = sp::view<F>::attach(count, {{{std::as_writable_bytes(std::span{reinterpret_cast<std::uint8_t*>(encoded.address), encoded.size}), F::tile_bytes},{},{}}});
  require(bool(placed), "actual packed holder placement");
  auto prepared = sp::bind_writer(*placed); require(bool(prepared), "packed holder writer");
  auto write = prepared->erase<unsigned, sp::author::sum_change>();
  std::vector<ikea::journal::entry> records(count * 2);
  ikea::journal effects{records};
  require(bool(write.initialize(mapping, effects)), "packed holder initialize");
  auto decoded = sp::bind_reader<unsigned>(*placed);
  for (unsigned i = 0; i < count; ++i) require(decoded.get_unchecked(i) == mapping[i], "exact holder identity");
  for (unsigned capacity : {128, 256, 512}) for (bool tiny : {true, false}) {
    ReadFixture f(pool, count, capacity, tiny);
    const auto run = [&]<ReadOp Op>(auto base, const char* plain, const char* packed,
        std::size_t meta, std::size_t payload) {
      const auto one = [&](auto reader, const char* name, std::size_t holder_bytes) {
        for (unsigned rep = 0; rep < repeats; ++rep) {
          read_trial<Op>(tiny ? "tiny-held" : "mixed-held", "holder-point", name, reader, f,
              requests, rep, meta + holder_bytes, payload, mapping);
          read_trial<Op, true>(tiny ? "tiny-held" : "mixed-held", "holder-range128", name, reader, f,
              requests, rep, meta + holder_bytes, payload, mapping);
        }
      };
      one(Mapped{base, NativeIndex{mapping.data()}}, plain, mapping.size() * sizeof(unsigned));
      one(Mapped{base, PackedIndex{decoded}}, packed, encoded.size);
    };
    const auto methods = [&]<ReadOp Op> {
      run.template operator()<Op>(f.dense<std::uint16_t>(), "direct16-native", "direct16-series12",
          f.ends16.size() * 2 + f.blocks16.size() * sizeof(Block<std::uint16_t>), f.flat.back());
      run.template operator()<Op>(Flat{f.flat.data(), f.body.address}, "flat32-native", "flat32-series12",
          f.flat.size() * 4, f.flat.back());
      run.template operator()<Op>(Duck{f.strings.data()}, "duckdb16-native", "duckdb16-series12",
          f.strings.size() * sizeof(duckdb::string_t), f.outlined_bytes);
    };
    methods.template operator()<ReadOp::length>();
    methods.template operator()<ReadOp::touch>();
    methods.template operator()<ReadOp::project>();
  }
}

void read_suite(oa::Pool& pool, unsigned count, unsigned repeats) {
  for (unsigned capacity : {128, 256, 512}) for (bool tiny : {true, false}) {
    ReadFixture f(pool, count, capacity, tiny);
    for (bool random : {false, true}) {
      std::vector<unsigned> rows(count); std::iota(rows.begin(), rows.end(), 0);
      if (random) { std::mt19937 rng(764); std::shuffle(rows.begin(), rows.end(), rng); }
      const auto run = [&]<ReadOp Op>(auto reader, const char* name, std::size_t metadata, std::size_t payload) {
        for (unsigned repeat = 0; repeat < repeats; ++repeat)
          read_trial<Op>(tiny ? "tiny" : "mixed", random ? "shuffled" : "sequential", name,
              reader, f, rows, repeat, metadata, payload);
        if (!random) for (unsigned repeat = 0; repeat < repeats; ++repeat)
          read_trial<Op, true>(tiny ? "tiny" : "mixed", "range", name,
              reader, f, rows, repeat, metadata, payload);
      };
      const auto methods = [&]<ReadOp Op> {
        run.template operator()<Op>(Flat{f.flat.data(), f.body.address}, "flat32",
            f.flat.size() * 4, f.flat.back());
        run.template operator()<Op>(f.dense<std::uint16_t>(), "direct16",
            f.ends16.size() * 2 + f.blocks16.size() * sizeof(Block<std::uint16_t>), f.flat.back());
        run.template operator()<Op>(f.dense<std::uint32_t>(), "direct32",
            f.ends32.size() * 4 + f.blocks32.size() * sizeof(Block<std::uint32_t>), f.flat.back());
        run.template operator()<Op>(Descriptors{f.descriptors.data(), f.body.address}, "location8",
            f.descriptors.size() * sizeof(Location), f.flat.back());
        run.template operator()<Op>(Duck{f.strings.data()}, "duckdb16",
            f.strings.size() * sizeof(duckdb::string_t), f.outlined_bytes);
      };
      methods.template operator()<ReadOp::length>();
      methods.template operator()<ReadOp::touch>();
      methods.template operator()<ReadOp::project>();
    }
  }
}

enum class SearchOp { any, membership, count };
enum class SearchMethod { libc, native_value, sparse, segmented };
template<SearchOp Op, unsigned Capacity> using SearchResult =
    std::conditional_t<Op == SearchOp::membership, std::array<std::uint64_t, Capacity / 64>,
    std::conditional_t<Op == SearchOp::any, bool, std::uint64_t>>;
template<SearchOp Op, SearchMethod Method, unsigned Capacity, class View>
__attribute__((noinline)) SearchResult<Op, Capacity> search(View view, std::byte needle) {
  SearchResult<Op, Capacity> result{};
  if constexpr (Method == SearchMethod::libc && Op != SearchOp::count) {
    // libc existence/membership searches once per value; it never enumerates
    // every occurrence to recover a row-level answer.
    for (unsigned r = 0; r < view.size(); ++r) if (view.live(r)) {
      const auto v = view.value(r);
      if (!v.empty() && std::memchr(v.data(), std::to_integer<unsigned>(needle), v.size())) {
        if constexpr (Op == SearchOp::any) return true;
        else result[r / 64] |= 1ull << (r % 64);
      }
    }
  } else if constexpr (Method == SearchMethod::native_value) {
    for (unsigned r = 0; r < view.size(); ++r) if (view.live(r)) {
      sn::byte_masks(view.value(r), needle, [&](auto, auto mask, auto) {
        if constexpr (Op == SearchOp::count) { result += mask.population(); return true; }
        if constexpr (Op == SearchOp::membership) result[r / 64] |= 1ull << (r % 64);
        if constexpr (Op == SearchOp::any) result = true;
        return false;
      });
      if constexpr (Op == SearchOp::any) if (result) return true;
    }
  } else if constexpr (Method == SearchMethod::segmented) {
    sn::byte_segmented(view, needle, [&](auto r, auto, auto, auto mask) {
      if constexpr (Op == SearchOp::any) { result = true; return false; }
      if constexpr (Op == SearchOp::membership) result[r / 64] |= 1ull << (r % 64);
      if constexpr (Op == SearchOp::count) result += mask.population();
      return true;
    });
  } else {
    const auto emit = [&](auto r, auto) {
      if constexpr (Op == SearchOp::any) { result = true; return false; }
      if constexpr (Op == SearchOp::membership) result[r / 64] |= 1ull << (r % 64);
      if constexpr (Op == SearchOp::count) ++result;
      return true;
    };
    if constexpr (Method == SearchMethod::libc) sn::byte_per_value(view, needle, emit);
    else sn::byte_sparse(view, needle, emit);
  }
  return result;
}

template<unsigned capacity> void search_capacity(unsigned repeats) {
  for (unsigned width : {4, 32, 256})
    for (bool waste : {false, true}) {
      Values oracle(capacity);
      std::vector<std::byte> body;
      std::vector<std::uint32_t> ends{0};
      std::vector<std::uint16_t> used;
      std::vector<std::uint64_t> tombs(capacity / 64);
      std::mt19937 rng(521);
      for (unsigned r = 0; r < capacity; ++r) {
        auto& v = oracle[r]; v.resize(r % 13 == 0 ? 0 : width);
        for (auto& b : v) b = std::byte(rng() % 14);
        if (r % 61 == 0 && !v.empty()) v.back() = std::byte{15};
        body.insert(body.end(), v.begin(), v.end()); used.push_back(v.size());
        if (waste) {
          body.insert(body.end(), width / 2, std::byte{7});
          if (r % 3 == 0) tombs[r / 64] |= 1ull << (r % 64);
        }
        ends.push_back(body.size());
      }
      const sn::Direct<std::uint32_t, true, true> view{ends, body, used, tombs};
      require(view.admit() == sn::Error::none, "search admission");
      for (unsigned needle : {14, 15, 7}) {
        const auto operations = [&]<SearchOp Op> {
          SearchResult<Op, capacity> expected{};
          for (unsigned r = 0; r < capacity; ++r) {
            if (waste && r % 3 == 0) continue;
            for (auto b : oracle[r]) if (b == std::byte(needle)) {
              if constexpr (Op == SearchOp::any) expected = true;
              if constexpr (Op == SearchOp::membership) expected[r / 64] |= 1ull << (r % 64);
              if constexpr (Op == SearchOp::count) ++expected;
            }
          }
          const auto method = [&]<SearchMethod Method>(const char* name) {
            SearchResult<Op, capacity> actual{};
            const auto operation = [&] { actual = search<Op, Method, capacity>(view, std::byte(needle)); escape(&actual); };
            operation(); require(actual == expected, "exact search result");
            for (unsigned rep = 0; rep < repeats; ++rep) {
              const auto sample = time_operation(operation);
              require(actual == expected, "post timing search result");
              const char* op = Op == SearchOp::any ? "any" : Op == SearchOp::membership ? "membership" : "count";
              std::printf("search,w%u-n%u-%s,%u,physical,%s,%s,%u,1,%u,%.3f,%zu,%zu\n",
                  width, needle, waste ? "waste" : "full", capacity, op, name, rep, sample.iterations,
                  sample.elapsed_ns / sample.iterations, ends.size() * 4 + used.size() * 2 + tombs.size() * 8,
                  body.size());
            }
          };
          method.template operator()<SearchMethod::libc>("libc");
          method.template operator()<SearchMethod::native_value>("native-value");
          method.template operator()<SearchMethod::sparse>("sparse");
          method.template operator()<SearchMethod::segmented>("segmented");
        };
        operations.template operator()<SearchOp::any>();
        operations.template operator()<SearchOp::membership>();
        operations.template operator()<SearchOp::count>();
      }
    }
}
void search_suite(unsigned repeats) {
  search_capacity<128>(repeats); search_capacity<256>(repeats); search_capacity<512>(repeats);
}

int main(int argc, char** argv) {
  const std::string suite = argc > 1 ? argv[1] : "all";
  const unsigned count = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 4096;
  const unsigned repeats = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 3;
  require(count >= 512 && count % 512 == 0 && count <= (1u << 20) && repeats > 0, "pilot arguments");
  std::puts("suite,dataset,capacity,order,operation,method,repetition,items,iterations,ns_per_item,metadata_bytes,payload_bytes");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "lane adoption");
  auto arena = take(oa::open_arena(2ull << 30)); auto* pool = take(oa::add_pool(*arena, lane));
  if (suite == "read" || suite == "all") read_suite(*pool, count, repeats);
  if (suite == "holders" || suite == "all") holder_suite(*pool, repeats);
  if (suite == "search" || suite == "all") search_suite(repeats);
  for (;;) {
    auto p = oa::close(*pool);
    if (p.kind() == orbital::Progress::Kind::done) break;
    require(p.kind() != orbital::Progress::Kind::failed, "pool close");
    orbital::worker::poll(lane, 128); std::this_thread::yield();
  }
  require(arena->charged_bytes == 0, "settled pilot body allocations");
  require(bool(oa::remove_pool(*pool)), "remove pool");
  arena.reset(); orbital::worker::relinquish(lane);
}
