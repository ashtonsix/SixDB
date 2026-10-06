#include "support.h"
#include "microtask.h"
#include "messaging.h"

#include <algorithm>
#include <atomic>
#include <numeric>
#include <random>
#include <thread>
#include <stdexcept>

namespace {
namespace mt = loom_spike::microtask;
namespace message = loom_spike::messaging;
struct alignas(64) Node { std::uint64_t next{}, value{}; };
struct Work {
    const std::vector<Node>* nodes{};
    std::uint64_t start{}, steps{}, result{}, finished{};
};
template <bool Prefetch>
LOOM_MICROTASK_CC void hop(mt::ScalarRing* ring, mt::ScalarRing::Slot* slot,
                         std::uint64_t cursor, std::uint64_t remaining,
                         std::uint64_t result, std::uint64_t owner) {
    auto& work = *reinterpret_cast<Work*>(owner);
    const auto& node = (*work.nodes)[cursor];
    result += node.value;
    cursor = node.next;
    if (!--remaining) {
        work.result = result;
        ++work.finished;
        ring->complete(*slot);
    } else if constexpr (Prefetch)
        ring->depend_known<loom_spike::prefetch::Class::l1_1>(*slot, {&(*work.nodes)[cursor], 64});
    [[clang::musttail]] return mt::ScalarRing::yield(ring, slot, cursor, remaining, result, owner);
}
std::uint64_t percentile(std::vector<std::uint64_t> values, unsigned percent) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * percent / 100];
}
}

