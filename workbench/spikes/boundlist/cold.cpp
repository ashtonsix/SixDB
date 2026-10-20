#include "checkpoint-phase.h"
#include "boundlist.h"
#include "geometry.h"
#include "fused.h"
#include "scan-count.h"
#include <ikea/boundlist/author/search.h>
#include <ikea/streampack.h>
#include <loom/ring.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace bl = boundlist_spike;
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;
using word = std::uint64_t;
inline word mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
template <class R> auto take(R&& r) { assert(r); return std::move(*r); }
struct runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    runtime() {
        assert(loom::adopt(worker)); arena = take(oa::open_arena(1ull << 30));
        assert(loom::configure(worker, arena, 0));
    }
    ~runtime() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto p = loom::shutdown(worker);
            if (p.kind() == orbital::Progress::Kind::done) break;
            assert(p.kind() != orbital::Progress::Kind::failed && std::chrono::steady_clock::now() < until);
            loom::poll(worker, {256, 0}); std::this_thread::yield();
        }
        assert(!arena->charged_bytes.load()); loom::relinquish(worker);
    }
};
struct alignas(64) node {
    std::uint16_t checkpoints[16];
    std::uint16_t count;
    std::array<sp::byte, 6> extent_slot{};
    const std::uint16_t* ends;
    const std::uint8_t* widths;
    word padding{};
};
static_assert(sizeof(node) == 64);
struct alignas(64) child { word salt; std::array<word, 7> padding{}; };
struct fixture {
    runtime& rt;
    unsigned nodes, children, span;
    bool mixed;
    oa::Allocation headers_storage, ends_storage, widths_storage, child_storage;
    node* headers;
    std::uint16_t* endpoints;
    std::uint8_t* widths;
    child* values;
    std::array<sp::byte, 8192> shared_payload{}; // Real readable control extent; never consumed here.
    sp::layout format = take(sp::layout::prepare({sp::geometry::direct, sp::placement::grouped, 255, 65280, {}, true, true}));
    sp::layout width_format = take(sp::layout::prepare({sp::geometry::width, sp::placement::grouped, 255, 65280, {}, true, true}));
    fixture(runtime& r, unsigned n, bool variable, unsigned range_span)
        : rt(r), nodes(n), children(std::min(1u << 21, std::bit_ceil(n * 257))), span(range_span), mixed(variable) {
        headers_storage = take(loom::allocate(rt.worker, {.bytes = std::size_t(nodes) * sizeof(node), .alignment = 64}));
        ends_storage = take(loom::allocate(rt.worker, {.bytes = std::size_t(nodes) * 512, .alignment = 64}));
        widths_storage = take(loom::allocate(rt.worker, {.bytes = std::size_t(nodes) * 256, .alignment = 64}));
        child_storage = take(loom::allocate(rt.worker, {.bytes = std::size_t(children) * sizeof(child), .alignment = 64}));
        headers = reinterpret_cast<node*>(headers_storage.address);
        endpoints = reinterpret_cast<std::uint16_t*>(ends_storage.address);
        widths = reinterpret_cast<std::uint8_t*>(widths_storage.address);
        values = reinterpret_cast<child*>(child_storage.address);
        for (unsigned i = 0; i < nodes; ++i) {
            auto& h = *std::construct_at(headers + i);
            h.count = variable ? 64 + mix(i + 713) % 193 : 256; h.ends = endpoints + std::size_t(i) * 256;
            h.widths = widths + std::size_t(i) * 256;
            const unsigned extent = h.count * 32 - 1; std::memcpy(h.extent_slot.data(), &extent, 4);
            for (unsigned j = 0; j < 16; ++j) h.checkpoints[j] = j ? j * 512 - 1 : 0;
            for (unsigned j = 0; j < 256; ++j) std::construct_at(endpoints + std::size_t(i) * 256 + j, (j + 1) * 32 - 1);
            for (unsigned j = 0; j < 256; ++j) std::construct_at(widths + std::size_t(i) * 256 + j, j >= h.count ? 0 : j ? 32 : 31);
            if (!(i % 1024)) { assert(stream(h).validate()); assert(stream(h, true).validate()); }
        }
        for (unsigned i = 0; i < children; ++i) std::construct_at(values + i, child{mix(i + 0x3917), {}});
    }
    ~fixture() { loom::release(child_storage); loom::release(widths_storage); loom::release(ends_storage); loom::release(headers_storage); }
    [[gnu::always_inline]] sp::const_view stream(const node& h, bool width = false) const {
        if (width) return sp::const_view::assume_valid(width_format, {shared_payload,
            {reinterpret_cast<const sp::byte*>(h.checkpoints), 32}, {reinterpret_cast<const sp::byte*>(h.widths), 256},
            {reinterpret_cast<const sp::byte*>(&h) + offsetof(node, count), 6}});
        return sp::const_view::assume_valid(format, {shared_payload, {reinterpret_cast<const sp::byte*>(h.ends), 512}, {},
            {reinterpret_cast<const sp::byte*>(&h.count), 2}});
    }
    unsigned child_id(unsigned n, unsigned rank) const { return ((n * 0x9e3779b9u) ^ rank) & (children - 1); }
    const child* selected(unsigned n, unsigned rank) const { return values + child_id(n, rank); }
    static constexpr unsigned domain = 256 * 32 + 17;
};
template <unsigned Method> [[gnu::always_inline]] unsigned locate(const fixture& f, const node& h, unsigned point, unsigned group = 0) {
    const auto q = std::uint16_t(point);
    if constexpr (Method == 0 || Method == 6) {
        if (!point) return 0;
        const auto extent = [&] { if constexpr (Method == 0) return unsigned(bl::read_element(h.ends, h.count - 1));
            else { unsigned value; std::memcpy(&value, h.extent_slot.data(), 4); return value; } }();
        if (point > extent) return h.count;
        const auto source = f.stream(h, Method == 6);
        const sp::author::access<Method == 6 ? sp::geometry::width : sp::geometry::direct, std::uint8_t, std::uint16_t, const sp::byte> a(source);
        unsigned rank; [[clang::always_inline]] rank = a.reverse(point - 1).rank; return rank;
    } else if constexpr (Method == 1) return bl::hybrid_array<bl::edge::at_or_after, true>(h.ends, h.count, q);
    else if constexpr (Method == 2) return boundlist_count_control::scan<true>(h.ends, h.count, q);
    else if constexpr (Method == 3) return boundlist_fused::flat<true>(h.ends, h.count, q);
    else if constexpr (Method == 5) {
        const auto base = unsigned(h.checkpoints[group]);
        const auto relative = point - base;
        if (relative > 4080) return h.count;
        unsigned within = 0;
        if (relative) [[clang::always_inline]] within = ikea::boundlist::detail::owner16_native(h.widths + group * 16, relative - 1);
        return std::min(unsigned(h.count), group * 16 + within);
    } else {
        const auto first = group * 16, take = std::min(16u, unsigned(h.count) - first);
        const auto mask = bl::native_mask<bl::edge::at_or_after>(h.ends + first, take, q);
        return mask ? first + std::countr_zero(mask) : unsigned(h.count);
    }
}
[[gnu::always_inline]] unsigned guide(const node& h, unsigned point) {
    // The paid guide owns sixteen initialized checkpoints, even for partial
    // nodes. Future checkpoints are only hints; clamp the selected actual group.
    const auto mask = bl::native_mask<bl::edge::at_or_after>(h.checkpoints, 16, std::uint16_t(point));
    const auto next = mask ? std::countr_zero(mask) : 16;
    return std::min(unsigned(next ? next - 1 : 0), (unsigned(h.count) - 1) / 16);
}
using width_lookup = decltype(std::declval<ikea::boundlist::width_source<std::uint8_t,std::uint16_t>>().template prepare<ikea::boundlist::edge::at_or_after>(0u));
struct no_routing {};
#ifndef BOUNDLIST_COMPACT_FRAME
#define BOUNDLIST_COMPACT_FRAME 0
#endif
struct frame_result { const child *first,*last; bool absent; };
template<unsigned Method> struct alignas(BOUNDLIST_COMPACT_FRAME?64:8) frame {
    word salt;
    unsigned node_id, low, high, count, left;
#if BOUNDLIST_COMPACT_FRAME
    [[no_unique_address]] std::conditional_t<Method==4 || Method==5,std::array<unsigned,2>,no_routing> groups;
    union handoff {
        const node* source;
        std::conditional_t<Method>=7,std::array<width_lookup,2>,no_routing> phases;
        frame_result result;
        handoff():source{} {}
    } state;
    void set_source(const node* source) { ::new (static_cast<void*>(&state.source)) const node*(source); }
    const node* source() const { return state.source; }
    auto& routing() {
        if constexpr(Method>=7) return state.phases;
        else return groups;
    }
    template<bool Point> void set_phases(width_lookup low,width_lookup high) {
        ::new (static_cast<void*>(&state.phases)) std::array<width_lookup,2>;
        state.phases[0]=low;
        if constexpr(!Point) state.phases[1]=high;
    }
    void set_result(frame_result result) { ::new (static_cast<void*>(&state.result)) frame_result(result); }
    const auto& result() const { return state.result; }
#else
    [[no_unique_address]] std::conditional_t<Method>=7,std::array<width_lookup,2>,
        std::conditional_t<Method==4 || Method==5,std::array<unsigned,2>,no_routing>> routing_;
    const node* source_{};
    frame_result result_{};
    void set_source(const node* source) { source_=source; }
    const node* source() const { return source_; }
    auto& routing() { return routing_; }
    template<bool Point> void set_phases(width_lookup low,width_lookup high) {
        routing_[0]=low;
        if constexpr(!Point) routing_[1]=high;
    }
    void set_result(frame_result result) { result_=result; }
    const auto& result() const { return result_; }
#endif
};
#if BOUNDLIST_COMPACT_FRAME
static_assert(sizeof(frame<5>)==64 && sizeof(frame<7>)==64 && sizeof(frame<0>)==64);
#endif
template<class Frame> [[gnu::always_inline]] inline void prepare(const fixture& f, Frame& x, bool point = false) {
    x.node_id = unsigned(x.salt) & (f.nodes - 1);
    x.low = unsigned((__uint128_t(x.salt) * fixture::domain) >> 64);
    if (!point) x.high = std::min(fixture::domain - 1, x.low + f.span - 1);
    x.set_source(f.headers + x.node_id);
}
template <unsigned Method, bool Point> [[gnu::always_inline]] void bind(const fixture& f,frame<Method>& x) {
    x.count = x.source()->count;
    if constexpr (Method == 4 || Method == 5) {
        x.routing()[0] = guide(*x.source(), x.low);
        if constexpr (!Point) x.routing()[1] = guide(*x.source(), x.high);
    } else if constexpr(Method>=7) {
        unsigned extent; std::memcpy(&extent,x.source()->extent_slot.data(),4);
        const ikea::boundlist::width_source<std::uint8_t,std::uint16_t> bounds{
            x.source()->widths,x.source()->checkpoints,x.count,16,std::uint16_t(extent)};
        auto select=[&](unsigned q) __attribute__((always_inline)) {
            if constexpr(Method==7) return bounds.prepare<ikea::boundlist::edge::at_or_after>(q);
            else if constexpr(Method==10) {
                const auto view=f.stream(*x.source(),true);
                const sp::author::access<sp::geometry::width,std::uint8_t,std::uint16_t,const sp::byte> access(view);
                return access.prepare<ikea::boundlist::edge::at_or_after>(q);
            } else return checkpoint_spike::prepare<Method==9,ikea::boundlist::edge::at_or_after>(bounds,q);
        };
        const auto low=select(x.low);
        const auto high=[&]() __attribute__((always_inline)) { if constexpr(Point) return width_lookup{}; else return select(x.high); }();
        x.template set_phases<Point>(low,high);
    }
}
template <unsigned Method, bool Point> [[gnu::always_inline]] void resolve(const fixture& f, frame<Method>& x) {
    const auto first = [&]() __attribute__((always_inline)) {
        if constexpr(Method>=7) return x.routing()[0].finish();
        else if constexpr(Method==4 || Method==5) return locate<Method>(f,*x.source(),x.low,x.routing()[0]);
        else return locate<Method>(f,*x.source(),x.low);
    }();
    const auto last=[&]() __attribute__((always_inline)) {
        if constexpr(Point)return 0u;
        else return std::min(x.count-1,[&]() __attribute__((always_inline)) {
            if constexpr(Method>=7) return x.routing()[1].finish();
            else if constexpr(Method==4 || Method==5) return locate<Method>(f,*x.source(),x.high,x.routing()[1]);
            else return locate<Method>(f,*x.source(),x.high);
        }());
    }();
    const bool absent=first==x.count;
    const auto* a=absent?nullptr:f.selected(x.node_id,first);
    const auto* b=[&] { if constexpr(Point)return static_cast<const child*>(nullptr);
        else return absent?nullptr:f.selected(x.node_id,last); }();
    x.set_result({a,b,absent});
}
template <bool Point,class Frame> [[gnu::always_inline]] void advance(const fixture& f, Frame& x) {
    const word a = x.result().absent ? 0xdeadc0de713ULL : x.result().first->salt;
    const word b = [&] { if constexpr (Point) return word{}; else return x.result().absent ? 0x713badd00dULL : x.result().last->salt; }();
    x.salt = mix(x.salt ^ a ^ std::rotl(b, 17)); prepare(f, x, Point);
}
template<unsigned Method> frame<Method> initial(const fixture& f, unsigned lane, unsigned rounds) {
    frame<Method> x{}; x.salt = mix(713 + lane * 0x3917); x.left = rounds; prepare(f, x); return x;
}
template <unsigned Width, unsigned Method, bool Point>
[[gnu::noinline]] word direct(const fixture& f, unsigned rounds) {
    std::array<frame<Method>, Width> frames;
    for (unsigned c = 0; c < Width; ++c) frames[c] = initial<Method>(f, c, rounds);
    for (unsigned step = 0; step < rounds; ++step)
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto one = [&](auto i) __attribute__((always_inline)) { auto& x = frames[i]; bind<Method, Point>(f,x); resolve<Method, Point>(f, x); advance<Point>(f, x); };
            (one(std::integral_constant<unsigned, I>{}), ...);
        }(std::make_index_sequence<Width>{});
    word result = 0; for (const auto& x : frames) result ^= x.salt; return result;
}
struct demands { loom::DemandClass one; };
template <class Emit> [[gnu::always_inline]] void cover(const void* address, std::size_t bytes, std::size_t line, Emit emit) {
    emit(address);
    const auto first = reinterpret_cast<std::uintptr_t>(address), limit = first + bytes;
    for (auto p = (first + line) & ~(line - 1); p < limit; p += line) emit(reinterpret_cast<const void*>(p));
}
template <unsigned Width, unsigned Method, bool Point>
[[gnu::noinline]] word manual(const fixture& f, const demands& d, unsigned rounds) {
    std::array<frame<Method>, Width> frames;
    const auto& policy = f.rt.worker.hints;
    for (unsigned c = 0; c < Width; ++c) {
        frames[c] = initial<Method>(f, c, rounds); loom::prefetch(policy, d.one, frames[c].source());
    }
    for (unsigned step = 0; step < rounds; ++step) {
        for (auto& x : frames) {
            bind<Method, Point>(f,x);
            if constexpr (Method == 4 || Method == 5) {
                const void* a = Method == 5 ? static_cast<const void*>(x.source()->widths + x.routing()[0] * 16)
                                            : static_cast<const void*>(x.source()->ends + x.routing()[0] * 16);
                loom::prefetch(policy, d.one, a);
                if constexpr (!Point) if (x.routing()[1] != x.routing()[0])
                    loom::prefetch(policy, d.one, Method == 5 ? static_cast<const void*>(x.source()->widths + x.routing()[1] * 16)
                                                                                  : static_cast<const void*>(x.source()->ends + x.routing()[1] * 16));
            } else if constexpr(Method>=7) {
                auto hint=[&](const auto& phase) __attribute__((always_inline)) {
                    const auto window=phase.local_window();
                    if(!window.empty()) cover(window.data(),window.size(),policy.profile.geometry.line_bytes,
                        [&](const void* p) __attribute__((always_inline)) { loom::prefetch(policy,d.one,p); });
                };
                hint(x.routing()[0]);
                if constexpr(!Point) if(x.routing()[1].local_window().data()!=x.routing()[0].local_window().data()) hint(x.routing()[1]);
            } else {
                const void* p = Method == 6 ? static_cast<const void*>(x.source()->widths) : static_cast<const void*>(x.source()->ends);
                cover(p, Method == 6 ? 256 : 512, policy.profile.geometry.line_bytes,
                    [&](const void* at) __attribute__((always_inline)) { loom::prefetch(policy, d.one, at); });
            }
        }
        for (auto& x : frames) {
            resolve<Method, Point>(f, x);
            if (!x.result().absent) {
                loom::prefetch(policy, d.one, x.result().first);
                if constexpr (!Point) if (x.result().last != x.result().first) loom::prefetch(policy, d.one, x.result().last);
            }
        }
        for (auto& x : frames) { advance<Point>(f, x); loom::prefetch(policy, d.one, x.source()); }
    }
    word result = 0; for (const auto& x : frames) result ^= x.salt; return result;
}
template <unsigned Method, bool Point, bool Hints> struct native {
    using Frame = frame<Method>;
    using Ring = typename loom::Ring<Frame*>::template WithShared<const fixture*, const demands*>;
    static LOOM_NATIVE_CC void header(typename Ring::Cursor ring, typename Ring::Slot* slot, const fixture* f, const demands* d, Frame* x) {
        bind<Method, Point>(*f,*x);
        if constexpr (Hints) {
            if constexpr (Method == 4 || Method == 5) {
                const void* a = Method == 5 ? static_cast<const void*>(x->source()->widths + x->routing()[0] * 16)
                                            : static_cast<const void*>(x->source()->ends + x->routing()[0] * 16);
                ring->prefetch(d->one, a);
                if constexpr (!Point) if (x->routing()[1] != x->routing()[0])
                    ring->prefetch(d->one, Method == 5 ? static_cast<const void*>(x->source()->widths + x->routing()[1] * 16)
                                                                                   : static_cast<const void*>(x->source()->ends + x->routing()[1] * 16));
            } else if constexpr(Method>=7) {
                auto hint=[&](const auto& phase) __attribute__((always_inline)) {
                    const auto window=phase.local_window();
                    if(!window.empty()) cover(window.data(),window.size(),ring->hint_policy().profile.geometry.line_bytes,
                        [&](const void* p) __attribute__((always_inline)) { ring->prefetch(d->one,p); });
                };
                hint(x->routing()[0]);
                if constexpr(!Point) if(x->routing()[1].local_window().data()!=x->routing()[0].local_window().data()) hint(x->routing()[1]);
            } else {
                const void* p = Method == 6 ? static_cast<const void*>(x->source()->widths) : static_cast<const void*>(x->source()->ends);
                cover(p, Method == 6 ? 256 : 512, ring->hint_policy().profile.geometry.line_bytes,
                    [&](const void* at) __attribute__((always_inline)) { ring->prefetch(d->one, at); });
            }
        }
        slot->function = metadata;
        [[clang::musttail]] return Ring::yield(ring, slot, f, d, x);
    }
    static LOOM_NATIVE_CC void metadata(typename Ring::Cursor ring, typename Ring::Slot* slot, const fixture* f, const demands* d, Frame* x) {
        resolve<Method, Point>(*f, *x);
        if constexpr (Hints) if (!x->result().absent) {
            ring->prefetch(d->one, x->result().first);
            if constexpr (!Point) if (x->result().last != x->result().first) ring->prefetch(d->one, x->result().last);
        }
        slot->function = payload;
        [[clang::musttail]] return Ring::yield(ring, slot, f, d, x);
    }
    static LOOM_NATIVE_CC void payload(typename Ring::Cursor ring, typename Ring::Slot* slot, const fixture* f, const demands* d, Frame* x) {
        advance<Point>(*f, *x);
        if (!--x->left) { [[clang::musttail]] return Ring::finish(ring, slot, f, d, x); }
        if constexpr (Hints) ring->prefetch(d->one, x->source());
        slot->function = header;
        [[clang::musttail]] return Ring::yield(ring, slot, f, d, x);
    }
};
template <unsigned Width, bool Point> word oracle(const fixture& f, unsigned rounds) {
    word result = 0;
    for (unsigned lane = 0; lane < Width; ++lane) {
        word salt = mix(713 + lane * 0x3917);
        for (unsigned step = 0; step < rounds; ++step) {
            const unsigned n = unsigned(salt) & (f.nodes - 1), low = unsigned((__uint128_t(salt) * fixture::domain) >> 64);
            const unsigned count = f.mixed ? unsigned(64 + mix(n + 713) % 193) : 256;
            const auto high = std::min(fixture::domain - 1, low + f.span - 1);
            const auto first = low / 32, last = std::min(count - 1, high / 32);
            const word a = first >= count ? 0xdeadc0de713ULL : mix(f.child_id(n, first) + 0x3917);
            const word b = Point ? 0 : first >= count ? 0x713badd00dULL : mix(f.child_id(n, last) + 0x3917);
            salt = mix(salt ^ a ^ std::rotl(b, 17));
        }
        result ^= salt;
    }
    return result;
}
inline volatile word observed;
template <unsigned Width, unsigned Method, bool Point, bool RingPath, bool Hints = false, bool Manual = false>
void trial(fixture& f, unsigned ops, unsigned rep) {
    using N = native<Method, Point, Hints>; using R = typename N::Ring;
    const auto rounds = std::max(1u, ops / Width);
    const auto expected = oracle<Width, Point>(f, rounds);
    demands d{take(loom::bind_demand({loom::MemoryPattern::irregular, 1}, 1))};
    R ring;
    std::array<frame<Method>, Width> frames;
    const auto setup_begin = std::chrono::steady_clock::now();
    if constexpr (RingPath) assert(ring.configure(f.rt.worker, {Width, 0}, {&f, &d}));
    const auto setup_end = std::chrono::steady_clock::now();
    const auto begin = std::chrono::steady_clock::now();
    word result;
    if constexpr (Manual) result = manual<Width, Method, Point>(f, d, rounds);
    else if constexpr (!RingPath) result = direct<Width, Method, Point>(f, rounds);
    else {
        for (unsigned i = 0; i < Width; ++i) {
            frames[i] = initial<Method>(f, i, rounds);
            const auto dependency = Hints ? loom::MemoryDependency{frames[i].source(), d.one} : loom::MemoryDependency{};
            assert(ring.push(N::header, {&frames[i]}, dependency));
        }
        while (ring.pending()) ring.spin({4096});
        result = 0; for (const auto& x : frames) result ^= x.salt;
    }
    const auto end = std::chrono::steady_clock::now();
    observed = result; assert(result == expected);
    const auto counters = [&] { if constexpr (RingPath) return ring.counters(); else return loom::RingCounters{}; }();
    const auto scheduler_bytes = RingPath ? ring.storage_bytes() : 0;
    const auto close_begin = std::chrono::steady_clock::now();
    if constexpr (RingPath) assert(ring.close());
    const auto close_end = std::chrono::steady_clock::now();
    constexpr const char* names[] = {"current_streampack_max_adapter", "select_window", "fused_count_scan", "fused_shortcircuit_scan", "paid_inline_guide", "width_checkpoint_window", "current_streampack_width_adapter", "production_width_phase", "exact_scan_phase", "padded_scan_phase", "production_sidecar_phase"};
    auto ns = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count(); };
    std::printf("%u,%u,%u,%s,%s,%u,%s,%u,%.6f,%lld,%lld,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu\n", f.nodes, unsigned(f.mixed), f.span,
        names[Method], Manual ? "manual_hints" : RingPath ? (Hints ? "loom_hints" : "loom_no_hints") : "direct", Width,
        Point ? "point_child" : "range_children", rep, double(ns(begin, end)) / (rounds * Width),
        (long long)ns(setup_begin, setup_end), (long long)ns(close_begin, close_end),
        std::size_t(f.nodes) * 512, std::size_t(f.nodes) * 256, std::size_t(f.nodes) * sizeof(node), std::size_t(f.children) * sizeof(child),
        sizeof(frames), scheduler_bytes,
        (unsigned long long)counters.visits, (unsigned long long)result);
}
template <bool Point> void panel(fixture& f, unsigned ops, unsigned reps) {
    for (unsigned rep = 0; rep < reps; ++rep) for (unsigned i = 0; i < 11; ++i) {
        auto run = [&]<unsigned Method>() {
            trial<1, Method, Point, false>(f, ops, rep); trial<8, Method, Point, false>(f, ops, rep);
            if(std::getenv("BOUNDLIST_COLD_SHORT_PANEL")) {
                trial<16,Method,Point,true,true>(f,ops,rep);
                return;
            }
            trial<8, Method, Point, true>(f, ops, rep); trial<8, Method, Point, true, true>(f, ops, rep);
            trial<16, Method, Point, true, true>(f, ops, rep); trial<32, Method, Point, true, true>(f, ops, rep);
            trial<8, Method, Point, false, true, true>(f, ops, rep);
            trial<16, Method, Point, false, true, true>(f, ops, rep);
            trial<32, Method, Point, false, true, true>(f, ops, rep);
        };
        const auto selected=rep%2?10-i:i;
        if(std::getenv("BOUNDLIST_COLD_PRODUCTION_ONLY") && selected<5)continue;
        if(std::getenv("BOUNDLIST_COLD_FINAL_ONLY") && selected!=5 && selected!=7 && selected!=9 && selected!=10)continue;
        switch (selected) {
        case 0: run.template operator()<0>(); break; case 1: run.template operator()<1>(); break;
        case 2: run.template operator()<2>(); break; case 3: run.template operator()<3>(); break;
        case 4: run.template operator()<4>(); break;
        case 5: run.template operator()<5>(); break; case 6: run.template operator()<6>(); break;
        case 7: run.template operator()<7>(); break;
        case 8: run.template operator()<8>(); break;case 9: run.template operator()<9>(); break;case 10: run.template operator()<10>();break;
        }
    }
}
int main(int argc, char** argv) {
    if (argc != 7) return 2;
    const unsigned nodes = std::stoul(argv[1]), mixed = std::stoul(argv[2]), span = std::stoul(argv[3]);
    const bool point = std::stoul(argv[4]); const unsigned ops = std::stoul(argv[5]), reps = std::stoul(argv[6]);
    if (!std::has_single_bit(nodes) || nodes > 524288 || mixed > 1 || !span || span > fixture::domain || !ops || !reps) return 2;
    runtime rt; fixture f(rt, nodes, mixed, span);
    std::puts("nodes,mixed,span,implementation,execution,width,metric,rep,ns,setup_ns,close_ns,end_bytes,width_bytes,header_bytes,child_bytes,frame_bytes,scheduler_bytes,ring_visits,checksum");
    if (point) panel<true>(f, ops, reps); else panel<false>(f, ops, reps);
}
