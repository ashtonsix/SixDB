// Measurement fixture, not an Orbital queue interface. No network or persistence.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <time.h>
#include <vector>

using U = std::uint64_t;
constexpr U pattern = 0xa59fc6714238de09ULL;
U stamp(clockid_t clock = CLOCK_MONOTONIC_RAW) {
    timespec t{};
    if (clock_gettime(clock, &t)) std::abort();
    return U(t.tv_sec) * 1000000000 + U(t.tv_nsec);
}
void relax() {
#if defined(__x86_64__)
    asm volatile("pause");
#elif defined(__aarch64__)
    asm volatile("yield");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}
void pin(int cpu) {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (cpu < 0 || cpu >= CPU_SETSIZE || sched_getaffinity(0, sizeof(allowed), &allowed) ||
        !CPU_ISSET(cpu, &allowed)) throw std::runtime_error("CPU not allowed");
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set))
        throw std::runtime_error("affinity failed");
}
void until(U due) {
    for (;;) {
        const U now = stamp();
        if (now >= due) return;
        // Same producer pacing in all policies. Scheduled latency includes lateness.
        if (due - now > 100000)
            std::this_thread::sleep_for(std::chrono::nanoseconds(due - now - 50000));
        else relax();
    }
}
struct alignas(256) Counter { std::atomic<U> value{0}; };
struct Slot { U id; }; // Payload follows; each slot starts on a 256-byte boundary.
struct Sent { U prepared = 0, batch = 0, refused_at = 0, refusal_size = 0; std::uint8_t state = 0; };
struct Received { U begin = 0, end = 0; bool done = false; };
struct Publication { U before, after; };
struct Config {
    int producer = 0, consumer = 1, neighbour = 2;
    U interval = 1000, burst = 1, count = 200000, bytes = 64, capacity = 256, batch = 1;
    U pause_ns = 0;
    std::string policy = "spin", background = "none", timing = "full", output = "samples.csv";
};
U independent_work(U& state) {
    for (int i = 0; i < 128; ++i) {
        state = state * 6364136223846793005ULL + 1;
        asm volatile("" : "+r"(state));
    }
    return 128;
}
int main(int argc, char** argv) try {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (++i == argc) throw std::runtime_error("missing argument");
        const std::string val = argv[i];
        if (arg == "--producer") c.producer = std::stoi(val);
        else if (arg == "--consumer") c.consumer = std::stoi(val);
        else if (arg == "--neighbour") c.neighbour = std::stoi(val);
        else if (arg == "--interval") c.interval = std::stoull(val);
        else if (arg == "--burst") c.burst = std::stoull(val);
        else if (arg == "--count") c.count = std::stoull(val);
        else if (arg == "--bytes") c.bytes = std::stoull(val);
        else if (arg == "--capacity") c.capacity = std::stoull(val);
        else if (arg == "--batch") c.batch = std::stoull(val);
        else if (arg == "--pause-ns") c.pause_ns = std::stoull(val);
        else if (arg == "--policy") c.policy = val;
        else if (arg == "--background") c.background = val;
        else if (arg == "--timing") c.timing = val;
        else if (arg == "--output") c.output = val;
        else throw std::runtime_error("unknown argument " + arg);
    }
    if (c.producer == c.consumer || !c.interval || c.interval > 100000000 || !c.burst || c.burst > 65536 ||
        !c.count || c.count > 2000000 || ((c.count - 1) / c.burst) * c.interval > 1000000000 ||
        c.bytes < 16 || c.bytes > 65536 || c.bytes % 8 || !c.capacity || c.capacity > 4096 ||
        !c.batch || c.batch > c.capacity || c.pause_ns > 100000000 ||
        (c.policy != "spin" && c.policy != "wait" && c.policy != "work") ||
        (c.timing != "full" && c.timing != "reduced") ||
        (c.background != "none" && c.background != "stream") ||
        (c.background == "stream" && (c.neighbour == c.producer || c.neighbour == c.consumer)))
        throw std::runtime_error("invalid configuration");
    // Check affinity before threads inherit the producer's eventual narrower mask.
    cpu_set_t available;
    if (sched_getaffinity(0, sizeof(available), &available)) throw std::runtime_error("affinity read");
    for (int cpu : {c.producer, c.consumer, c.neighbour})
        if (cpu < 0 || cpu >= CPU_SETSIZE || !CPU_ISSET(cpu, &available))
            throw std::runtime_error("requested CPU unavailable");
    const U stride = ((sizeof(Slot) + c.bytes + 255) / 256) * 256;
    std::unique_ptr<void, decltype(&std::free)> memory(std::aligned_alloc(256, stride * c.capacity), std::free);
    if (!memory) throw std::bad_alloc();
    // First touch is explicitly producer-local. Warm pages, no cold-cache claim.
    pin(c.producer);
    std::memset(memory.get(), 0, stride * c.capacity);
    std::vector<U> clock_pairs;
    clock_pairs.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        const U a = stamp(), b = stamp();
        clock_pairs.push_back(b - a);
    }
    std::sort(clock_pairs.begin(), clock_pairs.end());
    if (pthread_setaffinity_np(pthread_self(), sizeof(available), &available)) std::abort();
    auto slot = [&](U index) { return reinterpret_cast<Slot*>(static_cast<char*>(memory.get()) + index % c.capacity * stride); };
    Counter tail, head;
    alignas(256) std::atomic<std::uint32_t> signal{0};
    alignas(256) std::atomic<bool> producer_done{false}, stop_background{false};
    std::atomic<unsigned> ready{0};
    std::atomic<U> start{0};
    std::vector<Sent> sent(c.count);
    std::vector<Received> received(c.count);
    std::vector<Publication> publications;
    publications.reserve(c.count);
    U consumer_cpu = 0, background_cpu = 0, background_bytes = 0;
    U errors = 0, consumed = 0, max_depth = 0, empty_polls = 0, work = 0, wake_calls = 0;
    U consumer_finish = 0, background_sum = 0, work_state = 1;
    const U duration = ((c.count - 1) / c.burst) * c.interval;
    const U offer_window = ((c.count + c.burst - 1) / c.burst) * c.interval;
    const bool detailed = c.timing == "full";
    auto notify = [&] {
        if (c.policy == "wait") {
            signal.fetch_add(1, std::memory_order_release);
            signal.notify_one();
        }
    };
    std::jthread background;
    if (c.background == "stream") background = std::jthread([&] {
        pin(c.neighbour);
        std::vector<U> data(64 * 1024 * 1024 / sizeof(U), pattern);
        ready.fetch_add(1, std::memory_order_release);
        U t0;
        while (!(t0 = start.load(std::memory_order_acquire))) relax();
        until(t0);
        const U cpu0 = stamp(CLOCK_THREAD_CPUTIME_ID);
        while (!stop_background.load(std::memory_order_relaxed)) {
            for (U i = 0; i < data.size(); ++i) background_sum += data[i];
            background_bytes += data.size() * sizeof(U);
            asm volatile("" : "+r"(background_sum) :: "memory");
        }
        background_cpu = stamp(CLOCK_THREAD_CPUTIME_ID) - cpu0;
    });
    std::jthread receiver([&] {
        pin(c.consumer);
        ready.fetch_add(1, std::memory_order_release);
        U t0;
        while (!(t0 = start.load(std::memory_order_acquire))) relax();
        until(t0);
        const U cpu0 = stamp(CLOCK_THREAD_CPUTIME_ID);
        U read = 0, last_id = 0;
        bool paused = false;
        for (;;) {
            if (stamp() >= t0 + duration + 500000000) break;
            if (c.pause_ns && !paused && stamp() >= t0 + duration / 2) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(c.pause_ns));
                paused = true;
            }
            // Observe signal before tail: publishing between the tail check and
            // wait changes the expected value, so the consumer cannot miss a wake.
            const auto observed = signal.load(std::memory_order_acquire);
            const U available_tail = tail.value.load(std::memory_order_acquire);
            if (available_tail == read) {
                if (producer_done.load(std::memory_order_acquire)) {
                    // producer_done can become visible after the earlier tail load.
                    if (tail.value.load(std::memory_order_acquire) == read) break;
                    continue;
                }
                ++empty_polls;
                if (c.policy == "wait") { ++wake_calls; signal.wait(observed, std::memory_order_relaxed); }
                else if (c.policy == "work") work += independent_work(work_state);
                else relax();
                continue;
            }
            const U n = std::min(c.batch, available_tail - read);
            for (U j = 0; j < n; ++j) {
                const U begin = detailed ? stamp() : 0;
                const auto* s = slot(read + j);
                const U id = s->id;
                if (id >= c.count || (consumed && id <= last_id)) std::abort();
                const auto* payload = reinterpret_cast<const U*>(s + 1);
                U bad = (payload[0] != id) + (payload[c.bytes / 8 - 1] != ~id);
                for (U k = 1; k + 1 < c.bytes / 8; ++k) bad += payload[k] != pattern;
                errors += bad;
                received[id] = {begin, detailed ? stamp() : 0, true};
                last_id = id;
                ++consumed;
            }
            read += n;
            head.value.store(read, std::memory_order_release);
        }
        consumer_finish = stamp();
        consumer_cpu = stamp(CLOCK_THREAD_CPUTIME_ID) - cpu0;
    });
    pin(c.producer);
    while (ready.load(std::memory_order_acquire) != (c.background == "stream" ? 2U : 1U)) relax();
    const U t0 = stamp() + 2000000;
    start.store(t0, std::memory_order_release);
    until(t0);
    const U cpu0 = stamp(CLOCK_THREAD_CPUTIME_ID);
    U next = 0, written = 0, refused = 0;
    while (next < c.count) {
        const U due = t0 + next / c.burst * c.interval;
        until(due);
        const U now = stamp();
        if (now >= t0 + duration + 500000000) break;
        const U due_end = std::min(c.count, ((now - t0) / c.interval + 1) * c.burst);
        const U free = c.capacity - (written - head.value.load(std::memory_order_acquire));
        if (!free) {
            max_depth = c.capacity;
            const U refusal_size = due_end - next;
            for (; next < due_end; ++next) {
                sent[next].state = 2; sent[next].refused_at = now;
                sent[next].refusal_size = refusal_size; ++refused;
            }
            continue;
        }
        const U n = std::min({free, c.batch, due_end - next});
        const U batch_id = publications.size();
        for (U j = 0; j < n; ++j) {
            const U id = next + j;
            auto* s = slot(written + j);
            sent[id] = {detailed ? stamp() : 0, batch_id, 0, 0, 1};
            s->id = id;
            auto* payload = reinterpret_cast<U*>(s + 1);
            payload[0] = id;
            for (U k = 1; k + 1 < c.bytes / 8; ++k) payload[k] = pattern;
            payload[c.bytes / 8 - 1] = ~id;
        }
        written += n;
        const U before = detailed ? stamp() : 0;
        tail.value.store(written, std::memory_order_release);
        const U after = detailed ? stamp() : 0;
        publications.push_back({before, after});
        notify();
        max_depth = std::max(max_depth, written - head.value.load(std::memory_order_acquire));
        next += n;
    }
    producer_done.store(true, std::memory_order_release);
    notify();
    const U producer_finish = stamp();
    const U producer_cpu = stamp(CLOCK_THREAD_CPUTIME_ID) - cpu0;
    receiver.join();
    stop_background.store(true, std::memory_order_relaxed);
    if (background.joinable()) background.join();
    U checked = 0;
    std::ofstream out(c.output);
    out << "id,scheduled_ns,state,completed,refused_at_ns,refusal_batch_size,prepare_ns,publish_before_ns,publish_after_ns,consume_begin_ns,consume_end_ns\n";
    for (U id = 0; id < c.count; ++id) {
        const auto& s = sent[id];
        const auto& r = received[id];
        if (r.done) {
            ++checked;
            if (s.state != 1 || (detailed && (r.begin < t0 + id / c.burst * c.interval || r.end < r.begin))) ++errors;
        }
        const auto p = s.state == 1 ? publications[s.batch] : Publication{0, 0};
        if (detailed && r.done && (r.begin < p.before || p.before < s.prepared)) ++errors;
        out << id << ',' << id / c.burst * c.interval << ',' << unsigned(s.state) << ',' << r.done << ','
            << (s.refused_at ? std::to_string(s.refused_at - t0) : "") << ',' << s.refusal_size << ',';
        const U values[] = {s.prepared, p.before, p.after, r.begin, r.end};
        for (int k = 0; k < 5; ++k)
            out << (values[k] ? std::to_string(values[k] - t0) : "") << (k == 4 ? '\n' : ',');
    }
    if (!out) throw std::runtime_error("write samples");
    if (checked != consumed || written + refused != next || next > c.count || max_depth > c.capacity) ++errors;
    std::cout << "{\"offered\":" << c.count << ",\"admitted\":" << written
              << ",\"refused\":" << refused << ",\"unattempted\":" << c.count - next
              << ",\"completed\":" << consumed << ",\"unfinished\":" << c.count - refused - consumed
              << ",\"errors\":" << errors << ",\"producer_cpu_ns\":" << producer_cpu
              << ",\"consumer_cpu_ns\":" << consumer_cpu << ",\"background_cpu_ns\":" << background_cpu
              << ",\"background_bytes\":" << background_bytes << ",\"background_checksum\":" << background_sum
              << ",\"independent_ops\":" << work << ",\"independent_checksum\":" << work_state
              << ",\"empty_polls\":" << empty_polls << ",\"wait_calls\":" << wake_calls
              << ",\"publications\":" << publications.size() << ",\"max_observed_depth\":" << max_depth
              << ",\"clock_pair_p50_ns\":" << clock_pairs[4999] << ",\"clock_pair_p99_ns\":" << clock_pairs[9899]
              << ",\"ring_bytes\":" << stride * c.capacity << ",\"last_arrival_ns\":" << duration
              << ",\"offer_window_ns\":" << offer_window
              << ",\"producer_finish_ns\":" << producer_finish - t0
              << ",\"consumer_finish_ns\":" << consumer_finish - t0 << "}\n";
    return errors ? 2 : 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