int main(int argc, char** argv) {
    try {
        loom_spike::Options options(argc, argv);
        const bool check = options.has("check");
        const auto cpus = options.cpus();
        if (cpus.size() < 2) throw std::invalid_argument("service needs two CPUs");
        const auto jobs = check ? 9 : options.number("jobs", 64);
        const auto steps = check ? 1021 : options.number("steps", 32768);
        const auto count = check ? 2048 : options.number("dataset-items", 4194304);
        const auto messages = check ? 31 : options.number("messages", 1000);
        const auto interval = check ? 0 : options.number("interval-ns", 100000);
        const auto visits = options.number("visits", 128);
        const auto time_budget = options.number("budget-ns", 0);
        const auto message_budget = options.number("message-budget", 16);
        const auto ring_size = options.number("ring", 16);
        const auto observer = options.get("service-observer", "full");
        if (observer != "full" && observer != "reduced")
            throw std::invalid_argument("service observer must be full or reduced");
        const bool observe_gaps = observer == "full";
        if (!jobs || jobs > 10000 || !steps || steps > 10000000 || !count || count > 16777216 ||
            !messages || messages > 100000 || !visits || visits > 65536 ||
            !message_budget || message_budget > 65536 || !ring_size || ring_size > 256 ||
            interval > 30000000000ULL / messages || steps > 50000000ULL / jobs)
            throw std::invalid_argument("service bounds exceeded");
        loom_spike::pin(cpus[0]);
        std::vector<Node> nodes(count);
        std::vector<std::uint64_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 random(712);
        std::shuffle(order.begin(), order.end(), random);
        for (std::size_t i = 0; i < count; ++i) {
            nodes[order[i]].next = order[(i + 1) % count];
            nodes[order[i]].value = random();
        }
        std::vector<Work> work(jobs);
        const bool prefetch = options.has("prefetch");
        mt::ScalarRing ring(ring_size, jobs, loom_spike::prefetch::Binding{
            prefetch ? loom_spike::prefetch::Class::l1_1 : loom_spike::prefetch::Class::none, 4096});
        for (std::size_t i = 0; i < jobs; ++i) {
            work[i] = {&nodes, (i * 15485863ULL) % count, steps, 0, 0};
            if (!ring.push(prefetch ? hop<true> : hop<false>,
                           {work[i].start, steps, 0, reinterpret_cast<std::uint64_t>(&work[i]), {}},
                           {&nodes[work[i].start], 64}))
                throw std::runtime_error("preallocated service admission refused");
        }
        const std::array<message::Edge, 2> edges{{{0, 1}, {1, 0}}};
        message::Channels channels(message::Variant::sparse, 2, 2, 64, true, edges);
        message::Counters owner_counters, sender_counters;
        std::vector<std::uint64_t> waits(messages);
        std::vector<std::uint64_t> scheduled_waits(messages);
        std::vector<bool> live_cohort(messages, false);
        std::vector<bool> seen(messages, false);
        std::atomic<bool> ready{false}, go{false}, sender_done{false};
        std::atomic<bool> failed{false};
        std::uint64_t accepted = 0, replied = 0, refused = 0, sender_cpu = 0, start = 0;
        std::uint64_t admission_delay = 0, max_admission_delay = 0;
        std::thread sender([&] {
            try { loom_spike::pin(cpus[1]); }
            catch (...) {
                failed.store(true);
                ready.store(true, std::memory_order_release);
                sender_done.store(true, std::memory_order_release);
                return;
            }
            ready.store(true, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) loom_spike::relax();
            const auto cpu_begin = loom_spike::thread_cpu_ns();
            const auto deadline = start + 40000000000ULL;
            std::uint64_t first_offer = 0;
            while (replied < messages) {
                const auto now = loom_spike::now_ns();
                if (now >= deadline) break;
                if (accepted < messages && now >= start + accepted * interval) {
                    if (!first_offer) first_offer = now;
                    message::Packet packet{accepted, accepted, accepted ^ 12345, first_offer, 1, 0, 0, 0};
                    if (channels.send(1, packet, sender_counters)) {
                        const auto delay = now - (start + accepted * interval);
                        admission_delay += delay;
                        max_admission_delay = std::max(max_admission_delay, delay);
                        ++accepted;
                        first_offer = 0;
                    }
                    else ++refused;
                }
                message::Packet packet;
                if (channels.receive(1, packet, sender_counters)) {
                    if (packet.identity >= messages || seen[packet.identity] ||
                        packet.payload != (packet.identity ^ 12345) || packet.source != 0 ||
                        packet.destination != 1 || packet.sequence != packet.identity || packet.hops != 2)
                        failed.store(true);
                    else {
                        seen[packet.identity] = true;
                        const auto completion = loom_spike::now_ns();
                        waits[packet.identity] = completion - packet.offered_ns;
                        scheduled_waits[packet.identity] = completion - (start + packet.identity * interval);
                        live_cohort[packet.identity] = packet.reserved != 0;
                        ++replied;
                    }
                }
                loom_spike::relax();
            }
            sender_cpu = loom_spike::thread_cpu_ns() - cpu_begin;
            sender_done.store(true, std::memory_order_release);
        });
        while (!ready.load(std::memory_order_acquire)) loom_spike::relax();
        start = loom_spike::now_ns();
        const auto cpu_begin = loom_spike::thread_cpu_ns();
        go.store(true, std::memory_order_release);
        bool pending_reply = false;
        message::Packet reply;
        std::uint64_t services = 0, spins = 0, max_gap = 0, previous_service = start;
        std::uint64_t ring_finished = 0;
        while (ring.pending() || !sender_done.load(std::memory_order_acquire)) {
            // Full gap observation is a separately measured observer. The
            // reduced path checks the same outer failure bound every 64 polls.
            if (observe_gaps || !(services & 63)) {
                const auto service_time = loom_spike::now_ns();
                if (service_time >= start + 40000000000ULL) { failed.store(true); break; }
                if (observe_gaps) {
                    max_gap = std::max(max_gap, service_time - previous_service);
                    previous_service = service_time;
                }
            }
            ++services;
            for (std::uint64_t n = 0; n < message_budget; ++n) {
                if (!pending_reply) {
                    if (!channels.receive(0, reply, owner_counters)) break;
                    reply.reserved = ring.pending() != 0;
                    std::swap(reply.source, reply.destination);
                    pending_reply = true;
                }
                if (!channels.send(0, reply, owner_counters)) break;
                pending_reply = false;
            }
            if (ring.pending()) {
                ring.spin({visits, time_budget, 16});
                ++spins;
                if (!ring.pending()) ring_finished = loom_spike::now_ns();
            } else loom_spike::relax();
        }
        sender.join();
        const auto elapsed = loom_spike::now_ns() - start;
        const auto owner_cpu = loom_spike::thread_cpu_ns() - cpu_begin;
        const auto unfinished_microtasks = ring.pending();
        if (unfinished_microtasks) ring.discard();
        bool verified = !failed.load() && accepted == messages && replied == messages && !pending_reply;
        std::uint64_t hash = 0;
        for (const auto& item : work) {
            auto cursor = item.start;
            std::uint64_t expected = 0;
            for (std::uint64_t n = 0; n < steps; ++n) {
                expected += nodes[cursor].value;
                cursor = nodes[cursor].next;
            }
            verified &= item.finished == 1 && item.result == expected;
            hash += item.result;
        }
        std::vector<std::uint64_t> completed_waits, completed_scheduled, live_waits, post_waits;
        std::uint64_t first_live = messages, last_live = 0;
        for (std::size_t id = 0; id < messages; ++id) if (seen[id]) {
            completed_waits.push_back(waits[id]); completed_scheduled.push_back(scheduled_waits[id]);
            if (live_cohort[id]) {
                live_waits.push_back(waits[id]);
                first_live = std::min<std::uint64_t>(first_live, id); last_live = id;
            } else post_waits.push_back(waits[id]);
        }
        loom_spike::Record record;
        record.family = "service"; record.variant = time_budget ? "visits-and-time" : "visits";
        record.unit = "microtask-visit"; record.repeat = options.number("repeat", 0);
        record.elapsed_ns = elapsed; record.items = ring.counters().visits; record.verified = verified;
        record.parameters = {{"owner_cpu", std::to_string(cpus[0])}, {"sender_cpu", std::to_string(cpus[1])},
            {"visits", std::to_string(visits)}, {"budget_ns", std::to_string(time_budget)},
            {"message_budget", std::to_string(message_budget)}, {"messages", std::to_string(messages)},
            {"interval_ns", std::to_string(interval)}, {"jobs", std::to_string(jobs)},
            {"steps", std::to_string(steps)}, {"ring", std::to_string(ring_size)},
            {"hint_path", "specialized_inline"},
            {"dataset_items", std::to_string(count)}, {"prefetch", options.has("prefetch") ? "first" : "none"},
            {"observer", observe_gaps ? "every-message RTT and every-service gap" :
                "every-message RTT; failure clock every 64 services"}};
        record.counters = {{"accepted_messages", accepted}, {"completed_messages", replied},
            {"offered_attempts", accepted + refused}, {"refused_offers", refused},
            {"unfinished_messages", accepted - replied}, {"unoffered_messages", messages - accepted},
            {"admission_delay_total_ns", admission_delay}, {"admission_delay_max_ns", max_admission_delay},
            {"microtask_accepted", ring.counters().accepted}, {"microtask_completed", ring.counters().completed},
            {"microtask_abandoned", ring.counters().abandoned},
            {"microtask_unfinished", unfinished_microtasks},
            {"ring_wall_ns", ring_finished ? ring_finished - start : elapsed},
            {"microtask_spins", spins}, {"owner_services", services}, {"max_service_gap_ns", max_gap},
            {"service_gap_measured", observe_gaps},
            {"message_rtt_p50_ns", percentile(completed_waits, 50)}, {"message_rtt_p99_ns", percentile(completed_waits, 99)},
            {"message_rtt_max_ns", percentile(completed_waits, 100)},
            {"scheduled_to_complete_p99_ns", percentile(completed_scheduled, 99)},
            {"live_ring_messages", live_waits.size()}, {"post_ring_messages", post_waits.size()},
            {"first_live_ring_message", first_live}, {"last_live_ring_message", last_live},
            {"live_ring_rtt_p50_ns", percentile(live_waits, 50)}, {"live_ring_rtt_p99_ns", percentile(live_waits, 99)},
            {"post_ring_rtt_p99_ns", percentile(post_waits, 99)}, {"owner_cpu_ns", owner_cpu},
            {"sender_cpu_ns", sender_cpu}, {"output_hash", hash},
            {"budget_stops", ring.counters().budget_stops}, {"overflow_high", ring.counters().high_overflow}};
        loom_spike::emit(record);
        return verified ? 0 : 1;
    } catch (const std::exception& error) {
        loom_spike::Record record;
        record.family = "service-error"; record.unit = "case";
        record.parameters["error"] = error.what();
        loom_spike::emit(record);
        return 2;
    }
}
