#include "messaging.h"
#include "microtask.h"
#include "support.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace loom_spike {
namespace {
namespace mt = microtask;
namespace msg = messaging;

// Private, actual buffer protocol. Loom's outer admission chooses work and
// capacity; these fixed spans stand in for Orbital-qualified geometry. A buffer
// remains unavailable until its physical consumer finishes AND the owner
// settles the captured generation receipt. No candidate Port/View runtime is
// claimed: actual Orbital admission/protection coupling remains unimplemented.
enum class State { free, producing, submitted };
struct alignas(64) Buffer {
    const std::uint64_t* input{};
    std::uint64_t* output{};
    std::uint64_t identity{}, generation{}, elements{};
    State state = State::free;
    bool ready = false;
};
struct alignas(64) Receipt {
    std::atomic<std::uint64_t> generation{};
    std::uint64_t identity{};
};
struct Config {
    std::vector<int> cpus;
    std::string variant = "reserved";
    std::uint64_t jobs = 64, buffers = 2, elements = 256, grain = 64;
    std::uint64_t work_capacity = 2, hold_ns = 100000, stall_ns = 1000000;
    std::uint64_t deadline_ns = 2000000000, repeat{};
    bool cancel_while_using = false;
    bool cancel_with_native_pending = false; // Authored cancellation boundary check only.
    bool queued_receipt_control = false; // Authored unified FIFO check only.
};
struct alignas(64) Peer {
    msg::Counters channels;
    std::atomic<bool> ready{}, done{}, failed{}, ack_blocked{};
    std::atomic<std::uint64_t> accepted_snapshot{}, receipts_snapshot{}, using_buffer{};
    std::uint64_t unique_offered{}, attempts{}, accepted{}, refused{};
    std::uint64_t consumed{}, receipts{}, receipt_refusals{}, errors{}, cpu_ns{};
    std::uint64_t read_passes{}, reads_after_cancel{}, checksum{}, use_after_cancel_ns{};
};

// This callback only sees already-reserved writable output and qualified input.
// Its native grain stops at a meaningful pipeline boundary; it has no slow
// wait/Signal/output-credit branch. Parent admission owns those decisions.
LOOM_MICROTASK_CC void produce(mt::ScalarRing* ring, mt::ScalarRing::Slot* slot,
                             std::uint64_t state, std::uint64_t cursor,
                             std::uint64_t grain, std::uint64_t unused) {
    auto* buffer = reinterpret_cast<Buffer*>(state);
    const auto stop = std::min(buffer->elements, cursor + grain);
    for (auto i = cursor; i < stop; ++i)
        buffer->output[i] = (buffer->input[i] + 11) * 3;
    if (stop == buffer->elements) {
        buffer->ready = true;
        ring->complete(*slot);
    }
    [[clang::musttail]] return mt::ScalarRing::yield(ring, slot, state, stop, grain, unused);
}

Record run_case(const Config& c) {
    pin(c.cpus[0]);
    const auto begin_input_bytes = c.jobs * c.elements * sizeof(std::uint64_t);
    const auto output_bytes = c.buffers * c.elements * sizeof(std::uint64_t);
    std::vector<std::uint64_t> input(c.jobs * c.elements), output(c.buffers * c.elements);
    for (std::uint64_t id = 0; id < c.jobs; ++id)
        for (std::uint64_t i = 0; i < c.elements; ++i)
            input[id * c.elements + i] = (id << 32) + i;
    auto buffers = std::make_unique<Buffer[]>(c.buffers);
    std::unique_ptr<Receipt[]> receipts;
    if (c.variant == "reserved") receipts = std::make_unique<Receipt[]>(c.buffers);
    for (std::uint64_t b = 0; b < c.buffers; ++b) {
        buffers[b].output = output.data() + b * c.elements;
        buffers[b].elements = c.elements;
    }
    std::vector<std::uint8_t> accepted(c.jobs), started(c.jobs), produced(c.jobs),
        submitted(c.jobs), consumed(c.jobs), returned(c.jobs), retired(c.jobs);
    std::vector<std::uint64_t> free;
    free.reserve(c.buffers);
    for (std::uint64_t b = c.buffers; b; --b) free.push_back(b - 1);
    const msg::Edge output_edge[]{ {0, 1} };
    msg::PairChannels outputs(2, c.buffers, true, output_edge, false);
    msg::InboxChannels work(2, c.work_capacity, true);
    mt::ScalarRing ring(c.buffers, 0, prefetch::Binding{prefetch::Class::none, 4096});
    msg::Counters owner_channels;
    Peer peer;
    std::atomic<bool> start{}, cancel{}, admission_closed{}, abort{};
    std::atomic<std::uint64_t> submitted_snapshot{};
    std::uint64_t owner_started{}, owner_produced{}, owner_submitted{}, owner_retired{}, owner_discarded{};
    std::uint64_t owner_errors{}, native_abandoned{}, receipt_polls{}, capacity_stops{}, stopped_service_visits{};
    std::uint64_t retained_peak{}, retained_at_cancel{}, in_use_at_cancel{}, settled_while_work_full{};
    std::uint64_t workload_received{}, deadlock_observed{}, deadline_failure{}, forced_retirements{};
    std::uint64_t deadlock_stall_ns{};
    std::uint64_t queued_receipts_at_deadlock{};
    std::uint64_t cancel_at{}, stalled_since{}, stop_started{}, stopped_ns{}, stop_cursor{};
    bool capacity_stopped = false;
    const auto record_receipt = [&](const msg::Packet& packet) {
        if (packet.payload >= c.buffers) { ++owner_errors; return; }
        auto& buffer = buffers[packet.payload];
        if (buffer.state != State::submitted || packet.identity != buffer.identity ||
            packet.sequence != buffer.generation || packet.identity >= c.jobs || retired[packet.identity]) {
            ++owner_errors; return;
        }
        if (peer.accepted_snapshot.load(std::memory_order_acquire) >= workload_received + c.work_capacity)
            ++settled_while_work_full;
        retired[packet.identity] = 1;
        ++owner_retired;
        buffer.state = State::free;
        buffer.ready = false;
        free.push_back(packet.payload);
    };
    const auto close_cancel = [&](std::uint64_t timestamp) {
        if (cancel.load(std::memory_order_relaxed)) return;
        cancel_at = timestamp;
        retained_at_cancel = c.buffers - free.size();
        in_use_at_cancel = peer.using_buffer.load(std::memory_order_acquire) != 0;
        if (capacity_stopped) { stopped_ns += timestamp - stop_started; capacity_stopped = false; }
        cancel.store(true, std::memory_order_release);
        native_abandoned += ring.discard();
        // Only owner-thread native frames are discarded here. Submitted spans
        // remain physically retained even after logical cancellation.
        for (std::uint64_t b = 0; b < c.buffers; ++b) if (buffers[b].state == State::producing) {
            if (buffers[b].ready) { produced[buffers[b].identity] = 1; ++owner_produced; }
            buffers[b].state = State::free;
            buffers[b].ready = false;
            free.push_back(b);
        }
        admission_closed.store(true, std::memory_order_release);
    };
    std::thread consumer([&] {
        try { pin(c.cpus[1]); }
        catch (...) { peer.failed.store(true); peer.ready.store(true, std::memory_order_release); peer.done.store(true); return; }
        peer.ready.store(true, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) relax();
        const auto cpu_begin = thread_cpu_ns();
        msg::Packet pending;
        bool held_receipt = false;
        std::uint64_t next{};
        bool waiting_first_cancel = c.cancel_while_using;
        while (!abort.load(std::memory_order_acquire)) {
            if (held_receipt) {
                bool posted = true;
                if (c.variant == "reserved") {
                    auto& receipt = receipts[pending.payload];
                    if (receipt.generation.load(std::memory_order_acquire)) { ++peer.errors; break; }
                    receipt.identity = pending.identity;
                    receipt.generation.store(pending.sequence, std::memory_order_release);
                } else posted = work.send(1, pending, peer.channels);
                if (posted) {
                    returned[pending.identity] = 1;
                    ++peer.receipts;
                    peer.receipts_snapshot.store(peer.receipts, std::memory_order_release);
                    held_receipt = false;
                    peer.ack_blocked.store(false, std::memory_order_release);
                } else {
                    ++peer.receipt_refusals;
                    peer.ack_blocked.store(true, std::memory_order_release);
                    relax();
                    continue;
                }
            }
            if (!cancel.load(std::memory_order_acquire)) for (unsigned offer = 0; offer < 8 && next < c.jobs; ++offer) {
                if (c.queued_receipt_control && !peer.consumed && next >= 8) break;
                if (peer.unique_offered == next) ++peer.unique_offered;
                ++peer.attempts;
                const msg::Packet request{next, next, 0, 0, 1, 0, 0, 0};
                if (!work.send(1, request, peer.channels)) { ++peer.refused; break; }
                accepted[next] = 1;
                ++peer.accepted; ++next;
                peer.accepted_snapshot.store(peer.accepted, std::memory_order_release);
            }
            msg::Packet packet;
            if (outputs.receive(1, packet, peer.channels)) {
                if (packet.payload >= c.buffers || packet.identity >= c.jobs || consumed[packet.identity]) { ++peer.errors; break; }
                const auto& buffer = buffers[packet.payload];
                const auto generation = packet.sequence;
                const auto id = packet.identity;
                if (buffer.identity != id || buffer.generation != generation) ++peer.errors;
                peer.using_buffer.store(packet.payload + 1, std::memory_order_release);
                const auto use_begin = now_ns();
                const auto release_at = use_begin + c.hold_ns;
                std::uint64_t saw_cancel{};
                do {
                    const bool after_cancel = cancel.load(std::memory_order_acquire);
                    if (after_cancel && !saw_cancel) saw_cancel = now_ns();
                    std::uint64_t checksum{};
                    const volatile auto* words = buffer.output;
                    for (std::uint64_t i = 0; i < c.elements; ++i) {
                        const auto actual = words[i];
                        // Independent oracle uses identity and position, never
                        // the producer's input window or transformation helper.
                        const auto expected = (id << 32) * 3 + i * 3 + 33;
                        if (actual != expected) ++peer.errors;
                        checksum += actual;
                    }
                    if (!consumed[id]) peer.checksum += checksum;
                    consumed[id] = 1;
                    ++peer.read_passes;
                    if (after_cancel) ++peer.reads_after_cancel;
                    if (abort.load(std::memory_order_acquire)) break;
                    relax();
                } while (now_ns() < release_at || (waiting_first_cancel && !saw_cancel));
                const auto use_end = now_ns();
                if (saw_cancel) peer.use_after_cancel_ns += use_end - saw_cancel;
                waiting_first_cancel = false;
                peer.using_buffer.store(0, std::memory_order_release);
                ++peer.consumed;
                pending = {id, generation, packet.payload, 0, 1, 0, 0, 1};
                held_receipt = true;
                continue;
            }
            if (admission_closed.load(std::memory_order_acquire) &&
                peer.consumed == submitted_snapshot.load(std::memory_order_acquire)) break;
            relax();
        }
        peer.cpu_ns = thread_cpu_ns() - cpu_begin;
        peer.done.store(true, std::memory_order_release);
    });
    while (!peer.ready.load(std::memory_order_acquire)) relax();
    const auto begin = now_ns(), cpu_begin = thread_cpu_ns();
    const auto deadline = begin + c.deadline_ns;
    const auto drain_deadline = deadline + c.buffers * c.hold_ns + 100000000;
    start.store(true, std::memory_order_release);
    while (true) {
        const auto timestamp = now_ns();
        if (c.cancel_while_using && peer.using_buffer.load(std::memory_order_acquire) &&
            (!c.cancel_with_native_pending || ring.pending())) close_cancel(timestamp);
        if (timestamp >= deadline && !cancel.load(std::memory_order_relaxed)) { deadline_failure = 1; close_cancel(timestamp); }
        if (c.variant == "reserved") for (std::uint64_t b = 0; b < c.buffers; ++b) {
            ++receipt_polls;
            const auto generation = receipts[b].generation.load(std::memory_order_acquire);
            if (!generation) continue;
            const msg::Packet receipt{receipts[b].identity, generation, b, 0, 1, 0, 0, 1};
            record_receipt(receipt);
            receipts[b].generation.store(0, std::memory_order_release);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            msg::Packet packet;
            for (unsigned count = 0; count < 16 && work.receive(0, packet, owner_channels); ++count) {
                if (packet.reserved == 1) record_receipt(packet);
                else { ++workload_received; ++owner_discarded; }
            }
        } else {
            // This is the explicit output safe stop. Workload admission may
            // stop, while independent physical receipt/reclamation runs above.
            if (free.empty() && owner_started < c.jobs) {
                if (!capacity_stopped) { ++capacity_stops; stop_started = timestamp; stop_cursor = owner_started; }
                capacity_stopped = true; ++stopped_service_visits;
            } else if (capacity_stopped) { stopped_ns += timestamp - stop_started; capacity_stopped = false; }
            msg::Packet request;
            const auto admit_batch = c.cancel_with_native_pending ? 1U : 8U;
            for (unsigned count = 0; count < admit_batch && !free.empty() && work.receive(0, request, owner_channels); ++count) {
                if (request.reserved == 1) { record_receipt(request); continue; }
                ++workload_received;
                if (request.identity >= c.jobs || started[request.identity] || request.sequence != request.identity ||
                    request.identity != owner_started) { ++owner_errors; continue; }
                const auto b = free.back(); free.pop_back();
                auto& buffer = buffers[b];
                buffer.input = input.data() + request.identity * c.elements;
                buffer.identity = request.identity; ++buffer.generation;
                buffer.state = State::producing; buffer.ready = false;
                const bool admitted = ring.push(produce, {reinterpret_cast<std::uint64_t>(&buffer), 0, c.grain, 0},
                                                {buffer.input, c.elements * sizeof(std::uint64_t)});
                if (!admitted) { ++owner_errors; buffer.state = State::free; free.push_back(b); ++owner_discarded; continue; }
                started[request.identity] = 1; ++owner_started;
                retained_peak = std::max(retained_peak, c.buffers - free.size());
            }
            // Authored cancellation check holds the parent at its spin-return
            // boundary after first submission, leaving another frame pending.
            if (ring.pending() && !(c.cancel_with_native_pending && owner_submitted))
                ring.spin({c.cancel_with_native_pending ? 1U : 16U, 0, 16});
            for (std::uint64_t b = 0; b < c.buffers; ++b) if (buffers[b].state == State::producing && buffers[b].ready) {
                auto& buffer = buffers[b];
                const msg::Packet packet{buffer.identity, buffer.generation, b, 0, 0, 1, 0, 2};
                if (!outputs.send(0, packet, owner_channels)) { ++owner_errors; continue; }
                produced[buffer.identity] = 1; submitted[buffer.identity] = 1;
                ++owner_produced; ++owner_submitted;
                buffer.state = State::submitted;
                submitted_snapshot.store(owner_submitted, std::memory_order_release);
            }
            if (owner_started == c.jobs && !ring.pending()) admission_closed.store(true, std::memory_order_release);
            const auto published_receipts = peer.receipts_snapshot.load(std::memory_order_acquire);
            if (c.variant == "unified" && free.empty() && !ring.pending() &&
                (peer.ack_blocked.load(std::memory_order_acquire) || published_receipts > owner_retired)) {
                if (!stalled_since) stalled_since = timestamp;
                if (timestamp - stalled_since >= c.stall_ns) {
                    deadlock_observed = 1; deadlock_stall_ns = timestamp - stalled_since;
                    queued_receipts_at_deadlock = published_receipts > owner_retired ? published_receipts - owner_retired : 0;
                    close_cancel(timestamp);
                }
            } else stalled_since = 0;
        }
        if (peer.done.load(std::memory_order_acquire) && free.size() == c.buffers &&
            workload_received == peer.accepted_snapshot.load(std::memory_order_acquire)) break;
        if (timestamp >= drain_deadline || peer.failed.load()) { deadline_failure = 1; abort.store(true, std::memory_order_release); break; }
        relax();
    }
    if (capacity_stopped) stopped_ns += now_ns() - stop_started;
    const auto owner_cpu = thread_cpu_ns() - cpu_begin;
    consumer.join();
    // Even a failed fixture joins the physical consumer before releasing storage.
    // Forced retirement is visible and makes the result fail verification.
    native_abandoned += ring.discard();
    for (std::uint64_t b = 0; b < c.buffers; ++b) if (buffers[b].state != State::free) {
        ++forced_retirements; buffers[b].state = State::free;
    }
    const auto end = now_ns();
    std::uint64_t expected_checksum{}, accounting_errors{};
    for (std::uint64_t id = 0; id < c.jobs; ++id) {
        if (accepted[id] != (id < peer.accepted)) ++accounting_errors;
        if (started[id] && !accepted[id]) ++accounting_errors;
        if (produced[id] && !started[id]) ++accounting_errors;
        if (submitted[id] && !produced[id]) ++accounting_errors;
        if (submitted[id] != consumed[id] || consumed[id] != returned[id] || returned[id] != retired[id]) ++accounting_errors;
        if (consumed[id]) for (std::uint64_t i = 0; i < c.elements; ++i)
            expected_checksum += (id << 32) * 3 + i * 3 + 33;
    }
    const auto errors = owner_errors + peer.errors + accounting_errors;
    Record record;
    record.family = "output-pressure"; record.variant = c.variant; record.unit = "outputs";
    record.repeat = c.repeat; record.elapsed_ns = end - begin; record.items = owner_retired;
    record.parameters = {{"jobs", std::to_string(c.jobs)}, {"buffers", std::to_string(c.buffers)},
        {"elements", std::to_string(c.elements)}, {"grain", std::to_string(c.grain)},
        {"work_capacity", std::to_string(c.work_capacity)}, {"hold_ns", std::to_string(c.hold_ns)},
        {"cancel_while_using", c.cancel_while_using ? "true" : "false"},
        {"cancel_with_native_pending", c.cancel_with_native_pending ? "true" : "false"},
        {"queued_receipt_control", c.queued_receipt_control ? "true" : "false"},
        {"coupling", "private_qualified_spans_generation_receipts_no_candidate_Orbital_Port_View_runtime"},
        {"stop", "owner_output_credit_boundary_callback_has_reserved_span"},
        {"receipt_path", c.variant == "reserved" ? "one_pre_reserved_cell_per_retained_buffer" : "shared_workload_FIFO_negative"},
        {"first_touch_cpu", std::to_string(c.cpus[0])}};
    record.counters = {{"planned", c.jobs}, {"accepted", peer.accepted}, {"unique_offered", peer.unique_offered},
        {"offered_attempts", peer.attempts}, {"refused_work", peer.refused}, {"unoffered", c.jobs - peer.unique_offered},
        {"unique_unaccepted", peer.unique_offered - peer.accepted},
        {"started", owner_started}, {"unstarted", peer.accepted - owner_started}, {"produced", owner_produced},
        {"submitted", owner_submitted}, {"consumed", peer.consumed}, {"receipts", peer.receipts}, {"retired", owner_retired},
        {"unfinished", owner_submitted - owner_retired}, {"discarded_unstarted", owner_discarded},
        {"canceled_started", owner_started - owner_submitted},
        {"native_abandoned", native_abandoned}, {"capacity_stops", capacity_stops}, {"last_stop_cursor", stop_cursor},
        {"stopped_service_visits", stopped_service_visits}, {"output_stop_ns", stopped_ns},
        {"receipt_refusals", peer.receipt_refusals}, {"receipt_polls", receipt_polls},
        {"receipts_settled_while_work_full", settled_while_work_full}, {"deadlock_observed", deadlock_observed},
        {"deadlock_stall_ns", deadlock_stall_ns}, {"free_credits_after_drain", free.size()},
        {"queued_receipts_at_deadlock", queued_receipts_at_deadlock},
        {"logical_cancel", cancel_at != 0}, {"in_use_at_cancel", in_use_at_cancel},
        {"retained_at_cancel", retained_at_cancel}, {"retained_peak", retained_peak},
        {"retained_peak_bytes", retained_peak * c.elements * sizeof(std::uint64_t)},
        {"retained_after_join", forced_retirements}, {"forced_retirements", forced_retirements},
        {"use_after_cancel_ns", peer.use_after_cancel_ns}, {"physical_drain_ns", cancel_at ? end - cancel_at : 0},
        {"output_pool_bytes", output_bytes}, {"input_bytes", begin_input_bytes},
        {"receipt_bytes", c.variant == "reserved" ? c.buffers * sizeof(Receipt) : 0}, {"buffer_state_bytes", c.buffers * sizeof(Buffer)},
        {"channel_bytes", work.storage_bytes() + outputs.storage_bytes()}, {"ring_bytes", ring.storage_bytes()},
        {"owner_cpu_ns", owner_cpu}, {"consumer_cpu_ns", peer.cpu_ns}, {"read_passes", peer.read_passes},
        {"reads_after_cancel", peer.reads_after_cancel},
        {"checksum", peer.checksum}, {"expected_checksum", expected_checksum}, {"errors", errors},
        {"deadline_failure", deadline_failure}, {"pin_failure", peer.failed.load()}};
    const bool negative = c.variant == "unified" && !c.cancel_while_using;
    record.verified = !errors && !deadline_failure && !peer.failed.load() && !forced_retirements &&
        !record.counters["unfinished"] && owner_retired == peer.consumed && owner_retired == peer.receipts &&
        peer.accepted == owner_started + owner_discarded && peer.checksum == expected_checksum &&
        owner_started == owner_produced + native_abandoned &&
        (c.cancel_while_using ? (in_use_at_cancel && retained_at_cancel && peer.use_after_cancel_ns && peer.reads_after_cancel) :
         negative ? (deadlock_observed && (peer.receipt_refusals || queued_receipts_at_deadlock) && owner_discarded) :
                    (peer.accepted == c.jobs && owner_retired == c.jobs && !cancel_at)) &&
        (c.variant != "reserved" || !peer.receipt_refusals);
    if (c.cancel_with_native_pending) record.verified &= native_abandoned && owner_started > owner_submitted;
    if (c.queued_receipt_control) record.verified &= queued_receipts_at_deadlock && !peer.receipt_refusals;
    return record;
}
} // namespace
} // namespace loom_spike

