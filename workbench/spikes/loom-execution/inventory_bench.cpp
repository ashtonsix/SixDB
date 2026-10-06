#include "microtask.h"
#include "support.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace loom_spike {
namespace {
using microtask::ScalarRing;
using microtask::VectorRing;
using microtask::Vector;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

// An operation owns its qualified input and stable native continuation state.
// Worker inventories borrow these pointers only while physical entries exist.
// There is deliberately no owner generation/refcount/cancellation test in step.
struct Parent;
struct Job {
    Parent* parent = nullptr;
    std::uint64_t ordinal = 0, steps = 0, start = 0, result = 0, done = 0;
    bool accepted = false, abandoned = false;
};
struct Observer {
    std::uint64_t outer = 0, completed_outer_visits = 0, start_ns = 0;
    std::uint64_t first_outer = 0, first_ns = 0, preceding_visits = 0;
};
struct Parent {
    std::array<std::uint64_t, 64> words{};
    std::vector<Job> jobs;
    Observer* observer = nullptr;
    bool vector_family = false;
    Parent(std::size_t count, std::uint64_t steps, std::uint64_t wave, bool vectors,
           Observer* observation) : jobs(count), observer(observation), vector_family(vectors) {
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] = (i * 73 + wave * 17 + 19) ^ (i << 8);
        for (std::size_t i = 0; i < count; ++i)
            jobs[i] = {this, i, steps, (i * 7 + wave) & 63, 0, 0, false, false};
    }
};
struct Audit {
    struct Outcome {
        std::uint64_t expected = 0, result = 0, done = 0;
        bool accepted = false, abandoned = false, published_before_cancel = false;
    };
    std::vector<Outcome> jobs;
    bool captured = false;
};

Audit prepare_audit(const Parent& parent) {
    Audit audit;
    audit.jobs.resize(parent.jobs.size());
    for (const auto& job : parent.jobs) {
        std::uint64_t cursor = job.start, total = 0;
        std::uint64_t x0 = job.ordinal + 1, x1 = job.ordinal + 2;
        std::uint64_t y0 = job.ordinal + 3, y1 = job.ordinal + 4;
        // Independent scalar oracle for both native ABI families.
        for (std::uint64_t step = 0; step < job.steps; ++step) {
            const auto value = parent.words[cursor];
            total += value;
            cursor = (cursor + value) & 63;
            x0 += value;
            x1 += value ^ 0x5a5a;
            y0 ^= x0;
            y1 ^= x1;
        }
        audit.jobs[job.ordinal].expected = total +
            (parent.vector_family ? x0 + x1 + y0 + y1 : 0);
    }
    return audit;
}
void capture(const Parent& parent, Audit& audit) {
    require(!audit.captured, "an operation was captured twice");
    for (const auto& job : parent.jobs) {
        auto& outcome = audit.jobs[job.ordinal];
        outcome.result = job.result;
        outcome.done = job.done;
        outcome.accepted = job.accepted;
        outcome.abandoned = job.abandoned;
    }
    audit.captured = true;
}
void mark_abandoned(Parent& parent) {
    for (auto& job : parent.jobs)
        if (job.accepted && !job.done)
            job.abandoned = true;
}

