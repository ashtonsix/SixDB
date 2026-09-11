// Executable integration probe. These small Pool and Engine types are test
// adapters, not proposed replacements for Loom or Engine's still-open APIs.
#include <ikea2/seriespack/write.h>
#include "../support.h"
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace sp = ikea2::seriespack;
namespace demo {
using F = sp::format<23, sp::geometry::local, 8>;
constexpr std::size_t count = 128, capacity = 512;
constexpr std::uint16_t selected = 0xeeee;
struct alignas(64) buffer {
    std::array<std::uint8_t, capacity> bytes;
    std::uint64_t incarnation = 0;
};
using lease = std::shared_ptr<buffer>;
struct completion {
    std::uint64_t generation;
    lease bytes;
};

// Serialized test scheduler. An in-flight completion owns its lease until
// delivery, even when the original consumer has cancelled or changed generation.
class pool {
    std::array<buffer, 4> buffers_;
    std::array<bool, 4> used_{};
    unsigned limit_;
    struct pending {
        std::uint64_t generation;
        lease bytes;
    };
    std::vector<std::optional<pending>> pending_;

  public:
    unsigned acquisitions = 0, releases = 0, prefetch_hints = 0;
    explicit pool(unsigned limit) : limit_(limit) {}
    lease try_acquire() {
        for (unsigned i = 0; i < limit_; ++i)
            if (!used_[i]) {
                used_[i] = true;
                ++acquisitions;
                ++buffers_[i].incarnation;
                return lease(&buffers_[i], [this, i](buffer*) {
                    IKEA2_CHECK(used_[i]);
                    used_[i] = false;
                    ++releases;
                });
            }
        return {};
    }
    std::size_t request(std::uint64_t generation) {
        pending_.push_back(pending{generation, try_acquire()});
        return pending_.size() - 1;
    }
    std::optional<completion> deliver(std::size_t id) {
        IKEA2_CHECK(pending_[id]);
        auto& request = *pending_[id];
        if (!request.bytes)
            request.bytes = try_acquire();
        if (!request.bytes)
            return {};
        completion result{request.generation, std::move(request.bytes)};
        pending_[id].reset();
        return result;
    }
    void prefetch(const lease& bytes, std::size_t offset, std::size_t size) {
        IKEA2_CHECK(bytes && offset <= capacity && size <= capacity - offset);
        ++prefetch_hints;
        // A real driver can feed this identity/incarnation and access geometry
        // into Loom's model. A DRAM hint is not an asynchronous readiness event.
        __builtin_prefetch(bytes->bytes.data() + offset, 1, 1);
    }
    unsigned outstanding() const {
        return acquisitions - releases;
    }
};

auto writable(const lease& bytes) {
    // Payloads and head bytes interleave in one Loom-owned partition. Their
    // envelopes overlap; their occupied spans and ownership do not.
    auto v =
        sp::view<F, std::uint8_t>::attach(count, {{{{bytes->bytes.data(), capacity}, 32},
                                                   {{bytes->bytes.data() + 16, capacity - 16}, 32},
                                                   {}}});
    IKEA2_CHECK(v);
    return *v;
}
auto readable(const std::shared_ptr<const buffer>& bytes) {
    auto v = sp::view<F>::attach(count, {{{{bytes->bytes.data(), capacity}, 32},
                                          {{bytes->bytes.data() + 16, capacity - 16}, 32},
                                          {}}});
    IKEA2_CHECK(v);
    return *v;
}
struct version;
using snapshot = std::shared_ptr<const version>;
struct mutation_input {
    snapshot before;
    std::array<sp::byte_write, 64> physical;
    std::size_t writes;
};
struct version {
    std::shared_ptr<const buffer> bytes;
    std::uint64_t revision = 1, mapping = 7;
    std::uint64_t sum = 0;
    std::optional<std::uint64_t> upper;
    std::shared_ptr<const mutation_input> mutation{};
};
snapshot initial(pool& buffers) {
    auto data = buffers.try_acquire();
    IKEA2_CHECK(data);
    data->bytes.fill(0xa7);
    const auto view = writable(data);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sp::set_unchecked(view, i, i + 1);
        sum += i + 1;
    }
    return std::make_shared<version>(version{std::move(data), 1, 7, sum, count});
}
struct sealed_change {
    lease bytes;
    snapshot before;
    std::uint64_t expected_revision, mapping, sum;
    std::array<sp::byte_write, 64> physical;
    std::size_t writes;
    // Upper-bound repair is deferred. The bypass travels with the visible
    // version; merely queueing a repair would not make pruning sound.
    bool bypass_upper = true;
    sealed_change(sealed_change&&) = default;
    sealed_change& operator=(sealed_change&&) = default;
    sealed_change(const sealed_change&) = delete;
    sealed_change(lease b, snapshot baseline, std::uint64_t total,
                  const std::array<sp::byte_write, 64>& effects, std::size_t used)
        : bytes(std::move(b)), before(std::move(baseline)), expected_revision(before->revision),
          mapping(before->mapping), sum(total), physical(effects), writes(used) {}
};

