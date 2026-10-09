#include "composed_runtime.h"

namespace orbital_versioned_performance {
MappedShape::MappedShape(const Corpus& corpus) : corpus_(corpus) {
  const auto& image = corpus.images.at(0);
  const compact_descriptor::PartSource part{image.address.part, image.encoded,
      image.shape.parts[0].interpretation, 0, 1, {}};
  shape_ = need(compact_shape::prove_fixture(memory_, corpus.schema, data::data_program,
      image.shape.interpretation, std::span{&part, 1}));
  const auto admitted = shape_read::admit_tuple_shape(shape_, data::data_program, image.address.part);
  if (!admitted) {
    compact_shape::release(shape_); shape_ = nullptr; need(admitted);
  }
  proof_ = *admitted;
  const auto write_admitted = shape_write::TupleLaw::prepare(write_, shape_, corpus.schema.tuple,
      std::span(corpus.schema.writers).subspan<1, 3>(), data::data_program, image.address.part);
  if (!write_admitted) {
    proof_.reset(); write_.reset(); compact_shape::release(shape_); shape_ = nullptr;
    need(write_admitted);
  }
}
MappedShape::~MappedShape() {
  proof_.reset(); write_.reset();
  if (shape_) compact_shape::release(shape_);
  if (memory_.live) std::terminate();
}
namespace {
struct MappedBody {
  const MappedShape* shape;
  orbital::Bytes bytes;
  auto generic() const {
    const auto& corpus = shape->corpus();
    // Counts and physical law are uniform in this fixed-membership fixture.
    // Opaque identity was resolved by the protecting transaction, not here.
    const auto& image = corpus.images[0];
    return corpus.bind(0, bytes, image.shape.parts[0].interpretation);
  }
  auto get(orbital_bench::Key key) const { return generic().get(key); }
  auto scan(orbital_bench::Key key, std::span<orbital_bench::Row> output) const { return generic().scan(key, output); }
  orbital_bench::Outcome consume(const orbital_bench::Operation& op) const {
    using orbital_bench::Kind;
    if (op.kind == Kind::read || op.kind == Kind::chain) {
      if (op.kind == Kind::chain && op.count > 16) throw std::invalid_argument("mapped chain exceeds fixture bound");
      orbital_bench::Outcome output;
      need(shape_read::read_mapped_proven(shape->proof(), shape->corpus().images[0].shape.records, bytes,
          op.key, op.kind == Kind::chain ? op.count : 1, output));
      return output;
    }
    return read_operation(generic(), op);
  }
};
template<std::size_t Capacity> class MappedBodies {
  Lmdb::Read context_;
  const Corpus& corpus_;
  const MappedShape& shape_;
  RequestCache<orbital::Bytes, Capacity> cache_;
public:
  std::uint64_t bindings{};
  MappedBodies(Lmdb& store, const Corpus& corpus, const MappedShape& shape)
      : context_(store.read()), corpus_(corpus), shape_(shape) {}
  MappedBody body(std::size_t object) {
    const auto& image = corpus_.images.at(object);
    auto bytes = cache_.get(object, [&] {
      auto mapped = context_.part(image.address);
      if (!mapped) throw std::runtime_error("missing mapped Shape body");
      return *mapped;
    }, bindings);
    return {&shape_, bytes};
  }
};
}
Interval run_lmdb_shape_reads(Lmdb& store, const Corpus& corpus, const MappedShape& shape, bool latency,
    std::span<const orbital_bench::Operation> input, std::span<const std::size_t> objects,
    std::span<orbital_bench::Outcome> output, std::size_t batch, std::uint64_t& bindings) {
  const auto run = [&]<std::size_t Capacity>() {
    const auto acquire = [&] { return MappedBodies<Capacity>{store, corpus, shape}; };
    return latency ? opaque_read_trial<true>(acquire, input, objects, output, batch, bindings) :
                     opaque_read_trial<false>(acquire, input, objects, output, batch, bindings);
  };
  return batch == 1 ? run.template operator()<1>() : run.template operator()<64>();
}
void lmdb_composed_update(Lmdb& store, const MappedShape& mapped_shape, std::span<const orbital_bench::Operation> input,
    std::span<const std::size_t> objects, std::span<orbital_bench::Outcome> output,
    data::Costs& costs, write_batch::ProbeCosts& probe) {
  using namespace orbital_bench;
  const auto& corpus = mapped_shape.corpus();
  const auto write_proof = mapped_shape.write_proof();
  if (input.empty() || input.size() > 64 || input.size() != objects.size() || input.size() != output.size())
    throw std::invalid_argument("invalid composed LMDB publication batch");
  for (std::size_t i = 0; i < input.size(); ++i)
    if (objects[i] >= corpus.images.size() || input[i].kind != Kind::update || input[i].record.value > value_mask)
      throw std::invalid_argument("invalid composed LMDB fixed-row update");
  std::optional<Lmdb::Read> basis(store.read());
  auto write = store.edit();
  std::array<data::Update, 64> updates;
  for (std::size_t offered = 0; offered < objects.size(); ++offered) {
    const auto object = objects[offered];
    if (std::find(objects.begin(), objects.begin() + offered, object) != objects.begin() + offered) continue;
    const auto& image = corpus.images[object];
    const auto bytes = basis->part(image.address);
    if (!bytes) throw std::runtime_error("missing composed LMDB update basis");
    std::size_t used{};
    for (std::size_t i = 0; i < input.size(); ++i) if (objects[i] == object) {
      std::size_t position{};
      Record record;
      need(shape_write::lookup_mapped(write_proof, image.shape.records, *bytes,
          input[i].key, position, record));
      record.value = input[i].record.value; record.tag = input[i].record.tag;
      updates[used++] = {input[i].key, record};
    }
    auto target = write.reserve_part(image.address, bytes->size());
    std::memcpy(target.data(), bytes->data(), bytes->size());
    costs.copied_bytes += bytes->size(); ++costs.copied_spans;
    costs.requested_body_bytes += bytes->size(); ++costs.requested_parts;
    const data::WritePart part{image.address.part, target};
    need(shape_write::apply(write_proof, image.shape.records, std::span{&part, 1},
        std::span(updates).first(used), costs.kernel, probe));
  }
  basis.reset(); write.publish();
  for (auto& outcome : output) { outcome = {}; outcome.changes = 1; detail::mix(outcome.digest, 1); }
}
} // namespace orbital_versioned_performance