LOOM_MICROTASK_CC void scalar_step(ScalarRing* ring, ScalarRing::Slot* slot,
                                  std::uint64_t cursor, std::uint64_t remaining,
                                  std::uint64_t total, std::uint64_t state) {
    auto& job = *reinterpret_cast<Job*>(state);
    const auto value = job.parent->words[cursor];
    total += value;
    cursor = (cursor + value) & 63;
    if (!--remaining) {
        job.result = total;
        ++job.done;
        ring->complete(*slot);
    }
    [[clang::musttail]] return ScalarRing::yield(ring, slot, cursor, remaining, total, state);
}
LOOM_MICROTASK_CC void vector_step(VectorRing* ring, VectorRing::Slot* slot,
                                  std::uint64_t cursor, std::uint64_t remaining,
                                  std::uint64_t total, std::uint64_t state,
                                  Vector x, Vector y) {
    auto& job = *reinterpret_cast<Job*>(state);
    const auto value = job.parent->words[cursor];
    total += value;
    cursor = (cursor + value) & 63;
    x += Vector{value, value ^ 0x5a5a};
    y ^= x;
    if (!--remaining) {
        job.result = total + x[0] + x[1] + y[0] + y[1];
        ++job.done;
        ring->complete(*slot);
    }
    [[clang::musttail]] return VectorRing::yield(ring, slot, cursor, remaining, total, state, x, y);
}
// Selected only for B's first job at enqueue. One clock sample per wave records
// its first actual service, including admission/outer waits. Subsequent visits
// go directly to the ordinary typed body: no observer test on the hot path.
LOOM_MICROTASK_CC void scalar_first(ScalarRing* ring, ScalarRing::Slot* slot,
                                   std::uint64_t cursor, std::uint64_t remaining,
                                   std::uint64_t total, std::uint64_t state) {
    auto& observation = *reinterpret_cast<Job*>(state)->parent->observer;
    observation.first_outer = observation.outer;
    observation.preceding_visits = observation.completed_outer_visits;
    observation.first_ns = now_ns() - observation.start_ns;
    slot->function = scalar_step;
    [[clang::musttail]] return scalar_step(ring, slot, cursor, remaining, total, state);
}
LOOM_MICROTASK_CC void vector_first(VectorRing* ring, VectorRing::Slot* slot,
                                   std::uint64_t cursor, std::uint64_t remaining,
                                   std::uint64_t total, std::uint64_t state, Vector x, Vector y) {
    auto& observation = *reinterpret_cast<Job*>(state)->parent->observer;
    observation.first_outer = observation.outer;
    observation.preceding_visits = observation.completed_outer_visits;
    observation.first_ns = now_ns() - observation.start_ns;
    slot->function = vector_step;
    [[clang::musttail]] return vector_step(ring, slot, cursor, remaining, total, state, x, y);
}

struct Configuration {
    std::string mode = "two-parent", variant = "shared", admission = "fifo", cancel = "selective";
    std::uint64_t slots = 8, overflow = 8, credits = 16, visits = 8, waves = 1;
    std::uint64_t jobs_a = 32, jobs_b = 8, steps_a = 64, steps_b = 2, cancel_after = 0;
};

struct Worker {
    Configuration config;
    std::unique_ptr<ScalarRing> shared, a, b;
    std::unique_ptr<VectorRing> vb;
    std::uint64_t credit_refused = 0;
    explicit Worker(Configuration configuration) : config(std::move(configuration)) {
        const auto binding = prefetch::Binding{prefetch::Class::none, 4096};
        if (config.variant == "shared")
            shared = std::make_unique<ScalarRing>(config.slots, config.overflow, binding);
        else {
            a = std::make_unique<ScalarRing>(config.slots, config.overflow, binding);
            if (config.variant == "parent-banks")
                b = std::make_unique<ScalarRing>(config.slots, config.overflow, binding);
            else
                vb = std::make_unique<VectorRing>(config.slots, config.overflow, binding);
        }
    }
    std::size_t pending() const {
        return shared ? shared->pending() : a->pending() + (b ? b->pending() : vb->pending());
    }
    std::uint64_t storage() const {
        return shared ? shared->storage_bytes() : a->storage_bytes() +
                                                     (b ? b->storage_bytes() : vb->storage_bytes());
    }
    microtask::Counters counters() const {
        auto total = shared ? shared->counters() : a->counters();
        if (!shared) {
            const auto& other = b ? b->counters() : vb->counters();
            total.accepted += other.accepted; total.refused += other.refused;
            total.completed += other.completed; total.abandoned += other.abandoned;
            total.visits += other.visits; total.promoted += other.promoted;
            total.budget_stops += other.budget_stops; total.clock_samples += other.clock_samples;
            total.high_ring += other.high_ring; total.high_overflow += other.high_overflow;
        }
        return total;
    }
    bool push(Job& job, bool is_b, bool b_waiting) {
        auto limit = config.credits;
        if (!is_b && b_waiting && config.admission == "reserve-b")
            --limit;
        if (pending() >= limit) {
            ++credit_refused;
            return false;
        }
        if (is_b && vb) {
            VectorRing::Arguments arguments{job.start, job.steps, 0,
                                            reinterpret_cast<std::uint64_t>(&job),
                                            {Vector{job.ordinal + 1, job.ordinal + 2},
                                             Vector{job.ordinal + 3, job.ordinal + 4}}};
            return vb->push(job.ordinal ? vector_step : vector_first, arguments, {});
        }
        ScalarRing::Arguments arguments{job.start, job.steps, 0,
                                       reinterpret_cast<std::uint64_t>(&job), {}};
        auto* ring = shared ? shared.get() : is_b ? b.get() : a.get();
        return ring->push(is_b && !job.ordinal ? scalar_first : scalar_step, arguments, {});
    }
    std::uint64_t spin(bool is_b) {
        const auto budget = microtask::SpinBudget{config.visits, 0, 16};
        if (shared)
            return shared->spin(budget);
        if (is_b && vb)
            return vb->spin(budget);
        return (is_b ? b : a)->spin(budget);
    }
    std::size_t cancel_a(Parent* owner) {
        if (config.cancel == "drain")
            return 0;
        if (shared) {
            if (config.cancel == "discard")
                return shared->discard();
            return shared->release_if([owner](const ScalarRing::Arguments& args) noexcept {
                return reinterpret_cast<const Job*>(args.d)->parent == owner;
            });
        }
        return a->discard();
    }
    void discard_remaining() {
        if (shared) shared->discard();
        else {
            a->discard();
            if (b) b->discard(); else vb->discard();
        }
    }
};