enum class phase { waiting, ready, cancelled, sealed, submitted };
enum class progress { blocked, rotate, complete, cancelled };
class attempt {
    using binding =
        decltype(sp::bind_mutation(std::declval<const sp::view<F, std::uint8_t>&>()))::value_type;
    snapshot base_; // semantic version pin
    lease private_; // residency and private write ownership
    std::optional<sp::view<F, std::uint8_t>> destination_;
    std::optional<binding> operation_;
    std::array<std::uint64_t, count> input_; // actual state needed after a stop
    std::array<sp::composition::owner_write, 64> effects_;
    sp::sum_change summary_;
    std::size_t used_ = 0, next_ = 0;
    std::uint64_t generation_;
    phase phase_ = phase::waiting;
    void acquired(lease bytes) {
        private_ = std::move(bytes);
        private_->bytes = base_->bytes->bytes;
        destination_ = writable(private_);
        operation_.emplace(*sp::bind_mutation(*destination_));
        phase_ = phase::ready;
    }

  public:
    attempt(snapshot base, std::uint64_t generation)
        : base_(std::move(base)), generation_(generation) {
        for (std::size_t i = 0; i < count; ++i)
            input_[i] = (std::uint64_t{1} << 23) - 1 - i;
        // The command, all values, the full effect capacity and writable layout
        // are admitted before mutation. No fallible allocation remains in step.
        for (auto value : input_)
            IKEA2_CHECK(value < (std::uint64_t{1} << 23));
        static_assert(count / 16 * 4 <= 64);
    }
    attempt(const attempt&) = delete;
    attempt& operator=(const attempt&) = delete;
    phase state() const {
        return phase_;
    }
    std::size_t frontier() const {
        return next_;
    }
    bool acquire_now(pool& buffers) {
        IKEA2_CHECK(phase_ == phase::waiting);
        if (auto bytes = buffers.try_acquire()) {
            acquired(std::move(bytes));
            return true;
        }
        return false;
    }
    bool accept(completion delivered) {
        if (phase_ != phase::waiting || delivered.generation != generation_)
            return false;
        acquired(std::move(delivered.bytes));
        return true;
    }
    void change_generation(std::uint64_t generation) {
        IKEA2_CHECK(phase_ == phase::waiting);
        generation_ = generation;
    }
    progress step(pool& buffers, unsigned region_budget) {
        if (phase_ == phase::cancelled)
            return progress::cancelled;
        if (phase_ == phase::waiting)
            return progress::blocked;
        IKEA2_CHECK(phase_ == phase::ready);
        if (next_ == count)
            return progress::complete;
        buffers.prefetch(private_, next_ / 8 * 32,
                         std::min<std::size_t>(64, capacity - next_ / 8 * 32));
        // These native accumulators remain local throughout the ready work.
        // Retaining them in the frame happens only at an actual driver return.
        auto summary = summary_;
        sp::composition::write_journal journal{effects_, used_};
        static constexpr auto masks = [] {
            std::array<std::uint16_t, count / 16> words;
            words.fill(selected);
            return words;
        }();
        const auto length = std::min<std::size_t>(region_budget, count / 16) * 16;
        const auto work = std::min(length, count - next_);
        const auto operation = operation_->erase<std::uint64_t, sp::sum_change>();
        operation.replace_unchecked(next_, work, input_.data() + next_,
                                    sp::row_selection::regions(0, masks), summary, journal);
        next_ += work; // data, contribution and coverage share this frontier
        summary_ = summary;
        used_ = journal.used;
        return next_ == count ? progress::complete : progress::rotate;
    }
    bool cancel() {
        if (phase_ == phase::submitted)
            return false; // outcome is now Engine-owned
        if (phase_ == phase::sealed)
            return false; // ownership has already moved
        phase_ = phase::cancelled;
        operation_.reset();
        destination_.reset();
        private_.reset();
        return true;
    }
    std::optional<sealed_change> seal() {
        if (phase_ != phase::ready || next_ != count)
            return {};
        std::array<sp::byte_write, 64> physical;
        for (std::size_t i = 0; i < used_; ++i) {
            IKEA2_CHECK(effects_[i].source == &*destination_);
            physical[i] = effects_[i].bytes;
        }
        phase_ = phase::sealed;
        operation_.reset();
        destination_.reset();
        return sealed_change{std::move(private_), base_, base_->sum + summary_.finish(), physical,
                             used_};
    }
    void submitted() {
        IKEA2_CHECK(phase_ == phase::sealed);
        phase_ = phase::submitted;
    }
};

