// Isolate physical span, byte demand and stride from the output packet shape.
// The window competitor may read inter-row padding: this is an experiment with
// an explicitly owned allocation, not the ordinary view's access contract.
#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <cstring>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <vector>
using namespace ikea::tuplepack;
namespace {
using namespace native::native_detail;
[[gnu::always_inline]] inline std::uint64_t sum(native::packet v) {
  auto part = [](native::vector16 p) {
#if defined(__aarch64__)
    return std::uint64_t(vaddlvq_u8(p));
#else
    const auto s = _mm_sad_epu8(p, _mm_setzero_si128());
    return std::uint64_t(_mm_cvtsi128_si64(s)) +
           std::uint64_t(_mm_extract_epi64(s, 1));
#endif
  };
  return part(split<0>(v)) + part(split<1>(v)) + part(split<2>(v)) +
         part(split<3>(v));
}
template <unsigned Rows>
void run(benchmark::State &state, unsigned extent, unsigned draws,
         unsigned stride, bool spread, unsigned method, bool update) {
  constexpr unsigned B = 64 / Rows;
  std::vector<code> codes(extent);
  for (unsigned i = 0; i < extent; ++i)
    codes[i] = {byte(i), byte(i % 2), 7};
  const auto format = *layout::make(extent, codes);
  std::vector<byte> map(draws);
  for (unsigned i = 0; i < draws; ++i)
    map[i] = spread && draws > 1 ? i * (extent - 1) / (draws - 1) : i;
  // A runtime reversed map prevents identity-copy fixtures from deciding the
  // result.
  std::reverse(map.begin(), map.end());
  // Retain this experiment's fixed row slices explicitly; ordinary short maps
  // now occupy a packed prefix of the packet.
  auto packet_map = map;
  packet_map.resize(B, hole);
  const auto rp = *reader<64, Rows>::make(format, packet_map);
  const auto wp = *writer<64, Rows>::make(format, packet_map);
  const auto point_r = *reader<64>::make(format, map);
  const auto point_w = *writer<64>::make(format, map);
  // Use the ordinary prepared scalar kernel when the actual projection fits
  // eight bytes, including its single-code/word writer specializations. Do
  // not charge a small map for the unused capacity of a wider packet shape.
  std::optional<reader<8>> small_r;
  std::optional<writer<8>> small_w;
  if (draws <= 8) {
    small_r = *reader<8>::make(format, map);
    small_w = *writer<8>::make(format, map);
  }
  detail::shuffle_description wiring;
  const unsigned first = *std::min_element(map.begin(), map.end());
  const unsigned hull = *std::max_element(map.begin(), map.end()) - first + 1;
  const unsigned span = (Rows - 1) * stride + hull;
  if (method == 3 && span > 64) {
    state.SkipWithError("window exceeds one packet");
    return;
  }
  if (span <= 64)
    for (unsigned r = 0; r < Rows; ++r)
      for (unsigned i = 0; i < map.size(); ++i) {
        auto c = codes[map[i]];
        wiring.index[r * B + i] = r * stride + c.offset - first;
        wiring.shift[r * B + i] = -c.shift;
        wiring.mask[r * B + i] = 127;
      }
  auto route = detail::compile_shuffle(wiring, false);
  constexpr std::size_t count = 1024;
  std::vector<byte> data((count - 1) * stride + extent);
  for (std::size_t i = 0; i < data.size(); ++i)
    data[i] = byte(i * 71 + 13);
  std::array<byte, 64> mask_bytes;
  mask_bytes.fill(63);
  const auto mask = native::load_packet(mask_bytes.data());
  // Every retained competitor is checked against independent per-code bits
  // before timing; updates use identical destinations and preserve all
  // neighbors.
  std::array<byte, 64> expected{}, actual{};
  for (unsigned r = 0; r < Rows; ++r)
    for (unsigned i = 0; i < map.size(); ++i) {
      auto c = codes[map[i]];
      expected[r * B + i] = (data[r * stride + c.offset] >> c.shift) & 127;
    }
  auto address = [&](unsigned r) { return data.data() + r * stride; };
  auto got = method == 3
                 ? native::transform<false>(
                       native::load_unit(data.data() + first, span), route)
                 : native::read_body<Rows>(rp.controls(), address,
                                           ~0ull >> (64 - Rows),
                                           method == 2 ? 0 : stride);
  if (method == 1) {
    for (unsigned r = 0; r < Rows; ++r) {
      if (small_r) {
        auto v = small_r->get_unchecked(address(r));
        std::memcpy(actual.data() + r * B, &v, std::min(8u, B));
      } else {
        std::array<byte, 64> point{};
        native::store_packet(point.data(),
                             native::read_body(point_r.controls(), address(r)));
        std::copy_n(point.data(), B, actual.data() + r * B);
      }
    }
    got = native::load_packet(actual.data());
  }
  native::store_packet(actual.data(), got);
  if (actual != expected) {
    state.SkipWithError("independent projection mismatch");
    return;
  }
  if (update) {
    auto wanted = data;
    for (unsigned r = 0; r < Rows; ++r)
      for (unsigned i = 0; i < map.size(); ++i) {
        auto c = codes[map[i]];
        const auto pos = r * stride + c.offset;
        const auto bits = 127u << c.shift;
        wanted[pos] = byte((wanted[pos] & ~bits) |
                           ((expected[r * B + i] & 63u) << c.shift));
      }
    if (method == 1) {
      for (unsigned r = 0; r < Rows; ++r) {
        if (small_r)
          small_w->set_unchecked(address(r),
                                 small_r->get_unchecked(address(r)) &
                                     0x3f3f3f3f3f3f3f3full);
        else {
          auto v = native::read_body(point_r.controls(), address(r));
          native::write_body(point_w.controls(), address(r),
                             native::bit_and(v, mask));
        }
      }
    } else
      native::write_body<Rows>(wp.controls(), address,
                               native::bit_and(got, mask), ~0ull >> (64 - Rows),
                               stride);
    if (data != wanted) {
      state.SkipWithError("independent update/preservation mismatch");
      return;
    }
  }
  for (auto _ : state) {
    std::uint64_t result = 0;
    for (std::size_t row = 0; row < count; row += Rows) {
      auto at = [&](unsigned r) { return data.data() + (row + r) * stride; };
      if (method == 1) {
        for (unsigned r = 0; r < Rows; ++r) {
          if (small_r) {
            auto v = small_r->get_unchecked(at(r));
            if (update)
              small_w->set_unchecked(at(r), v & 0x3f3f3f3f3f3f3f3full);
            else if (draws == 1)
              result += v;
            else {
              v = (v & 0x00ff00ff00ff00ffull) +
                  ((v >> 8) & 0x00ff00ff00ff00ffull);
              v = (v & 0x0000ffff0000ffffull) +
                  ((v >> 16) & 0x0000ffff0000ffffull);
              result += (v & 0xffffffffull) + (v >> 32);
            }
          } else {
            auto v = native::read_body(point_r.controls(), at(r));
            if (update)
              native::write_body(point_w.controls(), at(r),
                                 native::bit_and(v, mask));
            else
              result += sum(v);
          }
        }
      } else {
        auto v = method == 3
                     ? native::transform<false>(
                           native::load_unit(at(0) + first, span), route)
                     : native::read_body<Rows>(rp.controls(), at,
                                               ~0ull >> (64 - Rows),
                                               method == 2 ? 0 : stride);
        if (update)
          native::write_body<Rows>(wp.controls(), at, native::bit_and(v, mask),
                                   ~0ull >> (64 - Rows), stride);
        else
          result += sum(v);
      }
    }
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * count);
  state.counters["items_per_iteration"] = count;
  state.counters["physical_bytes"] = extent;
  state.counters["drawn_bytes"] = draws;
  state.counters["hull_bytes"] = hull;
  state.counters["window_bytes"] = span;
  state.counters["stride"] = stride;
}
template <unsigned Rows> void cases() {
  constexpr unsigned B = 64 / Rows;
  for (unsigned extent : {1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u, 24u, 32u, 64u})
    for (unsigned draws : {1u, 2u, 4u, 8u, 16u}) {
      if (draws > std::min(B, extent))
        continue;
      std::vector<unsigned> strides{extent, extent + 1, 64};
      std::sort(strides.begin(), strides.end());
      strides.erase(std::unique(strides.begin(), strides.end()), strides.end());
      for (unsigned stride : strides)
        for (bool spread : {false, true}) {
          if (spread && (draws == 1 || draws == extent))
            continue;
          const unsigned hull = spread ? extent : draws;
          for (bool update : {false, true})
            for (unsigned method = 0; method < 4; ++method) {
              if (method == 3 && (Rows - 1) * stride + hull > 64)
                continue;
              // Forced gather differs from packet selection only at coalescible
              // placements.
              if (method == 2 &&
                  !(stride == draws && std::has_single_bit(draws)))
                continue;
              constexpr const char *names[] = {"packet", "point", "gather",
                                               "window"};
              const auto name = "cross/" + std::to_string(Rows) + "/unit" +
                                std::to_string(extent) + "/draw" +
                                std::to_string(draws) + "/stride" +
                                std::to_string(stride) +
                                (spread ? "/spread" : "/compact") +
                                (update ? "/update/" : "/sum/") + names[method];
              benchmark::RegisterBenchmark(name.c_str(), [=](auto &s) {
                run<Rows>(s, extent, draws, stride, spread, method, update);
              });
            }
        }
    }
}
} // namespace
int main(int argc, char **argv) {
  cases<2>();
  cases<4>();
  cases<8>();
  cases<16>();
  cases<64>();
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
}