struct Summary {
    std::uint64_t elapsed = 0, jobs = 0, physical_done = 0, abandoned = 0;
    std::uint64_t logical_done_a = 0, logical_done_b = 0, suppressed_a = 0, collateral_b = 0;
    std::uint64_t outer = 0, empty_outer = 0, admission_wait_a = 0, admission_wait_b = 0;
    std::uint64_t first_b_outer_sum = 0, first_b_outer_max = 0, first_b_ns_sum = 0;
    std::uint64_t first_b_preceding_visits = 0, first_b_missing = 0, released = 0;
    std::uint64_t owner_early_destroy = 0, owner_destroy_with_b_resident = 0;
    std::uint64_t qualified_bytes = 0, native_state_bytes = 0;
};
void verify_audit(const Audit& audit, bool is_b, bool cancelled, Summary& summary) {
    require(audit.captured, "owner outcome not retained before destruction");
    for (const auto& outcome : audit.jobs) {
        require(outcome.done <= 1, "operation contribution completed more than once");
        require(!outcome.done || outcome.accepted, "unaccepted operation completed");
        require(!outcome.abandoned || (outcome.accepted && !outcome.done), "bad abandonment");
        if (outcome.done) {
            require(outcome.result == outcome.expected, "native result differs from independent oracle");
            ++summary.physical_done;
            if (!is_b && cancelled && !outcome.published_before_cancel) ++summary.suppressed_a;
            else if (is_b) ++summary.logical_done_b;
            else ++summary.logical_done_a;
        } else if (outcome.accepted) {
            require(outcome.abandoned, "accepted operation still borrows destroyed owner");
            ++summary.abandoned;
            if (is_b) ++summary.collateral_b;
        } else {
            require(!is_b && cancelled, "uncancelled operation failed admission permanently");
        }
    }
}

Summary exercise(Worker& worker) {
    Summary summary;
    const auto& config = worker.config;
    for (std::uint64_t wave = 0; wave < config.waves; ++wave) {
        require(!worker.pending(), "worker inventory did not drain between parents");
        Observer observation;
        auto a = std::make_unique<Parent>(config.jobs_a, config.steps_a, wave, false, nullptr);
        auto b = std::make_unique<Parent>(config.jobs_b, config.steps_b, wave,
                                        config.variant == "family-banks", &observation);
        struct BorrowGuard {
            Worker& worker;
            ~BorrowGuard() { if (worker.pending()) worker.discard_remaining(); }
        } borrow_guard{worker};
        auto audit_a = prepare_audit(*a), audit_b = prepare_audit(*b);
        summary.qualified_bytes = 2 * sizeof(a->words);
        summary.native_state_bytes = (a->jobs.capacity() + b->jobs.capacity()) * sizeof(Job);
        std::size_t next_a = 0, next_b = 0;
        bool cancelled = false;
        observation.start_ns = now_ns();
        // Every outer visit admits for one parent, then services its bank. The
        // shared bank can service either parent's resident frames on that visit.
        while (worker.pending() || (!cancelled && next_a < config.jobs_a) || next_b < config.jobs_b) {
            const bool is_b = (observation.outer & 1) != 0;
            ++observation.outer;
            auto* parent = is_b ? b.get() : a.get();
            auto& next = is_b ? next_b : next_a;
            if ((is_b || !cancelled) && parent) {
                while (next < parent->jobs.size()) {
                    auto& job = parent->jobs[next];
                    if (!worker.push(job, is_b, next_b < config.jobs_b)) {
                        if (is_b) ++summary.admission_wait_b;
                        else ++summary.admission_wait_a;
                        break;
                    }
                    job.accepted = true;
                    ++next;
                }
            }
            const auto visits = worker.spin(is_b);
            observation.completed_outer_visits += visits;
            if (!visits) ++summary.empty_outer;
            if (!cancelled && config.cancel_after && observation.outer >= config.cancel_after) {
                cancelled = true;
                for (const auto& job : a->jobs)
                    audit_a.jobs[job.ordinal].published_before_cancel = job.done != 0;
                const auto removed = worker.cancel_a(a.get());
                summary.released += removed;
                if (config.cancel != "drain") {
                    mark_abandoned(*a);
                    if (config.variant == "shared" && config.cancel == "discard")
                        mark_abandoned(*b);
                    capture(*a, audit_a);
                    // A's words and callback state really disappear while B
                    // can still be resident. ASan checks the continuing borrow.
                    if (worker.pending()) ++summary.owner_destroy_with_b_resident;
                    a.reset();
                    ++summary.owner_early_destroy;
                }
            }
        }
        summary.elapsed += now_ns() - observation.start_ns;
        summary.outer += observation.outer;
        if (a) capture(*a, audit_a);
        capture(*b, audit_b);
        verify_audit(audit_a, false, cancelled, summary);
        verify_audit(audit_b, true, cancelled, summary);
        summary.jobs += config.jobs_a + config.jobs_b;
        if (observation.first_outer) {
            summary.first_b_outer_sum += observation.first_outer;
            summary.first_b_outer_max = std::max(summary.first_b_outer_max, observation.first_outer);
            summary.first_b_ns_sum += observation.first_ns;
            summary.first_b_preceding_visits += observation.preceding_visits;
        } else ++summary.first_b_missing;
    }
    const auto counters = worker.counters();
    require(counters.accepted == counters.completed + counters.abandoned,
            "accepted inventory was neither completed nor abandoned");
    require(counters.completed == summary.physical_done && counters.abandoned == summary.abandoned,
            "physical counters disagree with independent job outcomes");
    return summary;
}

