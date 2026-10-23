#include "implementation_loom.h"
#include <ikea/streampack/author/chain.h>
#include <loom/ring.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace streampack_implementation::loom_consumer {
namespace {
namespace sp = ikea::streampack;
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "StreamPack Loom consumer: %s\n", why); std::abort(); }
}
std::uint64_t ns(Clock::time_point first, Clock::time_point last) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(last - first).count();
}

template<class Access> struct Frame {
    Access access;
    Request request;
    const sp::byte* payload;
    unsigned assigned;
    loom::DemandClass demand;
    const loom::HintPolicy* policy{};
    Outcome output;
    unsigned row{}, left{};
    std::uint64_t digest{};
    std::uintptr_t offset{}, length{};
    Frame *next{}, *previous{};
    const void* dependency{};
    bool geometry{true};

    Frame(Access a, Request r, loom::DemandClass d)
        : access(a), request(r), payload(a.source().windows().payload.data()),
          assigned(a.size()), demand(d) {}
    void reset(const loom::HintPolicy& p) {
        policy = &p; row = request.rank; left = request.steps;
        digest = request.identity * 0x9e3779b97f4a7c15ULL;
        output = {digest, row, 0, 0, Code::ok};
        offset = length = 0; next = previous = nullptr; geometry = true;
        dependency = nullptr;
    }
};

template<class Access>
[[gnu::always_inline]] inline void finish(Frame<Access>& f, unsigned row, unsigned left,
    std::uint64_t digest, Code code) noexcept {
    f.output = {digest, row, f.request.steps - left, f.output.completions + 1, code};
}
struct Geometry { sp::location value{}; bool live{}; };
template<class Access>
[[gnu::always_inline]] inline Geometry geometry_body(Frame<Access>& f, unsigned row,
    unsigned left, std::uint64_t digest) noexcept {
    if (row >= f.assigned) { finish(f, row, left, digest, Code::rank); return {}; }
    if (!f.access.live(row)) { finish(f, row, left, digest, Code::retired); return {}; }
    const auto p = f.access.locate(row);
    if (p.length < 12) { finish(f, row, left, digest, Code::short_value); return {}; }
    return {p, true};
}
struct Advance { unsigned row, left; std::uint64_t digest; bool done; };
template<class Access>
[[gnu::always_inline]] inline Advance payload_body(Frame<Access>& f, unsigned row,
    unsigned left, std::uint64_t digest, std::uintptr_t offset, std::uintptr_t length) noexcept {
    const auto* value = f.payload + offset;
    std::uint64_t key;
    unsigned next;
    static_assert(sizeof(next) == 4);
    std::memcpy(&key, value, 8);
    std::memcpy(&next, value + length - 4, 4);
    digest = std::rotl(digest, 9) ^ key ^ (length * 0x94d049bb133111ebULL) ^ row;
    --left;
    const bool invalid = next >= f.assigned;
    if (invalid || !left) finish(f, next, left, digest, invalid ? Code::invalid_link : Code::ok);
    return {next, left, digest, invalid || !left};
}

// Force the actual emission callable inline. The earlier caller's outlined
// lambda hid the inline policy API and added ordinary helper calls and frames.
struct MetadataHints {
    const loom::HintPolicy& policy;
    loom::DemandClass demand;
    [[gnu::always_inline]] void operator()(sp::bytes window) const noexcept {
        loom::prefetch(policy, demand, window.data());
    }
};
template<bool Hints, class Access>
[[gnu::always_inline]] inline void metadata_hints(const Frame<Access>& f,
    const loom::HintPolicy& policy, unsigned row) noexcept {
    if constexpr (Hints)
        [[clang::always_inline]] f.access.visit_value_metadata(row, MetadataHints{policy, f.demand});
}
template<bool Hints, class Access>
[[gnu::always_inline]] inline void payload_hints(const Frame<Access>& f,
    const loom::HintPolicy& policy, std::uintptr_t offset, std::uintptr_t length) noexcept {
    if constexpr (Hints) {
        // The first8 identity and final4 successor are the actual demand loads.
        loom::prefetch(policy, f.demand, f.payload + offset);
        loom::prefetch(policy, f.demand, f.payload + offset + length - 4);
    }
}
template<bool Hints, class Access> const void* first_dependency(Frame<Access>& f) {
    if constexpr (Hints) {
        if (f.row < f.assigned) f.access.visit_value_metadata(f.row, [&](sp::bytes window) {
            if (!f.dependency) f.dependency = window.data();
        });
    }
    return f.dependency;
}
template<bool Hints, class Access> void admission_hint(const Frame<Access>& f,
    const loom::HintPolicy& policy) {
    if constexpr (Hints) if (f.dependency) loom::prefetch(policy, f.demand, f.dependency);
}

