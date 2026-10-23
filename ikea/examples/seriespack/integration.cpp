#include <ikea/seriespack.h>
#include <cassert>
#include <memory>

namespace sp = ikea::seriespack;
namespace cp = sp::author;
using F = sp::layout<12, sp::geometry::striped>;
constexpr std::size_t count = 512;
struct buffer {
    alignas(64) std::array<ikea::byte, count * 12 / 8> bytes{};
};

// Teaching adapter: moving the lease excludes all readers until publication.
// A real owner supplies acquisition, versioning, durable effects and visibility.
struct owner {
    std::unique_ptr<buffer> readable = std::make_unique<buffer>();
    std::uint64_t published_sum = 0;
    std::size_t published_writes = 0;
    void publish(std::unique_ptr<buffer> lease, std::uint64_t delta,
                 std::span<const ikea::journal::entry> effects, const void* source) {
        assert(!readable);
        for (const auto& effect : effects) {
            assert(effect.source == source && effect.plane == 0);
            assert(effect.offset + effect.size <= lease->bytes.size());
            // Resolve source/plane to partition + offset while the view lives.
        }
        published_writes = effects.size();
        published_sum += delta;
        readable = std::move(lease); // one serialized visibility boundary
    }
};

enum class outcome { rotate, complete, cancelled_clean, needs_owner_resolution };
class mutation_work {
    std::unique_ptr<buffer> lease_;
    sp::view<F> destination_;
    using Prepared = sp::writer<decltype(cp::expression(destination_))>;
    Prepared prepared_;
    sp::erased_writer<std::uint16_t, sp::author::sum_change> operation_;
    std::array<std::uint16_t, count> input_{};
    std::array<ikea::journal::entry, count> records_{};
    ikea::journal effects_{records_};
    sp::author::sum_change summary_;
    std::size_t next_ = 0;

  public:
    explicit mutation_work(std::unique_ptr<buffer> acquired)
        : lease_(std::move(acquired)), destination_(*sp::view<F>::attach(
                                           count, {{{lease_->bytes, F::tile_bytes}, {}, {}}})),
          prepared_(*sp::bind_writer(destination_)),
          operation_(prepared_.erase<std::uint16_t, sp::author::sum_change>()) {
        for (std::size_t i = 0; i < count; ++i)
            input_[i] = i;
        assert(*prepared_.effect_capacity(0, count, sp::selection::all()) <= records_.size());
    }
    mutation_work(const mutation_work&) = delete;
    mutation_work& operator=(const mutation_work&) = delete;
    // Stable address: prepared_ borrows destination_, operation_ borrows prepared_.
    ~mutation_work() {
        assert(!lease_ && "owner must resolve and release this work before destruction");
    }
    outcome step(bool cancel_requested = false) {
        if (cancel_requested)
            return next_ ? outcome::needs_owner_resolution : outcome::cancelled_clean;
        if (next_ == count)
            return outcome::complete;
        constexpr std::size_t grain = 64;
        const auto result =
            operation_.replace(next_, std::span<const std::uint16_t>(input_).subspan(next_, grain),
                               effects_, sp::selection::all(), summary_);
        assert(result);
        next_ += grain;
        // Safe suspension frontier: no transient native carrier escapes the call.
        // Lease, input, progress, summary and effects all remain in this object.
        return next_ == count ? outcome::complete : outcome::rotate;
    }
    void publish(owner& target) {
        assert(next_ == count);
        target.publish(std::move(lease_), summary_.finish(), effects_.entries(), &destination_);
    }
    void cancel_clean(owner& target) {
        assert(!next_ && !target.readable);
        target.readable = std::move(lease_);
    }
};

int main() {
    owner storage;
    {
        mutation_work unused(std::move(storage.readable));
        assert(unused.step(true) == outcome::cancelled_clean);
        unused.cancel_clean(storage);
    }
    mutation_work work(std::move(storage.readable)); // successful acquisition
    assert(work.step() == outcome::rotate);
    assert(!storage.readable); // local mutation is not publication
    assert(work.step(true) == outcome::needs_owner_resolution);
    // The owner elects to finish the admitted operation after this cancellation.
    // Dropping the lease would neither undo these writes nor repair the summary.
    while (work.step() == outcome::rotate) {
    }
    work.publish(storage);
    assert(storage.readable && storage.published_sum == count * (count - 1) / 2);
    assert(storage.published_writes != 0);
}