// Fixed storage independent cleanup probes exercise both native-state families,
// wrapped overflow FIFO, exact cancellation, and rejected active cleanup.
struct Probe {
    unsigned owner = 0, id = 0, done = 0;
    std::uint64_t result = 0;
    std::array<unsigned, 64>* trace = nullptr;
    unsigned* length = nullptr;
    ScalarRing* ring = nullptr;
    bool rejected = false;
};
LOOM_MICROTASK_CC void probe_step(ScalarRing* ring, ScalarRing::Slot* slot,
                                 std::uint64_t remaining, std::uint64_t total,
                                 std::uint64_t marker, std::uint64_t state) {
    auto& probe = *reinterpret_cast<Probe*>(state);
    if (probe.trace) (*probe.trace)[(*probe.length)++] = probe.id;
    total += marker;
    if (!--remaining) {
        probe.result = total;
        ++probe.done;
        ring->complete(*slot);
    }
    [[clang::musttail]] return ScalarRing::yield(ring, slot, remaining, total, marker, state);
}
LOOM_MICROTASK_CC void active_cleanup(ScalarRing* ring, ScalarRing::Slot* slot,
                                     std::uint64_t a, std::uint64_t b,
                                     std::uint64_t c, std::uint64_t state) {
    auto& probe = *reinterpret_cast<Probe*>(state);
    try {
        ring->release_if([](const ScalarRing::Arguments&) noexcept { return true; });
    } catch (const std::logic_error&) { probe.rejected = true; }
    ring->complete(*slot);
    [[clang::musttail]] return ScalarRing::yield(ring, slot, a, b, c, state);
}
void cleanup_checks() {
    ScalarRing ring(2, 4, prefetch::Binding{});
    std::array<unsigned, 64> trace{};
    unsigned length = 0;
    std::array<Probe, 7> probes{};
    const unsigned owners[] = {0, 1, 1, 0, 1, 0, 1};
    for (unsigned i = 0; i < probes.size(); ++i)
        probes[i] = {owners[i], i, 0, 0, &trace, &length, &ring, false};
    const auto push = [&](unsigned id, std::uint64_t steps) {
        return ring.push(probe_step, {steps, 0, id + 1, reinterpret_cast<std::uint64_t>(&probes[id]), {}}, {});
    };
    require(push(0, 10) && push(1, 1) && push(2, 3) && push(3, 10) && push(4, 1) && push(5, 10),
            "cleanup fixture failed initial admission");
    require(!push(6, 1), "full inventory did not refuse");
    require(ring.spin({4, 0, 16}) == 4 && probes[1].done == 1, "wrapped fixture did not progress");
    require(push(6, 1), "wrapped overflow could not reuse released frame");
    const auto pending = ring.pending();
    require(ring.release_if([](const ScalarRing::Arguments&) noexcept { return false; }) == 0 &&
                ring.pending() == pending, "no-match changed live inventory");
    unsigned nested_rejections = 0;
    require(ring.release_if([&](const ScalarRing::Arguments&) noexcept {
                try { ring.spin({1, 0, 16}); } catch (const std::logic_error&) { ++nested_rejections; }
                try { ring.discard(); } catch (const std::logic_error&) { ++nested_rejections; }
                try { ring.release_if([](const ScalarRing::Arguments&) noexcept { return true; }); }
                catch (const std::logic_error&) { ++nested_rejections; }
                return false;
            }) == 0 && nested_rejections == pending * 3,
            "predicate reentry did not reject progress/cleanup");
    require(ring.release_if([](const ScalarRing::Arguments& args) noexcept {
                return reinterpret_cast<const Probe*>(args.d)->owner == 0;
            }) == 3 && ring.pending() == 3, "selective active/wrapped overflow removal mismatch");
    while (ring.pending()) ring.spin({1, 0, 16});
    require(!probes[0].done && !probes[3].done && !probes[5].done, "cancelled probe executed");
    require(probes[2].done == 1 && probes[2].result == 9 && probes[4].result == 5 &&
                probes[6].result == 7, "surviving native arguments changed");
    const auto first4 = std::find(trace.begin(), trace.begin() + length, 4);
    const auto first6 = std::find(trace.begin(), trace.begin() + length, 6);
    require(first4 < first6, "surviving overflow FIFO order changed");
    require(ring.counters().accepted == 7 && ring.counters().refused == 1 &&
                ring.counters().completed == 4 && ring.counters().abandoned == 3,
            "cleanup accounting mismatch");
    require(ring.release_if([](const ScalarRing::Arguments&) noexcept { return true; }) == 0,
            "empty cleanup removed nonexistent entry");
    for (unsigned i = 0; i < 6; ++i) require(push(i, 2), "full inventory reuse failed");
    require(ring.release_if([](const ScalarRing::Arguments&) noexcept { return true; }) == 6 &&
                !ring.pending(), "all-match did not restore inventory");
    require(ring.push(active_cleanup, {0, 0, 0, reinterpret_cast<std::uint64_t>(&probes[0]), {}}, {}),
            "underfilled inventory reuse failed");
    ring.spin({1, 0, 16});
    require(probes[0].rejected && !ring.pending(), "active cleanup was not rejected");
    require(ring.counters().accepted == ring.counters().completed + ring.counters().abandoned,
            "reuse accounting mismatch");
    alignas(64) std::array<std::uint64_t, 16> qualified{};
    const auto before = ring.counters();
    for (unsigned i = 0; i < 3; ++i) {
        const auto kind = i == 0 ? prefetch::Class::l1_1 : i == 1 ? prefetch::Class::none :
                                                                         prefetch::Class::l2_2;
        require(ring.push(probe_step,
                          {1, 0, i + 1, reinterpret_cast<std::uint64_t>(&probes[i]), {}},
                          {qualified.data(), sizeof(qualified)}, kind), "explicit-class admission failed");
    }
    while (ring.pending()) ring.spin({1, 0, 16});
    require(ring.counters().completed == before.completed + 3 &&
                ring.counters().promoted == before.promoted + 1,
            "binding-none explicit-class override/overflow promotion failed");
}