template<class Access, bool Hints> struct Native {
    using F = Frame<Access>;
    using Ring = loom::Ring<F*, unsigned, unsigned, std::uint64_t, std::uintptr_t, std::uintptr_t>;
    static LOOM_NATIVE_CC void geometry(typename Ring::Cursor ring, typename Ring::Slot* slot, F* f,
        unsigned row, unsigned left, std::uint64_t digest, std::uintptr_t, std::uintptr_t) {
        const auto p = geometry_body(*f, row, left, digest);
        if (!p.live) ring->complete(*slot);
        else {
            payload_hints<Hints>(*f, ring->hint_policy(), p.value.offset, p.value.length);
            slot->function = payload;
        }
        [[clang::musttail]] return Ring::yield(ring, slot, f, row, left, digest,
            std::uintptr_t(p.value.offset), std::uintptr_t(p.value.length));
    }
    static LOOM_NATIVE_CC void payload(typename Ring::Cursor ring, typename Ring::Slot* slot, F* f,
        unsigned row, unsigned left, std::uint64_t digest, std::uintptr_t offset, std::uintptr_t length) {
        const auto next = payload_body(*f, row, left, digest, offset, length);
        if (next.done) ring->complete(*slot);
        else {
            metadata_hints<Hints>(*f, ring->hint_policy(), next.row);
            slot->function = geometry;
        }
        [[clang::musttail]] return Ring::yield(ring, slot, f, next.row, next.left, next.digest, offset, length);
    }
#if defined(__aarch64__) || defined(__AVX2__)
    using Chain = sp::author::point_chain<4>;
    static typename Chain::result chain_geometry(void* binding, std::size_t row, bool,
        sp::author::pipeline_location, unsigned) {
        auto& f = *static_cast<F*>(binding);
        const auto p = geometry_body(f, row, f.left, f.digest);
        if (p.live) payload_hints<Hints>(f, *f.policy, p.value.offset, p.value.length);
        return {sp::author::pipeline_location::from(p.value), p.live, !p.live};
    }
    static typename Chain::result chain_payload(void* binding, std::size_t row, bool,
        sp::author::pipeline_location p, unsigned) {
        auto& f = *static_cast<F*>(binding);
        const auto next = payload_body(f, row, f.left, f.digest, p.offset(), p.length());
        f.row = next.row; f.left = next.left; f.digest = next.digest;
        if (!next.done) metadata_hints<Hints>(f, *f.policy, next.row);
        return {p, true, true};
    }
    static void chain_finish(void*, std::size_t, bool, sp::author::pipeline_location) {}
#endif
};

// Private matched control: same circular rotation and in-place replacement by
// overflow as Ring, but state stays in ordinary caller frames without tail ABI.
template<class Access, bool Hints> struct Manual {
    using F = Frame<Access>;
    F* cursor{};
    std::array<F*, 64> overflow{};
    unsigned active{}, head{}, queued{}, width{}, overflow_limit{};
    const loom::HintPolicy& policy;
    Sample counts;

    Manual(unsigned w, unsigned o, const loom::HintPolicy& p) : width(w), overflow_limit(o), policy(p) {}
    void append(F& f) {
        if (!cursor) { f.next = f.previous = &f; cursor = &f; }
        else {
            f.next = cursor; f.previous = cursor->previous;
            f.previous->next = &f; cursor->previous = &f;
        }
    }
    bool push(F& f) {
        if (active + queued == width + overflow_limit) { ++counts.refused; return false; }
        ++counts.accepted;
        if (active < width) { append(f); ++active; admission_hint<Hints>(f, policy); }
        else { overflow[(head + queued) % overflow_limit] = &f; ++queued; }
        return true;
    }
    void retire(F& f) {
        --active; ++counts.completed;
        if (queued) {
            auto* incoming = overflow[head]; head = (head + 1) % overflow_limit; --queued;
            if (f.next == &f) incoming->next = incoming->previous = incoming;
            else {
                incoming->next = f.next; incoming->previous = f.previous;
                incoming->previous->next = incoming; incoming->next->previous = incoming;
            }
            if (cursor == &f) cursor = incoming;
            ++active; ++counts.promoted; admission_hint<Hints>(*incoming, policy);
        } else if (f.next == &f) cursor = nullptr;
        else {
            f.previous->next = f.next; f.next->previous = f.previous;
            if (cursor == &f) cursor = f.next;
        }
    }
    std::uint64_t spin(unsigned visits) {
        std::uint64_t visited = 0;
        while (active && visited < visits) {
            auto& f = *cursor; cursor = f.next;
            ++visited;
            if (f.geometry) {
                const auto p = geometry_body(f, f.row, f.left, f.digest);
                if (!p.live) retire(f);
                else {
                    f.offset = p.value.offset; f.length = p.value.length; f.geometry = false;
                    payload_hints<Hints>(f, policy, f.offset, f.length);
                }
            } else {
                const auto next = payload_body(f, f.row, f.left, f.digest, f.offset, f.length);
                f.row = next.row; f.left = next.left; f.digest = next.digest;
                if (next.done) retire(f);
                else { metadata_hints<Hints>(f, policy, f.row); f.geometry = true; }
            }
        }
        counts.stage_visits += visited;
        return visited;
    }
    unsigned pending() const { return active + queued; }
};