enum class outcome { pending, committed, conflict };
class engine {
    snapshot current_;
    struct publication {
        sealed_change change;
        bool cancellation_requested = false;
        explicit publication(sealed_change c) : change(std::move(c)) {}
    };
    std::vector<std::unique_ptr<publication>> pending_;

  public:
    explicit engine(snapshot root) : current_(std::move(root)) {}
    snapshot read() const {
        return current_;
    }
    std::size_t submit(sealed_change change) {
        // Submission takes sole ownership of the candidate and its effects.
        pending_.push_back(std::make_unique<publication>(std::move(change)));
        return pending_.size() - 1;
    }
    outcome cancel_submission(std::size_t id) {
        IKEA2_CHECK(pending_[id]);
        pending_[id]->cancellation_requested = true;
        return outcome::pending;
    }
    outcome finish(std::size_t id) {
        IKEA2_CHECK(pending_[id]);
        auto publication = std::move(pending_[id]);
        auto& change = publication->change;
        if (change.expected_revision != current_->revision || change.mapping != current_->mapping)
            return outcome::conflict;
        for (std::size_t i = 0; i < change.writes; ++i) {
            const auto& span = change.physical[i];
            const auto partition_offset = span.offset + (span.plane == 1 ? 16 : 0);
            IKEA2_CHECK(span.plane <= 1 && partition_offset + span.size <= capacity);
            for (std::size_t b = partition_offset; b < partition_offset + span.size; ++b)
                IKEA2_CHECK(b % 32 < 15 || (b % 32 >= 16 && b % 32 < 24));
        }
        IKEA2_CHECK(change.bypass_upper);
        // One serialized test publication makes data, exact sum and the upper
        // pruning bypass visible together. A real multi-participant Engine
        // protocol must establish that property at every affected level.
        // This private-copy example keeps its baseline for checking coverage.
        // Ikea does not require that strategy: Orbital's UFFD page COW can
        // preserve versions while the same writers mutate an active mapping.
        auto effects = std::make_shared<mutation_input>(
            mutation_input{std::move(change.before), change.physical, change.writes});
        current_ = std::make_shared<version>(version{std::move(change.bytes),
                                                     current_->revision + 1,
                                                     change.mapping,
                                                     change.sum,
                                                     {},
                                                     std::move(effects)});
        return outcome::committed;
    }
    bool may_match_at_least(std::uint64_t lower) const {
        return !current_->upper || lower <= *current_->upper;
    }
};