void vector_cleanup_checks() {
    VectorRing ring(2, 4, prefetch::Binding{});
    auto a = std::make_unique<Parent>(3, 8, 5, true, nullptr);
    auto b = std::make_unique<Parent>(3, 3, 9, true, nullptr);
    auto aa = prepare_audit(*a), ab = prepare_audit(*b);
    for (std::size_t i = 0; i < 3; ++i) {
        for (auto* owner : {a.get(), b.get()}) {
            auto& job = owner->jobs[i];
            VectorRing::Arguments args{job.start, job.steps, 0, reinterpret_cast<std::uint64_t>(&job),
                                      {Vector{job.ordinal + 1, job.ordinal + 2},
                                       Vector{job.ordinal + 3, job.ordinal + 4}}};
            require(ring.push(vector_step, args, {}), "vector cleanup admission failed");
            job.accepted = true;
        }
    }
    ring.spin({3, 0, 16});
    require(ring.release_if([owner = a.get()](const VectorRing::Arguments& args) noexcept {
                return reinterpret_cast<const Job*>(args.d)->parent == owner;
            }) == 3 && ring.pending() == 3, "vector owner selection mismatch");
    mark_abandoned(*a); capture(*a, aa); a.reset();
    while (ring.pending()) ring.spin({1, 0, 16});
    capture(*b, ab);
    Summary summary;
    verify_audit(aa, false, true, summary); verify_audit(ab, true, true, summary);
    require(summary.logical_done_b == 3 && summary.abandoned == 3 &&
                ring.counters().accepted == ring.counters().completed + ring.counters().abandoned,
            "vector survivors did not preserve native scalar/vector state");
}