int main(int argc, char** argv) {
    using namespace loom_spike;
    try {
        Options options(argc, argv);
        Config c; c.cpus = options.cpus(); c.variant = options.get("variant", "reserved");
        c.jobs = options.number("jobs", 64); c.buffers = options.number("buffers", 2);
        c.elements = options.number("elements", 256); c.grain = options.number("grain", 64);
        c.work_capacity = options.number("work-capacity", 2);
        const auto hold_us = options.number("hold-us", 100), stall_ms = options.number("stall-ms", 1),
                   deadline_ms = options.number("deadline-ms", 2000);
        if (hold_us > 100000 || stall_ms > 100 || !deadline_ms || deadline_ms > 10000)
            throw std::invalid_argument("invalid/beyond bounded pressure time limits");
        c.hold_ns = hold_us * 1000; c.stall_ns = stall_ms * 1000000; c.deadline_ns = deadline_ms * 1000000;
        c.cancel_while_using = options.has("cancel-while-using"); c.repeat = options.number("repeat", 0);
        if (c.cpus.size() < 2 || (c.variant != "reserved" && c.variant != "unified") || !c.jobs || c.jobs > 100000 ||
            !c.buffers || c.buffers > 128 || !c.elements || c.elements > 65536 || !c.grain || c.grain > 65536 ||
            c.work_capacity < 2 || c.work_capacity > 65536 || c.hold_ns > 100000000 || !c.deadline_ns ||
            c.deadline_ns > 10000000000ULL || c.stall_ns > 100000000 ||
            c.jobs * c.elements * sizeof(std::uint64_t) > 128ULL * 1024 * 1024 ||
            (c.cancel_while_using && !c.hold_ns) || (c.variant == "unified" && !c.cancel_while_using && c.jobs <= c.buffers + c.work_capacity))
            throw std::invalid_argument("invalid/beyond bounded pressure fixture limits");
        if (options.has("check")) {
            bool verified = true;
            for (const auto* variant : {"reserved", "unified"}) for (const bool cancel : {false, true}) {
                auto check = c; check.variant = variant; check.cancel_while_using = cancel;
                check.jobs = 19; check.buffers = 2; check.elements = 73; check.grain = 7; check.work_capacity = 2;
                check.hold_ns = 1000000; check.deadline_ns = 2000000000; check.stall_ns = 1000000;
                auto record = run_case(check);
                if (check.variant == "reserved" && !cancel)
                    record.verified &= record.counters["capacity_stops"] && record.counters["receipts_settled_while_work_full"];
                verified &= record.verified; emit(record);
            }
            auto check = c; check.variant = "reserved"; check.cancel_while_using = false;
            check.jobs = 7; check.buffers = 1; check.elements = 1; check.grain = 1; check.hold_ns = 1000000;
            auto record = run_case(check);
            record.verified &= record.counters["capacity_stops"] && record.counters["receipts_settled_while_work_full"];
            verified &= record.verified; emit(record);
            check.jobs = 31; check.buffers = 3; check.elements = 257; check.grain = 63;
            record = run_case(check);
            record.verified &= record.counters["capacity_stops"] && record.counters["receipts_settled_while_work_full"];
            verified &= record.verified; emit(record);
            check.jobs = 19; check.buffers = 2; check.elements = 73; check.grain = 7;
            check.cancel_while_using = true; check.cancel_with_native_pending = true;
            record = run_case(check); verified &= record.verified; emit(record);
            check.variant = "unified"; check.jobs = 66; check.buffers = 1; check.work_capacity = 64;
            check.cancel_while_using = false; check.cancel_with_native_pending = false; check.queued_receipt_control = true;
            record = run_case(check); verified &= record.verified; emit(record);
            return verified ? 0 : 1;
        }
        const auto record = run_case(c); emit(record); return record.verified ? 0 : 1;
    } catch (const std::exception& e) {
        Record record; record.family = "output-pressure-error"; record.variant = "error"; record.parameters["error"] = e.what();
        emit(record); return 2;
    }
}