void check_visible(snapshot actual, bool updated) {
    const auto view = readable(actual->bytes);
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto expected =
            updated && (selected & (1u << (i % 16))) ? (std::uint64_t{1} << 23) - 1 - i : i + 1;
        IKEA2_CHECK(sp::get_unchecked(view, i) == expected);
        total += expected;
    }
    IKEA2_CHECK(actual->sum == total);
    if (updated) {
        IKEA2_CHECK(actual->mutation && actual->mutation->before->mapping == actual->mapping);
        const auto& effects = *actual->mutation;
        check_visible(effects.before, false);
        for (std::size_t b = 0; b < capacity; ++b)
            if (actual->bytes->bytes[b] != effects.before->bytes->bytes[b]) {
                bool covered = false;
                for (std::size_t n = 0; n < effects.writes; ++n) {
                    const auto& span = effects.physical[n];
                    const auto offset = span.offset + (span.plane == 1 ? 16 : 0);
                    covered |= b >= offset && b - offset < span.size;
                }
                IKEA2_CHECK(covered);
            }
    }
}
void ready_and_rotated() {
    pool buffers(4);
    engine db(initial(buffers));
    const auto old = db.read();
    attempt job(old, 1);
    IKEA2_CHECK(job.acquire_now(buffers));
    IKEA2_CHECK(!job.seal());
    IKEA2_CHECK(job.step(buffers, 2) == progress::rotate && job.frontier() == 32);
    check_visible(db.read(), false); // no partial data or contributions escape
    IKEA2_CHECK(job.step(buffers, 100) == progress::complete);
    auto sealed = job.seal();
    IKEA2_CHECK(sealed);
    IKEA2_CHECK(!job.seal());
    const auto ticket = db.submit(std::move(*sealed));
    job.submitted();
    IKEA2_CHECK(!job.cancel());
    IKEA2_CHECK(db.cancel_submission(ticket) == outcome::pending);
    IKEA2_CHECK(db.finish(ticket) == outcome::committed); // cancellation cannot claim rollback
    check_visible(old, false);
    check_visible(db.read(), true);
    IKEA2_CHECK(db.may_match_at_least(1000)); // old upper bound would wrongly prune
    IKEA2_CHECK(buffers.prefetch_hints == 2);
}
void cancellation_and_late_completion() {
    pool buffers(3);
    engine db(initial(buffers));
    attempt cancelled(db.read(), 11);
    IKEA2_CHECK(cancelled.acquire_now(buffers));
    IKEA2_CHECK(cancelled.step(buffers, 1) == progress::rotate);
    IKEA2_CHECK(cancelled.cancel());
    IKEA2_CHECK(!cancelled.seal());
    check_visible(db.read(), false);
    IKEA2_CHECK(buffers.outstanding() == 1);
    attempt waiting(db.read(), 21);
    const auto old_ticket = buffers.request(21); // backend owns a lease in flight
    IKEA2_CHECK(buffers.outstanding() == 2);
    waiting.change_generation(22);
    IKEA2_CHECK(!waiting.accept(std::move(*buffers.deliver(old_ticket))));
    IKEA2_CHECK(buffers.outstanding() == 1); // stale reply released, not resumed
    const auto ticket = buffers.request(22);
    IKEA2_CHECK(waiting.cancel());
    IKEA2_CHECK(buffers.outstanding() == 2); // cancellation did not release backend ownership
    IKEA2_CHECK(!waiting.accept(std::move(*buffers.deliver(ticket))));
    IKEA2_CHECK(buffers.outstanding() == 1);
    check_visible(db.read(), false);
}
void pressure_and_conflict() {
    pool buffers(3);
    engine db(initial(buffers));
    attempt first(db.read(), 1), second(db.read(), 2), waiting(db.read(), 3);
    IKEA2_CHECK(first.acquire_now(buffers));
    IKEA2_CHECK(second.acquire_now(buffers));
    IKEA2_CHECK(!waiting.acquire_now(buffers));
    const auto wait = buffers.request(3);
    IKEA2_CHECK(!buffers.deliver(wait));
    IKEA2_CHECK(waiting.step(buffers, 1) == progress::blocked);
    IKEA2_CHECK(first.step(buffers, 100) == progress::complete);
    IKEA2_CHECK(second.step(buffers, 100) == progress::complete);
    auto a = first.seal(), b = second.seal();
    IKEA2_CHECK(a && b);
    const auto ta = db.submit(std::move(*a)), tb = db.submit(std::move(*b));
    first.submitted();
    second.submitted();
    IKEA2_CHECK(db.finish(ta) == outcome::committed);
    IKEA2_CHECK(db.finish(tb) == outcome::conflict);
    IKEA2_CHECK(waiting.accept(std::move(*buffers.deliver(wait))));
    IKEA2_CHECK(waiting.cancel());
    check_visible(db.read(), true);
}