Summary cleanup_timing(Worker& worker) {
    require(worker.config.variant == "shared", "cleanup uses the shared typed inventory");
    require(worker.config.jobs_a + worker.config.jobs_b <= worker.config.slots + worker.config.overflow,
            "cleanup jobs must fit inventory");
    require(worker.config.jobs_a + worker.config.jobs_b <= worker.config.credits,
            "cleanup jobs must fit aggregate credits");
    Summary summary;
    for (std::uint64_t wave = 0; wave < worker.config.waves; ++wave) {
        Observer observer;
        auto a = std::make_unique<Parent>(worker.config.jobs_a, worker.config.steps_a, wave, false, nullptr);
        auto b = std::make_unique<Parent>(worker.config.jobs_b, worker.config.steps_b, wave, false, &observer);
        struct BorrowGuard {
            Worker& worker;
            ~BorrowGuard() { if (worker.pending()) worker.discard_remaining(); }
        } borrow_guard{worker};
        auto aa = prepare_audit(*a), ab = prepare_audit(*b);
        // Interleave cold admissions so both owners can occupy homes and FIFO.
        for (std::size_t i = 0; i < std::max(a->jobs.size(), b->jobs.size()); ++i) {
            if (i < a->jobs.size()) {
                auto& job = a->jobs[i];
                require(worker.push(job, false, false), "cleanup A admission failed"); job.accepted = true;
            }
            if (i < b->jobs.size()) {
                auto& job = b->jobs[i];
                require(worker.push(job, true, false), "cleanup B admission failed"); job.accepted = true;
            }
        }
        const auto start = now_ns();
        const auto removed = worker.shared->release_if([owner = a.get()](const ScalarRing::Arguments& args) noexcept {
            return reinterpret_cast<const Job*>(args.d)->parent == owner;
        });
        summary.elapsed += now_ns() - start;
        summary.released += removed;
        require(removed == worker.config.jobs_a, "timed selective removal mismatch");
        mark_abandoned(*a); capture(*a, aa);
        if (worker.pending()) ++summary.owner_destroy_with_b_resident;
        a.reset(); ++summary.owner_early_destroy;
        observer.start_ns = now_ns();
        while (worker.pending()) { ++observer.outer; worker.spin(true); }
        capture(*b, ab);
        verify_audit(aa, false, true, summary); verify_audit(ab, true, true, summary);
        summary.jobs += worker.config.jobs_a + worker.config.jobs_b;
        summary.qualified_bytes = 2 * 64 * sizeof(std::uint64_t);
        summary.native_state_bytes = (worker.config.jobs_a + worker.config.jobs_b) * sizeof(Job);
    }
    const auto c = worker.counters();
    require(c.accepted == c.completed + c.abandoned && c.completed == summary.physical_done &&
                c.abandoned == summary.abandoned, "timed cleanup accounting mismatch");
    return summary;
}

