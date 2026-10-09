// Actual requested metadata for a complete frozen closure, not wrapper sizes.
#include <orbital/object.h>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

using namespace orbital;
namespace o = orbital::object;
template<class T> T need(Result<T> r) { if (!r) std::abort(); return std::move(*r); }
void need(Result<void> r) { if (!r) std::abort(); }

int main() {
  worker::Lane lane;
  need(worker::adopt(lane));
  for (std::size_t count : {0u, 1u, 4u, 17u}) {
    auto store = need(o::Store::open(lane, 1 << 20));
    const auto setup = store.statistics().metadata_bytes;
    std::vector<std::shared_ptr<const std::uint64_t>> bodies;
    std::vector<o::PartSource> parts;
    const std::array<std::uint64_t, 1> metadata{55};
    for (std::size_t i = 0; i < count; ++i) {
      auto body = std::make_shared<const std::uint64_t>(100 + i);
      parts.push_back({{0, i}, std::as_bytes(std::span(body.get(), 1)),
          std::as_bytes(std::span(metadata)), 0, 8, body});
      bodies.push_back(std::move(body));
    }
    const auto description = std::as_bytes(std::span(metadata));
    auto rep = need(store.capture({{0, 1}, 1, {}, {}, description, description, {}}, parts));
    const auto captured = store.statistics().metadata_bytes;
    auto draft = need(store.edit());
    const auto edited = store.statistics().metadata_bytes;
    need(draft.put(rep));
    const auto changed = store.statistics().metadata_bytes;
    auto prepared = need(store.prepare(draft));
    const auto prepared_bytes = store.statistics().metadata_bytes;
    need(store.install(prepared));
    prepared.retire();
    rep = {};
    while (store.drain(4096)) {}
    const auto completed = store.statistics().metadata_bytes;
    std::printf("{\"parts\":%zu,\"setup\":%zu,\"captured\":%zu,\"edit\":%zu,"
        "\"staged\":%zu,\"accepted\":%zu,\"completed\":%zu,"
        "\"external_body_bytes\":%zu}\n", count, setup, captured,
        edited, changed, prepared_bytes, completed, count * sizeof(std::uint64_t));
  }
  worker::relinquish(lane);
}
