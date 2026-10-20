#include "mutation-fixture.h"
#include <chrono>
#include <cstdio>
#include <ikea/streampack/author/chain.h>
#include <string>

#if defined(__aarch64__) || defined(__AVX2__)
using chain = sp::chain<8>;
template <class A> struct context {
  fixture &data;
  A prototype;
  word result = 0;
};
// A complete group consumer uses every offset/U vector, then reads a value word
// selected by that digest. No decoded array or per-rank geometry lookup.
template <class A>
[[gnu::always_inline]] word finish_value(context<A> &ctx, unsigned id,
                                         sp::native::packet values,
                                         unsigned live) {
  auto sum = sp::native::splat(0);
  sp::native::each_chunk([&](auto i) __attribute__((always_inline)) {
    sum = sp::native::add(
        sum, sp::native::add(values.offsets(i), values.lengths(i)));
  });
  const unsigned digest = sp::native::last(sp::native::prefix(sum));
  const auto &v = ctx.data.views[id / 16];
  word payload;
  const unsigned rank = (digest >> 9) & 255u;
  std::memcpy(&payload, v.windows().payload.data() + rank * 32 + 8, 8);
  return mix(payload ^ digest ^ live);
}
template <class A>
chain::result decode(void *raw, std::size_t id, std::uint16_t active,
                     sp::native::packet, unsigned) {
  auto &ctx = *static_cast<context<A> *>(raw);
  const auto a = ctx.prototype.rebind(ctx.data.views[id / 16]);
  const auto group = sp::native::read16(a, id % 16);
  return {group.values, std::uint16_t(active & group.live), false};
}
template <class A>
void complete(void *raw, std::size_t id, std::uint16_t active,
              sp::native::packet values) {
  auto &ctx = *static_cast<context<A> *>(raw);
  ctx.result = finish_value(ctx, id, values, active);
}
template <unsigned Mode, unsigned Chains, class A>
[[gnu::noinline]] word consume(fixture &f, A prototype, unsigned rounds) {
  const std::array<chain::function, 1> stages{chain::stage<decode<A>>};
  const auto plan = *chain::prepare(stages, chain::completion<complete<A>>);
  auto contexts = [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<context<A>, Chains>{{{f, prototype, I}...}};
  }(std::make_index_sequence<Chains>{});
  std::array<word, Chains> state{};
  for (unsigned c = 0; c < Chains; ++c)
    state[c] = mix(c + 713);
  for (unsigned r = 0; r < rounds; ++r) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto one = [&](auto c) __attribute__((always_inline)) {
        const unsigned id = state[c] & (f.nodes * 16 - 1);
        auto &ctx = contexts[c];
        if constexpr (Mode == 0) {
          const auto a = prototype.rebind(f.views[id / 16]);
          const auto group = sp::native::read16(a, id % 16);
          state[c] = finish_value(ctx, id, group.values, group.live);
        } else if constexpr (Mode == 1) {
          plan.run(&ctx, id, 0xffff, {});
          state[c] = ctx.result;
        } else {
          // Independent semantic oracle for this workload's R32,
          // full groups and all-live values. Never a timed baseline.
          const unsigned used = f.format.has_used() ? 16 : 32;
          const unsigned digest = 8192 * (id % 16) + 3840 + 16 * used;
          const unsigned rank = (digest >> 9) & 255;
          word payload;
          std::memcpy(&payload,
                      f.payload.data() + std::size_t(id / 16) * 8192 +
                          rank * 32 + 8,
                      8);
          state[c] = mix(payload ^ digest ^ 0xffffu);
        }
      };
      (one(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<Chains>{});
  }
  word result = 0;
  for (auto x : state)
    result ^= x;
  return result;
}
volatile word observed;
template <unsigned Mode, unsigned Chains, class A>
void trial(fixture &f, A a, unsigned operations, unsigned rep, word expected) {
  observed = consume<Mode, Chains>(f, a, 4096 / Chains);
  const auto begin = std::chrono::steady_clock::now();
  const auto result = consume<Mode, Chains>(f, a, operations / Chains);
  const auto end = std::chrono::steady_clock::now();
  observed = result;
  assert(result == expected);
  std::printf(
      "%s,%u,%u,%u,%u,%s,%u,%u,%.6f,%zu,%zu,%zu,%llu\n",
      A::family == sp::geometry::direct ? "direct" : "width",
      f.format.schema().maximum, f.nodes, unsigned(f.format.has_used()),
      unsigned(f.format.schema().lengths),
      Mode == 0 ? "direct_native" : "native_chain", Chains, rep,
      double(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
                 .count()) /
          operations,
      std::size_t(f.nodes) * f.stride, f.payload.size(), sizeof(sp::layout),
      (unsigned long long)result);
}
int main(int argc, char **argv) {
  const unsigned nodes = argc > 1 ? std::stoul(argv[1]) : 1,
                 ops = argc > 2 ? std::stoul(argv[2]) : 131072,
                 reps = argc > 3 ? std::stoul(argv[3]) : 5;
  if (!std::has_single_bit(nodes) || ops < 4096 || !reps)
    return 2;
  std::puts("layout,maximum,nodes,used,placement,consumer,chains,rep,ns,"
            "metadata_bytes,payload_bytes,layout_bytes,checksum");
  for (auto family : {sp::geometry::direct, sp::geometry::width})
    for (unsigned max : {255u, 4095u})
      for (bool full : {false, true})
        for (auto placement :
             {sp::placement::grouped, sp::placement::separated}) {
          fixture f(
              nodes,
              {family, placement, std::uint16_t(max), 8192, {}, full, false});
          sp::author::dispatch(f.views[0], [&](auto a) {
            const auto one = consume<2, 1>(f, a, ops),
                       eight = consume<2, 8>(f, a, ops / 8);
            for (unsigned rep = 0; rep < reps; ++rep) {
              trial<0, 1>(f, a, ops, rep, one);
              trial<1, 1>(f, a, ops, rep, one);
              trial<0, 8>(f, a, ops, rep, eight);
              trial<1, 8>(f, a, ops, rep, eight);
            }
          });
        }
}
#else
int main() { return 2; }
#endif