void emit_result(const Configuration& config, const Worker& worker, const Summary& summary,
                 std::uint64_t repeat) {
    const auto c = worker.counters();
    Record record;
    record.family = "inventory"; record.variant = config.variant;
    record.unit = config.mode == "cleanup" ? "released frame" : "accepted operation";
    record.repeat = repeat; record.elapsed_ns = summary.elapsed;
    record.items = config.mode == "cleanup" ? summary.released : c.accepted;
    record.verified = true;
    record.parameters = {{"case", config.mode}, {"admission", config.admission},
        {"cancel_mode", config.cancel}, {"cancel_a_after", std::to_string(config.cancel_after)},
        {"slots_per_bank", std::to_string(config.slots)}, {"overflow_per_bank", std::to_string(config.overflow)},
        {"aggregate_credits", std::to_string(config.credits)}, {"visits_per_outer", std::to_string(config.visits)},
        {"waves", std::to_string(config.waves)}, {"jobs_a", std::to_string(config.jobs_a)},
        {"jobs_b", std::to_string(config.jobs_b)}, {"steps_a", std::to_string(config.steps_a)},
        {"steps_b", std::to_string(config.steps_b)}, {"hint_class", "none"},
        {"calling_convention", "preserve_none"}, {"qualified_input", "two warm 512-byte arrays"},
        {"timed_scope", config.mode == "cleanup" ? "selective boundary scan and promotion only" :
            "admission, outer service, native work, retirement, cancellation and early A destruction"},
        {"first_b_observer", config.mode == "cleanup" ? "untimed survivor drain; latency not recorded" :
                                         "one clock sample on first-job initial callback per wave"},
        {"owner_lifetime", "parent input/state held through final actual slot use"},
        {"inventory_lifetime", "one worker inventory reused across all parent waves"}};
    record.parameters["logical_policy"] = "parent fixture publishes completed contributions before cancellation, suppresses later A completions";
    record.counters = {{"accepted", c.accepted}, {"refused", c.refused + worker.credit_refused},
        {"physical_refused", c.refused}, {"credit_refused", worker.credit_refused},
        {"completed", c.completed}, {"abandoned", c.abandoned}, {"visits", c.visits},
        {"promoted", c.promoted}, {"budget_stops", c.budget_stops}, {"clock_samples", c.clock_samples},
        {"scheduler_bytes", worker.storage()}, {"qualified_bytes", summary.qualified_bytes},
        {"native_state_bytes", summary.native_state_bytes}, {"logical_done_a", summary.logical_done_a},
        {"logical_done_b", summary.logical_done_b}, {"logical_suppressed_a", summary.suppressed_a},
        {"collateral_abandoned_b", summary.collateral_b}, {"outer_visits", summary.outer},
        {"empty_outer_visits", summary.empty_outer}, {"admission_waits_a", summary.admission_wait_a},
        {"admission_waits_b", summary.admission_wait_b}, {"first_b_outer_sum", summary.first_b_outer_sum},
        {"first_b_outer_max", summary.first_b_outer_max}, {"first_b_delay_ns_sum", summary.first_b_ns_sum},
        {"first_b_preceding_outer_native_visits", summary.first_b_preceding_visits},
        {"first_b_never_served", summary.first_b_missing}, {"released", summary.released},
        {"owner_early_destroy", summary.owner_early_destroy},
        {"owner_destroy_with_b_resident", summary.owner_destroy_with_b_resident},
        {"planned_operations", summary.jobs}, {"boundary_timer_reads", 2 * config.waves}};
    if (config.mode == "cleanup") {
        record.counters["scan_homes"] = config.slots * config.waves;
        record.counters["scan_live_arguments"] = (config.jobs_a + config.jobs_b) * config.waves;
    }
    emit(record);
}

void checks() {
    cleanup_checks();
    vector_cleanup_checks();
    for (const auto* variant : {"shared", "parent-banks", "family-banks"}) {
        for (const auto* admission : {"fifo", "reserve-b"}) {
            for (const auto* cancel : {"selective", "discard", "drain"}) {
                Configuration config;
                config.variant = variant; config.admission = admission; config.cancel = cancel;
                config.slots = 2; config.overflow = 3; config.credits = 5; config.visits = 2;
                config.jobs_a = 11; config.jobs_b = 7; config.steps_a = 17; config.steps_b = 2;
                config.cancel_after = 4; config.waves = 3;
                Worker worker(config);
                const auto result = exercise(worker);
                require(!worker.pending(), "two-parent check did not drain");
                if (config.variant != "shared" || config.cancel != "discard")
                    require(result.logical_done_b == config.jobs_b * config.waves && !result.collateral_b,
                            "A cancellation changed surviving B outcomes");
                if (config.variant == "shared" && config.cancel == "discard" &&
                    config.admission == "reserve-b")
                    require(result.collateral_b, "whole shared discard did not expose resident B collateral");
            }
        }
        for (const auto steps : {1u, 2u, 8u, 64u}) {
            Configuration config;
            config.variant = variant; config.slots = 8; config.overflow = 0; config.credits = 8;
            config.jobs_a = 3; config.jobs_b = 1; config.steps_a = config.steps_b = steps;
            config.visits = 1; config.waves = 4;
            Worker worker(config); const auto result = exercise(worker);
            require(result.logical_done_a == 12 && result.logical_done_b == 4,
                    "underfilled short-job reuse mismatch");
        }
    }
    Configuration config;
    config.mode = "cleanup"; config.jobs_a = 5; config.jobs_b = 5;
    config.slots = 3; config.overflow = 7; config.credits = 10; config.waves = 3;
    Worker worker(config); const auto result = cleanup_timing(worker);
    require(result.abandoned == 15 && result.logical_done_b == 15,
            "cleanup timed fixture did not preserve second parent");
}