template<class Access, bool Hints>
[[gnu::noinline]] Report typed(loom::Worker& worker, Access prototype,
    std::span<const sp::const_view> sources, std::span<const Request> requests,
    Options options, Boundary boundary, void* context, Clock::time_point setup_begin) {
    using F = Frame<Access>;
    using N = Native<Access, Hints>;
    const auto demand = loom::bind_demand({loom::MemoryPattern::irregular, 1}, 1);
    require(bool(demand), "qualified first-byte demand");
    const auto policy = worker.hints;
    std::vector<F> frames;
    frames.reserve(requests.size());
    for (const auto request : requests) {
        require(request.source < sources.size(), "source admission");
        frames.emplace_back(prototype.rebind_unchecked(sources[request.source]), request, *demand);
    }
    typename N::Ring ring;
    if (options.route == Route::ring) require(bool(ring.configure(worker, {options.width, options.overflow})), "Ring configuration");
#if defined(__aarch64__) || defined(__AVX2__)
    const std::array<typename N::Chain::function, 2> stages{
        N::Chain::template stage<N::chain_geometry>, N::Chain::template stage<N::chain_payload>};
    const auto chain = N::Chain::make(stages, N::Chain::template completion<N::chain_finish>);
    require(bool(chain), "production point-chain preparation");
#else
    require(options.route != Route::point_chain, "point chain requires its native ISA");
#endif
    Report report;
    report.outcomes.resize(requests.size()); report.samples.reserve(options.repetitions);
    report.frame_bytes = frames.capacity() * sizeof(F) + report.outcomes.capacity() * sizeof(Outcome);
    report.scheduler_bytes = options.route == Route::ring ? ring.storage_bytes() :
        options.route == Route::manual ? sizeof(Manual<Access, Hints>) : 0;
    report.setup_ns = ns(setup_begin, Clock::now());

    for (unsigned epoch = 0; epoch < options.repetitions + unsigned(options.warmup); ++epoch) {
        Sample sample;
        const auto before = ring.counters();
        Manual<Access, Hints> manual(options.width, options.overflow, policy);
        const auto begin = Clock::now();
        if (options.route == Route::direct || options.route == Route::point_chain) {
            for (auto& f : frames) {
                f.reset(policy);
                if (!f.left) finish(f, f.row, 0, f.digest, Code::ok);
                else {
                    ++sample.accepted;
                    first_dependency<Hints>(f); admission_hint<Hints>(f, policy);
                    while (!f.output.completions) {
                        ++sample.stage_visits;
#if defined(__aarch64__) || defined(__AVX2__)
                        if (options.route == Route::point_chain) {
                            const auto consumed_before = f.request.steps - f.left;
                            chain->run(&f, f.row, true, {{0, 0}});
                            sample.stage_visits += (f.request.steps - f.left) != consumed_before;
                            continue;
                        }
#endif
                        const auto p = geometry_body(f, f.row, f.left, f.digest);
                        if (!p.live) break;
                        payload_hints<Hints>(f, policy, p.value.offset, p.value.length);
                        ++sample.stage_visits;
                        const auto next = payload_body(f, f.row, f.left, f.digest, p.value.offset, p.value.length);
                        f.row = next.row; f.left = next.left; f.digest = next.digest;
                        if (!next.done) metadata_hints<Hints>(f, policy, f.row);
                    }
                }
                ++sample.completed;
                ++sample.quanta;
                if (boundary) boundary(context, sample.quanta, sample.completed);
            }
        } else {
            unsigned next = 0;
            std::uint64_t immediate_completed = 0;
            const auto pending = [&] { return options.route == Route::ring ? ring.pending() : manual.pending(); };
            const auto fill = [&] {
                const auto start = next;
                while (next < frames.size()) {
                    auto& f = frames[next]; f.reset(policy);
                    if (!f.left) {
                        finish(f, f.row, 0, f.digest, Code::ok);
                        ++immediate_completed; ++next; continue;
                    }
                    const auto* address = first_dependency<Hints>(f);
                    bool accepted;
                    if (options.route == Route::ring) {
                        const loom::MemoryDependency dependency{address, address ? f.demand : loom::DemandClass{}};
                        accepted = ring.push(N::geometry, {&f, f.row, f.left, f.digest, 0, 0}, dependency);
                    } else accepted = manual.push(f);
                    if (!accepted) break;
                    ++next;
                }
                return next != start;
            };
            fill();
            // A returned zero-visit budget preserves all accepted borrowers.
            if (options.route == Route::ring) require(!ring.spin({0}), "zero-budget native dispatch");
            else require(!manual.spin(0), "zero-budget manual dispatch");
            while (pending()) {
                if (options.route == Route::ring) require(ring.spin({options.quantum}) != 0, "native progress");
                else require(manual.spin(options.quantum) != 0, "manual progress");
                ++sample.quanta;
                const auto completed = immediate_completed + (options.route == Route::ring ?
                    ring.counters().completed - before.completed : manual.counts.completed);
                if (boundary) boundary(context, sample.quanta, completed);
                if (fill()) ++sample.refill_boundaries;
            }
            require(next == frames.size(), "complete request admission and drain");
            if (options.route == Route::ring) {
                const auto after = ring.counters();
                sample.accepted = after.accepted - before.accepted;
                sample.refused = after.refused - before.refused;
                sample.completed = immediate_completed + after.completed - before.completed;
                sample.promoted = after.promoted - before.promoted;
                sample.stage_visits = after.visits - before.visits;
            } else {
                sample.accepted = manual.counts.accepted; sample.refused = manual.counts.refused;
                sample.completed = immediate_completed + manual.counts.completed; sample.promoted = manual.counts.promoted;
                sample.stage_visits = manual.counts.stage_visits;
            }
        }
        // Result delivery belongs to the measured operation, not its oracle.
        for (unsigned i = 0; i < frames.size(); ++i) {
            report.outcomes[i] = frames[i].output;
            sample.consumed += frames[i].output.consumed;
        }
        sample.elapsed_ns = ns(begin, Clock::now());
        for (const auto& f : frames) require(f.output.completions == 1, "exactly-once result delivery");
        if (epoch >= unsigned(options.warmup)) report.samples.push_back(sample);
    }
    const auto close_begin = Clock::now();
    require(bool(ring.close()), "drained Ring close");
    std::vector<F>().swap(frames);
    report.close_ns = ns(close_begin, Clock::now());
    return report;
}
} // namespace

Report assess(loom::Worker& worker, std::span<const sp::const_view> sources,
    std::span<const Request> requests, Options options, Boundary boundary, void* context) {
    const auto setup_begin = Clock::now();
    require(!sources.empty(), "named source population");
    require(options.width && options.width <= 64 && options.overflow <= 64 &&
        options.quantum && options.repetitions && options.repetitions <= 1000, "bounded execution options");
    for (const auto& source : sources)
        require(&source.format() == &sources.front().format(), "shared stable layout identity");
    return sp::author::visit(sources.front(), [&](auto prototype) {
        if (options.hints) return typed<decltype(prototype), true>(worker, prototype, sources, requests, options, boundary, context, setup_begin);
        return typed<decltype(prototype), false>(worker, prototype, sources, requests, options, boundary, context, setup_begin);
    });
}
} // namespace streampack_implementation::loom_consumer