// A second adapter admits exclusive mutation of an active mapping. Orbital may
// protect its pages for MVCC; Ikea neither pins nor copies a container baseline.
// The exclusion here models caller-controlled visibility, not page preservation.
void in_place_with_retained_state() {
    pool buffers(1);
    auto active = buffers.try_acquire();
    IKEA2_CHECK(active);
    active->bytes.fill(0xa7);
    const auto destination = writable(active);
    std::array<std::uint64_t, count> expected;
    std::uint64_t published_sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        expected[i] = i + 1;
        published_sum += expected[i];
        sp::set_unchecked(destination, i, expected[i]);
    }
    const auto* address = active->bytes.data();
    struct retained {
        lease residency;
        sp::sum_change summary;
        std::array<sp::byte_write, 64> effects;
        std::size_t used = 0;
        bool publication_excluded = true, dirty = false;
        enum class cancellation { released, owner_resolution_required };
        cancellation cancel() {
            if (dirty)
                return cancellation::owner_resolution_required;
            residency.reset();
            publication_excluded = false;
            return cancellation::released;
        }
    } state{active, {}, {}};
    // Two calls stop at an ordinary driver boundary between them. They write
    // the same packed bytes, so the second delta must use the current values.
    for (unsigned pass = 0; pass < 2; ++pass) {
        IKEA2_CHECK(state.publication_excluded);
        std::array<std::uint64_t, 16> input;
        for (std::size_t j = 0; j < 16; ++j)
            input[j] = 5000 + pass * 100 + j;
        auto summary = state.summary;
        sp::write_journal journal{state.effects, state.used};
        const auto mask = std::uint16_t(pass ? 0xcccc : 0xeeee);
        sp::replace16_unchecked(destination, 16, input.data(), mask, summary, journal);
        state.summary = summary;
        state.used = journal.used;
        state.dirty = true;
        for (std::size_t j = 0; j < 16; ++j)
            if (mask & (1u << j))
                expected[16 + j] = input[j];
        // In-place cancellation does not gain rollback by releasing a lease.
        IKEA2_CHECK(state.cancel() == retained::cancellation::owner_resolution_required);
        IKEA2_CHECK(state.residency && state.publication_excluded);
    }
    published_sum += state.summary.finish(); // owner elects to publish the work
    state.publication_excluded = false;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        IKEA2_CHECK(sp::get_unchecked(destination, i) == expected[i]);
        total += expected[i];
    }
    IKEA2_CHECK(total == published_sum && state.used && active->bytes.data() == address);
    IKEA2_CHECK(buffers.acquisitions == 1); // no second lease or private candidate
}
} // namespace demo
int main() {
    demo::ready_and_rotated();
    demo::cancellation_and_late_completion();
    demo::pressure_and_conflict();
    demo::in_place_with_retained_state();
    std::cout << "Ikea2 integration: owned waits, rotation, cancellation, stale completion, "
                 "sealing, conflict, data/summary visibility and in-place mutation passed\n";
}