Configuration parse(const Options& options) {
    Configuration c;
    c.mode = options.get("case", "two-parent");
    c.variant = options.get("variant", "shared");
    c.admission = options.get("admission", "fifo");
    c.cancel = options.get("cancel-mode", "selective");
    c.slots = options.number("slots", 8); c.overflow = options.number("overflow", 8);
    c.credits = options.number("credits", c.slots + c.overflow);
    c.visits = options.number("visits", 8); c.waves = options.number("waves", c.mode == "short" ? 256 : 1);
    c.jobs_a = options.number("jobs-a", c.mode == "cleanup" ? (c.slots + c.overflow + 1) / 2 : 32);
    c.jobs_b = options.number("jobs-b", c.mode == "cleanup" ? (c.slots + c.overflow) / 2 : 8);
    c.steps_a = options.number("steps-a", c.mode == "short" ? 1 : 64);
    c.steps_b = options.number("steps-b", c.mode == "short" ? c.steps_a : 2);
    c.cancel_after = options.number("cancel-a-after", 0);
    require(c.mode == "two-parent" || c.mode == "short" || c.mode == "cleanup", "unknown inventory case");
    require(c.variant == "shared" || c.variant == "parent-banks" || c.variant == "family-banks",
            "unknown inventory variant");
    require(c.admission == "fifo" || c.admission == "reserve-b", "unknown admission policy");
    require(c.cancel == "selective" || c.cancel == "discard" || c.cancel == "drain", "unknown cancellation mode");
    require(c.mode != "cleanup" || (c.variant == "shared" && c.admission == "fifo" &&
                                       c.cancel == "selective" && !c.cancel_after),
            "cleanup requires shared/fifo/selective with cancel-a-after zero");
    require(c.slots && c.credits && c.visits && c.waves && c.jobs_a && c.jobs_b && c.steps_a && c.steps_b,
            "positive slots/credits/visits/waves/jobs/steps required");
    require(c.admission != "reserve-b" || c.credits >= 2, "reserve-b needs at least two aggregate credits");
    constexpr std::uint64_t cap = std::uint64_t(1) << 30;
    require(c.slots <= cap / 1024 && c.overflow <= cap / 1024 &&
                c.slots + c.overflow <= cap / 1024 && c.jobs_a <= cap / 256 && c.jobs_b <= cap / 256 &&
                c.jobs_a + c.jobs_b <= cap / 256, "inventory allocation exceeds conservative 1-GiB bound");
    const auto bank_count = c.variant == "shared" ? 1u : 2u;
    require(c.credits <= bank_count * (c.slots + c.overflow), "credits exceed physical worker capacity");
    require(c.waves <= 1000000 && c.visits <= 1000000000 && c.steps_a <= 1000000000 && c.steps_b <= 1000000000,
            "inventory duration inputs exceed finite fixture bounds");
    require(c.jobs_a <= 1000000000 / c.steps_a && c.jobs_b <= 1000000000 / c.steps_b &&
                c.waves <= 1000000000 / (c.jobs_a * c.steps_a + c.jobs_b * c.steps_b),
            "inventory useful work exceeds one billion visits");
    return c;
}
} // namespace
} // namespace loom_spike

int main(int argc, char** argv) {
    try {
        loom_spike::Options options(argc, argv);
        if (options.has("check")) {
            loom_spike::checks();
            loom_spike::Record record;
            record.family = "inventory"; record.variant = "check"; record.unit = "fixture";
            record.items = 1; record.verified = true; record.repeat = options.number("repeat", 0);
            record.parameters = {{"scope", "selective boundary cleanup, two owners, scalar/vector banks, short jobs and reuse"}};
            loom_spike::emit(record);
            return 0;
        }
        auto config = loom_spike::parse(options);
        const auto cpus = options.cpus();
        if (!cpus.empty()) loom_spike::pin(cpus.front());
        loom_spike::Worker worker(config);
        try {
            const auto result = config.mode == "cleanup" ? loom_spike::cleanup_timing(worker) :
                                                           loom_spike::exercise(worker);
            loom_spike::emit_result(config, worker, result, options.number("repeat", 0));
        } catch (...) {
            worker.discard_remaining();
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << "loom_inventory: " << error.what() << '\n';
        return 1;
    }
}
