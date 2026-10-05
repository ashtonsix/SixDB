#include "prototype.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sched.h>
#include <string_view>

using namespace views;
namespace {
template<class T> T get(Result<T> r) { if (!r) std::abort(); return std::move(*r); }
void done(Result<void> r) { if (!r) std::abort(); }
double ns(auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count(); }
void measure(std::size_t pages, Method method, bool retain) {
  constexpr int repeats = 60;
  const std::vector<std::size_t> first{0};
  const std::vector<Range> changed{{0, 64}};
  auto pool = retain ? std::make_shared<recycling::Pool>() : nullptr;
  Part part(Bytes(pages * page_size(), 19), 1, false, 0, pool);
  { auto warm = get(part.open(1, first, Method::copy)); }
  Bytes expected(64, 19);
  double acquire{}, capture{}, seal{}, total{};
  Counts work{};
  const auto before = pool ? pool->counts() : recycling::Counts{};
  for (int iteration = 0; iteration < repeats; ++iteration) {
    part.reset_counts();
    const auto a = std::chrono::steady_clock::now();
    auto draft = get(part.begin(method, first));
    auto native = get(draft.access(first));
    const auto b = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < expected.size(); ++i) {
      expected[i] = static_cast<std::uint8_t>((iteration * 37 + i * 11) % 251);
      native.writable()[i] = expected[i];
    }
    if (retain) native.release({recycling::Temperature::hot, ::sched_getcpu(), std::chrono::steady_clock::now()});
    else native.close();
    const auto c = std::chrono::steady_clock::now();
    auto delivered = get(draft.stage(changed)); delivered->complete = true;
    const auto d = std::chrono::steady_clock::now();
    done(draft.publish(static_cast<Epoch>(iteration + 2)));
    const auto e = std::chrono::steady_clock::now();
    auto latest = get(part.open(part.latest(), first));
    if (!std::equal(expected.begin(), expected.end(), latest.data())) std::abort();
    const auto counts = part.counts();
    if (counts.staged != 64 || counts.source_bytes != pages * page_size() ||
        counts.material_bytes != pages * page_size()) std::abort();
    work.copied += counts.copied; work.mapped += counts.mapped; work.staged += counts.staged;
    work.source_bytes += counts.source_bytes; work.material_bytes += counts.material_bytes;
    acquire += ns(a, b); capture += ns(c, d); seal += ns(d, e); total += ns(a, e);
  }
  const auto after = pool ? pool->counts() : recycling::Counts{};
  std::printf("%zu,%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
      pages * page_size(), name(method), retain, repeats, total / repeats,
      acquire / repeats, capture / repeats, seal / repeats, work.copied / repeats,
      work.mapped / repeats, work.staged / repeats, work.source_bytes / repeats,
      work.material_bytes / repeats, after.maps - before.maps, after.restored - before.restored);
}
}
int main(int argc, char** argv) {
  const bool reverse = argc > 1 && std::string_view(argv[1]) == "--reverse";
  std::puts("part_bytes,method,retain_homes,repeats,cycle_ns,acquire_ns,capture_ns,seal_install_ns,copy_bytes,mapped_bytes,stage_bytes,source_read_bytes,material_written_bytes,pool_new_maps,pool_restored");
  std::vector<Method> methods{Method::copy, Method::cow, Method::take};
  if (reverse) std::reverse(methods.begin(), methods.end());
  for (auto pages : {1u, 16u, 256u}) for (bool retain : {false, true})
    for (auto method : methods) measure(pages, method, retain);
}
