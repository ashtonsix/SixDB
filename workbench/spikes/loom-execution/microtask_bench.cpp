#include "microtask.h"
#include "support.h"
#include "../bec256-composition/consumer.h"

#include <ikea/bec/author/native.h>
#include <ikea/bec/bec256.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace loom_spike {
namespace {
namespace mt = microtask;
namespace bc = ikea::bec256;
namespace pf = prefetch;
enum class HintPath { legacy, specialized, runtime };

struct alignas(64) Node {
    std::uint64_t next, value;
    std::array<std::uint64_t, 6> padding{};
};
struct Entry {
    std::uint16_t population, length;
};
enum class Workload { chains, ranges, bec256 };
struct Dataset {
    Workload workload;
    std::vector<Node> nodes;
    std::vector<std::uint64_t> words;
    std::vector<Entry> directory;
    std::unique_ptr<bec_study::directory> material_directory;
    std::vector<bc::byte> bodies;
    std::vector<std::array<std::byte, 32>> plain;
    std::vector<std::array<std::byte, 32>> query;
    std::size_t range_lines = 0;
    std::size_t storage_bytes() const {
        return nodes.size() * sizeof(Node) + words.size() * sizeof(std::uint64_t) +
               directory.size() * sizeof(Entry) + bodies.size() +
               (plain.size() + query.size()) * sizeof(std::array<std::byte, 32>) +
               (material_directory ? material_directory->storage_bytes() : 0);
    }

    Dataset(Workload kind, std::size_t count, std::size_t range_bytes) : workload(kind) {
        std::mt19937_64 random(0x17305cd9);
        if (kind == Workload::chains) {
            nodes.resize(count);
            std::vector<std::size_t> order(count);
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), random);
            for (std::size_t i = 0; i < count; ++i) {
                nodes[order[i]].next = order[(i + 1) % count];
                nodes[order[i]].value = random();
            }
        } else if (kind == Workload::ranges) {
            range_lines = (range_bytes + 63) / 64;
            words.resize(count * range_lines * 8);
            for (auto& value : words)
                value = random();
        } else {
            plain.resize(count);
            query.resize(count);
            directory.reserve(count);
            std::vector<bec_study::entry> catalog;
            catalog.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                unsigned population = 0;
                for (unsigned j = 0; j < 32; ++j) {
                    const auto raw = i % 17 == 0 ? 0u : unsigned(random() & random() & 255u);
                    plain[i][j] = bc::byte(raw);
                    query[i][j] = bc::byte(random() & 255u);
                    population += std::popcount(raw);
                }
                const auto encoding = bc::encoding::make(plain[i], population);
                if (!encoding)
                    throw std::runtime_error("Bec256 preparation failed");
                catalog.push_back({population, encoding->bytes(), unsigned(bodies.size())});
                directory.push_back({std::uint16_t(population), std::uint16_t(encoding->bytes())});
                bodies.insert(bodies.end(), encoding->body().begin(), encoding->body().end());
            }
            bodies.resize(bodies.size() + 64); // Initialized wide read suffix.
            material_directory = std::make_unique<bec_study::directory>(
                bec_study::layout::series_local, catalog, 64);
        }
    }
};

struct Work {
    const Dataset* dataset = nullptr;
    std::uint64_t start = 0, count = 0, result = 0, expected = 0;
    unsigned grain = 1, finished = 0;
    bool alternate = false, body_phase = false, all_body_hints = false;
    std::uint64_t body_offset = 0;
    std::uint64_t second_offset = 0;
    Entry body_entry{}, second_entry{};
    pf::Binding hint;
};

std::uint64_t value_for(std::uint64_t value, std::uint64_t cursor, bool alternate) {
    return alternate ? std::rotl(value, 7) ^ cursor : value;
}

std::uint64_t reference(Work& work, bool vector_live) {
    auto cursor = work.start;
    std::uint64_t sum = 0;
    std::array<std::uint64_t, 2> additions{}, xors{};
    for (std::uint64_t i = 0; i < work.count; ++i) {
        std::uint64_t value = 0;
        const auto& data = *work.dataset;
        if (data.workload == Workload::chains) {
            value = value_for(data.nodes[cursor].value, cursor, work.alternate);
        } else if (data.workload == Workload::ranges) {
            for (unsigned word = 0; word < 8; ++word)
                value += data.words[cursor * 8 + word];
            value = value_for(value, cursor, work.alternate);
        } else {
            const auto ordinal = cursor % data.directory.size();
            const auto second = (ordinal + 17) % data.directory.size();
            for (const auto input : {ordinal, second})
                for (unsigned j = 0; j < 32; ++j)
                    value += std::popcount(std::to_integer<unsigned>(data.plain[input][j]) &
                                           std::to_integer<unsigned>(data.query[input][j]));
            value = value_for(value, ordinal, work.alternate);
        }
        sum += value;
        if (vector_live) {
            additions[0] += value;
            additions[1] += cursor;
            xors[0] ^= value;
            xors[1] ^= cursor;
        }
        cursor = data.workload == Workload::chains ? data.nodes[cursor].next : cursor + 1;
    }
    return sum + additions[0] + additions[1] + xors[0] + xors[1];
}

[[gnu::always_inline]] inline std::uint64_t count_body(const Dataset& data,
                                                     std::uint64_t ordinal,
                                                     std::uint64_t offset, Entry entry,
                                                     std::uint64_t second_offset, Entry second_entry) {
    const auto* body = data.bodies.data() + offset;
    const auto second = (ordinal + 17) % data.directory.size();
    const auto* second_body = data.bodies.data() + second_offset;
    const auto& query = data.query[ordinal];
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    // Dataset construction proved association; the final suffix makes every
    // body address readable for 64 initialized bytes, including packed neighbors.
    return bec_study::consume_pair(body, entry.population, second_body, second_entry.population,
                                   query.data(), data.query[second].data());
#else
    // Other x86 ISA profiles keep the same operation through the ordinary codec.
    std::array<std::byte, 32> decoded;
    const auto source = bc::bind_reader_unchecked({body, 64}, entry.length, entry.population);
    bc::decode(source, decoded);
    std::uint64_t result = 0;
    for (unsigned j = 0; j < 32; ++j)
        result += std::popcount(std::to_integer<unsigned>(decoded[j]) &
                               std::to_integer<unsigned>(query[j]));
    const auto source_second = bc::bind_reader_unchecked(
        {second_body, 64}, second_entry.length, second_entry.population);
    bc::decode(source_second, decoded);
    for (unsigned j = 0; j < 32; ++j)
        result += std::popcount(std::to_integer<unsigned>(decoded[j]) &
                               std::to_integer<unsigned>(data.query[second][j]));
    return result;
#endif
}

template <class Ring> struct Step {
    typename Ring::Arguments args;
    mt::Dependency dependency;
    bool done = false;
};

template <class Ring, bool Alternate, bool TypedChains = false>
[[gnu::always_inline]] inline Step<Ring> advance(typename Ring::Arguments args) {
    auto& work = *reinterpret_cast<Work*>(args.d);
    const auto& data = *work.dataset;
    if constexpr (TypedChains) {
        // The parent selected this exact native pipeline once: one dependent
        // Node per region. No runtime workload, grain or optional-hint test.
        const auto cursor = args.a;
        const auto& node = data.nodes[cursor];
        const auto value = value_for(node.value, cursor, Alternate);
        args.a = node.next;
        args.c += value;
        if constexpr (std::tuple_size_v<decltype(args.vectors)> != 0) {
            const mt::Vector pair{value, cursor};
            args.vectors[0] += pair;
            args.vectors[1] ^= pair;
        }
        --args.b;
        return {args, {&data.nodes[args.a], 64}, !args.b};
    } else {
    mt::Dependency dependency;
    unsigned consumed = 0;
    while (args.b && consumed < work.grain) {
        const auto cursor = args.a;
        std::uint64_t value;
        if (data.workload == Workload::bec256 && !work.body_phase) {
            const auto ordinal = cursor % data.directory.size();
            // Reuse the existing Ikea composition study's actual SeriesPack
            // Local population/length directory and checkpoint resolver.
            const auto first = bec_study::point<bec_study::layout::series_local>(
                *data.material_directory, unsigned(ordinal));
            const auto second = bec_study::point<bec_study::layout::series_local>(
                *data.material_directory, unsigned((ordinal + 17) % data.directory.size()));
            work.body_offset = first.offset;
            work.body_entry = {std::uint16_t(first.population), std::uint16_t(first.bytes)};
            work.second_offset = second.offset;
            work.second_entry = {std::uint16_t(second.population), std::uint16_t(second.bytes)};
            work.body_phase = true;
            dependency = {data.bodies.data() + first.offset, 64};
            return {args, dependency, false};
        }
        if (data.workload == Workload::chains) {
            value = data.nodes[cursor].value;
            args.a = data.nodes[cursor].next;
            dependency = {&data.nodes[args.a], 64};
        } else if (data.workload == Workload::ranges) {
            value = 0;
            for (unsigned word = 0; word < 8; ++word)
                value += data.words[cursor * 8 + word];
            ++args.a;
            dependency = {data.words.data() + args.a * 8,
                          std::size_t(std::min<std::uint64_t>(args.b - 1, work.grain)) * 64};
        } else {
            const auto ordinal = cursor % data.directory.size();
            value = count_body(data, ordinal, work.body_offset, work.body_entry,
                                work.second_offset, work.second_entry);
            work.body_phase = false;
            ++args.a;
            const auto next_ordinal = args.a % data.directory.size();
            dependency = {data.material_directory->population_data() + (next_ordinal / 8) * 9, 9};
        }
        value = value_for(value, data.workload == Workload::bec256 ?
                                      cursor % data.directory.size() : cursor, Alternate);
        args.c += value;
        if constexpr (std::tuple_size_v<decltype(args.vectors)> != 0) {
            const mt::Vector pair{value, cursor};
            args.vectors[0] += pair;
            args.vectors[1] ^= pair;
        }
        --args.b;
        ++consumed;
    }
    return {args, dependency, !args.b};
    }
}

template <class Ring> void finish(Work& work, const typename Ring::Arguments& args) {
    // The independent post-drain check also requires finished==1. Keeping a
    // throwing fixture guard here forces an otherwise unnecessary frame in
    // every native callback, including the nonterminal hot path.
    assert(!work.finished && "operation completed more than once");
    work.result = args.c;
    if constexpr (std::tuple_size_v<decltype(args.vectors)> != 0)
        for (const auto value : args.vectors)
            work.result += value[0] + value[1];
    ++work.finished;
}

template <class Ring, class Hint>
[[gnu::always_inline]] inline void additional_hints(const typename Ring::Arguments& args, Hint hint) {
    const auto& work = *reinterpret_cast<const Work*>(args.d);
    const auto& data = *work.dataset;
    if (!work.all_body_hints || data.workload != Workload::bec256 || !work.body_phase)
        return;
    const auto ordinal = args.a % data.directory.size();
    const auto second = (ordinal + 17) % data.directory.size();
    hint(mt::Dependency{data.bodies.data() + work.second_offset, 64});
    hint(mt::Dependency{data.query[ordinal].data(), 32});
    hint(mt::Dependency{data.query[second].data(), 32});
}

template <HintPath Path, pf::Class Kind, class Ring>
[[gnu::always_inline]] inline void ring_hint(Ring& ring, typename Ring::Slot& slot,
                                             mt::Dependency dependency, const Work& owner) {
    if constexpr (Path == HintPath::legacy)
        ring.depend(slot, dependency);
    else if constexpr (Path == HintPath::specialized)
        ring.template depend_known<Kind>(slot, dependency);
    else
        ring.depend_runtime(slot, dependency, owner.hint.kind);
}

template <HintPath Path, pf::Class Kind>
[[gnu::always_inline]] inline void ordinary_hint(mt::Dependency dependency, const Work& owner,
                                                 mt::Prefetch policy, mt::Counters& counters) {
    if constexpr (Path == HintPath::legacy)
        mt::issue_prefetch(dependency, policy, 4096, counters);
    else if constexpr (Path == HintPath::specialized)
        pf::issue<Kind>(dependency.address, owner.hint.page_bytes);
    else
        pf::issue_runtime(owner.hint.kind,
                          pf::at<pf::Class::page1>(dependency.address, owner.hint.page_bytes));
}

template <bool Alternate, HintPath Path = HintPath::legacy, pf::Class Kind = pf::Class::none, bool TypedChains = false>
LOOM_MICROTASK_CC void scalar_callback(mt::ScalarRing* ring, mt::ScalarRing::Slot* slot,
                                      std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                      std::uint64_t d) {
    const auto step = advance<mt::ScalarRing, Alternate, TypedChains>({a, b, c, d, {}});
    if (step.done) {
        finish<mt::ScalarRing>(*reinterpret_cast<Work*>(d), step.args);
        ring->complete(*slot);
    } else {
        ring_hint<Path, Kind>(*ring, *slot, step.dependency, *reinterpret_cast<Work*>(d));
        if constexpr (!TypedChains) additional_hints<mt::ScalarRing>(step.args, [&](mt::Dependency dependency) {
            ring_hint<Path, Kind>(*ring, *slot, dependency, *reinterpret_cast<Work*>(d));
        });
    }
    [[clang::musttail]] return mt::ScalarRing::yield(ring, slot, step.args.a, step.args.b,
                                                   step.args.c, step.args.d);
}

template <bool Alternate, HintPath Path = HintPath::legacy, pf::Class Kind = pf::Class::none, bool TypedChains = false>
LOOM_MICROTASK_CC void vector_callback(mt::VectorRing* ring, mt::VectorRing::Slot* slot,
                                      std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                      std::uint64_t d, mt::Vector v0, mt::Vector v1) {
    const auto step = advance<mt::VectorRing, Alternate, TypedChains>({a, b, c, d, {{v0, v1}}});
    if (step.done) {
        finish<mt::VectorRing>(*reinterpret_cast<Work*>(d), step.args);
        ring->complete(*slot);
    } else {
        ring_hint<Path, Kind>(*ring, *slot, step.dependency, *reinterpret_cast<Work*>(d));
        if constexpr (!TypedChains) additional_hints<mt::VectorRing>(step.args, [&](mt::Dependency dependency) {
            ring_hint<Path, Kind>(*ring, *slot, dependency, *reinterpret_cast<Work*>(d));
        });
    }
    [[clang::musttail]] return mt::VectorRing::yield(ring, slot, step.args.a, step.args.b,
                                                   step.args.c, step.args.d,
                                                   step.args.vectors[0], step.args.vectors[1]);
}

template <class Ring, HintPath Path = HintPath::legacy, pf::Class Kind = pf::Class::none, bool TypedChains = false>
typename Ring::Function callback(bool alternate) {
    if constexpr (std::is_same_v<Ring, mt::ScalarRing>)
        return alternate ? scalar_callback<true, Path, Kind, TypedChains> : scalar_callback<false, Path, Kind, TypedChains>;
    else
        return alternate ? vector_callback<true, Path, Kind, TypedChains> : vector_callback<false, Path, Kind, TypedChains>;
}

template <class Ring> typename Ring::Arguments initial(Work& work) {
    return {work.start, work.count, 0, reinterpret_cast<std::uintptr_t>(&work), {}};
}

mt::Dependency initial_dependency(const Work& work) {
    const auto& data = *work.dataset;
    if (data.workload == Workload::chains)
        return {&data.nodes[work.start], 64};
    if (data.workload == Workload::ranges)
        return {data.words.data() + work.start * 8, std::size_t(work.count) * 64};
    const auto ordinal = work.start % data.directory.size();
    return {data.material_directory->population_data() + (ordinal / 8) * 9, 9};
}

struct Run {
    mt::Counters counters;
    std::uint64_t spins = 0, elapsed_ns = 0, scheduler_bytes = 0;
};

template <class Ring, bool Alternate, HintPath Path, pf::Class Kind, bool TypedChains>
[[gnu::noinline]] void shared_complete(Work& owner, mt::Prefetch prefetch, mt::Counters& counters) {
    auto args = initial<Ring>(owner);
    for (;;) {
        const auto step = advance<Ring, Alternate, TypedChains>(args);
        args = step.args;
        if (step.done) {
            finish<Ring>(owner, args);
            return;
        }
        ordinary_hint<Path, Kind>(step.dependency, owner, prefetch, counters);
        if constexpr (!TypedChains) additional_hints<Ring>(step.args, [&](mt::Dependency dependency) {
            ordinary_hint<Path, Kind>(dependency, owner, prefetch, counters);
        });
    }
}

template <class Ring, HintPath Path = HintPath::legacy, pf::Class Kind = pf::Class::none, bool TypedChains = false>
Run run(std::vector<Work>& work, const std::string& variant, std::size_t slots,
        std::size_t overflow, mt::Prefetch prefetch, mt::SpinBudget budget) {
    Run result;
    if (variant == "ring") {
        std::optional<Ring> holder;
        if constexpr (Path == HintPath::legacy)
            holder.emplace(slots, overflow, prefetch);
        else
            holder.emplace(slots, overflow, work.front().hint);
        auto& ring = *holder;
        result.scheduler_bytes = ring.storage_bytes();
        const auto start = now_ns();
        std::size_t next = 0;
        while (next < work.size() || ring.pending()) {
            while (next < work.size()) {
                if (!ring.push(callback<Ring, Path, Kind, TypedChains>(work[next].alternate), initial<Ring>(work[next]),
                               initial_dependency(work[next]), work[next].hint.kind))
                    break;
                ++next;
            }
            if (!ring.spin(budget) && ring.pending())
                throw std::runtime_error("spin budget made no progress");
            ++result.spins;
        }
        result.elapsed_ns = now_ns() - start;
        result.counters = ring.counters();
    } else if (variant == "manual") {
        // Independent explicit AMAC control: each active operation advances one
        // region; completed positions admit the next item. No musttail dispatcher.
        const auto width = std::min(slots, work.size());
        std::vector<typename Ring::Arguments> states(width);
        std::vector<bool> active(width, false);
        result.scheduler_bytes = states.capacity() * sizeof(typename Ring::Arguments) +
                                 (active.capacity() + 7) / 8;
        std::size_t next = 0, pending = 0;
        const auto start = now_ns();
        for (std::size_t lane = 0; lane < width; ++lane) {
            states[lane] = initial<Ring>(work[next]);
            ordinary_hint<Path, Kind>(initial_dependency(work[next]), work[next], prefetch, result.counters);
            active[lane] = true;
            ++next;
            ++pending;
        }
        while (pending)
            for (std::size_t lane = 0; lane < width; ++lane) {
                if (!active[lane])
                    continue;
                auto& owner = *reinterpret_cast<Work*>(states[lane].d);
                auto step = owner.alternate ? advance<Ring, true, TypedChains>(states[lane]) :
                                              advance<Ring, false, TypedChains>(states[lane]);
                states[lane] = step.args;
                if (!step.done) {
                    ordinary_hint<Path, Kind>(step.dependency, owner, prefetch, result.counters);
                    if constexpr (!TypedChains) additional_hints<Ring>(step.args, [&](mt::Dependency dependency) {
                        ordinary_hint<Path, Kind>(dependency, owner, prefetch, result.counters);
                    });
                    continue;
                }
                finish<Ring>(owner, step.args);
                ++result.counters.completed;
                if (next < work.size()) {
                    states[lane] = initial<Ring>(work[next]);
                    ordinary_hint<Path, Kind>(initial_dependency(work[next]), work[next], prefetch, result.counters);
                    ++next;
                } else {
                    active[lane] = false;
                    --pending;
                }
            }
        result.counters.accepted = work.size();
        result.elapsed_ns = now_ns() - start;
    } else {
        const auto start = now_ns();
        for (auto& owner : work) {
            auto args = initial<Ring>(owner);
            ordinary_hint<Path, Kind>(initial_dependency(owner), owner, prefetch, result.counters);
            if (variant == "shared") {
                if (owner.alternate)
                    shared_complete<Ring, true, Path, Kind, TypedChains>(owner, prefetch, result.counters);
                else
                    shared_complete<Ring, false, Path, Kind, TypedChains>(owner, prefetch, result.counters);
                continue;
            }
            for (;;) {
                const auto step = owner.alternate ? advance<Ring, true, TypedChains>(args) : advance<Ring, false, TypedChains>(args);
                args = step.args;
                if (step.done) {
                    finish<Ring>(owner, args);
                    break;
                }
                ordinary_hint<Path, Kind>(step.dependency, owner, prefetch, result.counters);
                if constexpr (!TypedChains) additional_hints<Ring>(step.args, [&](mt::Dependency dependency) {
                    ordinary_hint<Path, Kind>(dependency, owner, prefetch, result.counters);
                });
            }
        }
        result.elapsed_ns = now_ns() - start;
        result.counters.accepted = result.counters.completed = work.size();
    }
    if (variant != "ring")
        for (const auto& owner : work)
            result.counters.visits += owner.dataset->workload == Workload::bec256 ?
                (owner.grain == 1 ? owner.count * 2 : owner.count + 1) :
                (owner.count + owner.grain - 1) / owner.grain;
    if constexpr (Path != HintPath::legacy) {
        for (const auto& owner : work) {
            const auto visits = owner.dataset->workload == Workload::bec256 ?
                (owner.grain == 1 ? owner.count * 2 : owner.count + 1) :
                (owner.count + owner.grain - 1) / owner.grain;
            const auto extra = owner.all_body_hints && owner.dataset->workload == Workload::bec256 ?
                                   owner.count * 3 : 0;
            result.counters.prefetches += (visits + extra) * pf::hints(owner.hint.kind);
        }
    }
    for (auto& owner : work) {
        owner.expected = reference(owner, !std::is_same_v<Ring, mt::ScalarRing>);
        if (owner.finished != 1 || owner.result != owner.expected)
            throw std::runtime_error("independent result/contribution check failed");
    }
    return result;
}

std::vector<Work> make_work(const Dataset& data, std::size_t count, std::uint64_t steps,
                           unsigned grain, bool heterogeneous, bool vectors) {
    std::vector<Work> result(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& work = result[i];
        work.dataset = &data;
        work.start = data.workload == Workload::ranges ? i * data.range_lines :
                     (i * 15485863u) % (data.workload == Workload::chains ?
                                               data.nodes.size() : data.directory.size());
        work.count = data.workload == Workload::ranges ? data.range_lines : steps;
        work.grain = grain;
        work.alternate = heterogeneous && i % 2;
    }
    (void)vectors;
    return result;
}

std::size_t minimum_dependency(const Work& owner) {
    if (owner.dataset->workload == Workload::chains) return 64;
    if (owner.dataset->workload == Workload::bec256) return 9;
    const auto remainder = owner.count % owner.grain;
    return std::size_t(std::min<std::uint64_t>(owner.count, remainder ? remainder : owner.grain)) * 64;
}

void bind_hints(std::vector<Work>& work, pf::Class requested, bool varying) {
    for (std::size_t i = 0; i < work.size(); ++i)
        work[i].hint = pf::bind_shape(varying ? pf::Class(i % pf::classes) : requested,
                                      minimum_dependency(work[i]));
}

template <class Ring, bool TypedChains>
Run classified_run_body(std::vector<Work>& work, const std::string& variant, std::size_t slots,
                   std::size_t overflow, mt::Prefetch policy, mt::SpinBudget budget,
                   HintPath path) {
    if (path == HintPath::legacy)
        return run<Ring, HintPath::legacy, pf::Class::none, TypedChains>(work, variant, slots, overflow, policy, budget);
    if (path == HintPath::runtime)
        return run<Ring, HintPath::runtime, pf::Class::none, TypedChains>(work, variant, slots, overflow, policy, budget);
#define LOOM_CLASS_RUN(C) case pf::Class::C: return run<Ring, HintPath::specialized, pf::Class::C, TypedChains>(work, variant, slots, overflow, policy, budget);
    if constexpr (TypedChains) {
        switch (work.front().hint.kind) {
            LOOM_CLASS_RUN(none) LOOM_CLASS_RUN(l1_1) LOOM_CLASS_RUN(l2_1) LOOM_CLASS_RUN(l3_1)
            default: throw std::invalid_argument("typed chain requires a class bound to 64B");
        }
    } else switch (work.front().hint.kind) {
        LOOM_CLASS_RUN(none) LOOM_CLASS_RUN(l1_1) LOOM_CLASS_RUN(l2_1) LOOM_CLASS_RUN(l3_1)
        LOOM_CLASS_RUN(l1_2) LOOM_CLASS_RUN(l2_2) LOOM_CLASS_RUN(l1_4) LOOM_CLASS_RUN(l2_4)
        LOOM_CLASS_RUN(l1_8) LOOM_CLASS_RUN(l2_8) LOOM_CLASS_RUN(l1_l2_2) LOOM_CLASS_RUN(l1_l2_4)
        LOOM_CLASS_RUN(page1) LOOM_CLASS_RUN(page2) LOOM_CLASS_RUN(page4) LOOM_CLASS_RUN(page8)
    }
#undef LOOM_CLASS_RUN
    throw std::invalid_argument("invalid bound hint class");
}

template <class Ring>
Run classified_run(std::vector<Work>& work, const std::string& variant, std::size_t slots,
                   std::size_t overflow, mt::Prefetch policy, mt::SpinBudget budget,
                   HintPath path, bool typed_chains = false) {
    if (typed_chains)
        return classified_run_body<Ring, true>(work, variant, slots, overflow, policy, budget, path);
    return classified_run_body<Ring, false>(work, variant, slots, overflow, policy, budget, path);
}

template <class Ring> void ring_boundary_checks() {
    Dataset data(Workload::chains, 127, 64);
    auto work = make_work(data, 9, 13, 1, true, !std::is_same_v<Ring, mt::ScalarRing>);
    Ring ring(3, 2, mt::Prefetch::none);
    for (std::size_t i = 0; i < 5; ++i)
        if (!ring.push(callback<Ring>(work[i].alternate), initial<Ring>(work[i]),
                       initial_dependency(work[i])))
            throw std::runtime_error("early admission refusal");
    if (ring.push(callback<Ring>(false), initial<Ring>(work[5]), initial_dependency(work[5])))
        throw std::runtime_error("overflow capacity overrun");
    if (ring.spin({0, 0, 1}) != 0 || ring.pending() != 5)
        throw std::runtime_error("zero budget changed work");
    if (ring.spin({1, 0, 1}) != 1 || ring.pending() != 5)
        throw std::runtime_error("single visit failed to suspend");
    std::size_t next = 5;
    while (next < work.size() || ring.pending()) {
        while (next < work.size() && ring.push(callback<Ring>(work[next].alternate),
                                               initial<Ring>(work[next]),
                                               initial_dependency(work[next])))
            ++next;
        ring.spin({1, 0, 1});
    }
    for (auto& owner : work) {
        owner.expected = reference(owner, !std::is_same_v<Ring, mt::ScalarRing>);
        if (owner.finished != 1 || owner.result != owner.expected)
            throw std::runtime_error("suspend/resume lost work");
    }
    if (ring.counters().completed != work.size() || ring.counters().accepted != work.size() ||
        !ring.counters().promoted || !ring.counters().refused)
        throw std::runtime_error("admission/completion accounting mismatch");
}

template <class Ring>
[[gnu::noinline]] void realigned_spin(Ring& ring, unsigned count) {
    alignas(64) volatile unsigned fixed[64];
    auto* dynamic = static_cast<volatile unsigned*>(__builtin_alloca(count * sizeof(unsigned)));
    for (unsigned i = 0; i < 64; ++i)
        fixed[i] = i + 3;
    for (unsigned i = 0; i < count; ++i)
        dynamic[i] = i + 7;
    while (ring.pending())
        ring.spin({3, 0, 1});
    for (unsigned i = 0; i < 64; ++i)
        if (fixed[i] != i + 3)
            throw std::runtime_error("realigned caller fixed frame corrupted");
    for (unsigned i = 0; i < count; ++i)
        if (dynamic[i] != i + 7)
            throw std::runtime_error("realigned caller dynamic frame corrupted");
}

LOOM_MICROTASK_CC void vector_frame_check(mt::VectorRing* ring, mt::VectorRing::Slot* slot,
                                         std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                         std::uint64_t d, mt::Vector v0, mt::Vector v1) {
#if defined(__aarch64__)
    // Reproduce the register clobber that made Ikea's fixed-frame entry needed.
    asm volatile("mov x19, #7" ::: "x19");
#endif
    [[clang::musttail]] return vector_callback<false>(ring, slot, a, b, c, d, v0, v1);
}

LOOM_MICROTASK_CC void reject_active_cleanup(mt::ScalarRing* ring, mt::ScalarRing::Slot* slot,
                                            std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                            std::uint64_t d) {
    auto& rejected = *reinterpret_cast<unsigned*>(d);
    {
        try { ring->discard(); }
        catch (const std::logic_error&) { rejected |= 1; }
        try { ring->spin({1, 0, 1}); }
        catch (const std::logic_error&) { rejected |= 2; }
    }
    ring->complete(*slot);
    [[clang::musttail]] return mt::ScalarRing::yield(ring, slot, a, b, c, d);
}

void prefetch_checks() {
    // Independent target enumeration. These are desired hint addresses, not a
    // claim that a complete line/page is accessible or resident after a hint.
    alignas(4096) std::array<std::byte, 65536> bytes{};
    constexpr std::array fixed_counts{0u, 1u, 1u, 1u, 2u, 2u, 4u, 4u,
                                      8u, 8u, 3u, 5u, 3u, 5u, 9u, 17u};
    constexpr std::array line_counts{0u, 1u, 1u, 1u, 2u, 2u, 4u, 4u, 8u, 8u, 3u, 5u};
    constexpr std::array page_counts{1u, 2u, 4u, 8u};
    for (unsigned index = 0; index < pf::classes; ++index) {
        const auto kind = pf::Class(index);
        if (pf::hints(kind) != fixed_counts[index] || pf::parse(pf::name(kind)) != kind)
            throw std::runtime_error("fixed class count/name mismatch");
        for (const std::size_t offset : {0u, 1u, 63u, 64u, 4095u, 4096u, 4097u}) {
            std::vector<std::size_t> targets;
            if (index < 12)
                for (unsigned n = 0; n < line_counts[index]; ++n)
                    targets.push_back(n * 64);
            else {
                targets.push_back(0);
                const auto next_page = ((offset / 4096) + 1) * 4096;
                for (unsigned n = 0; n < page_counts[index - 12]; ++n) {
                    targets.push_back(next_page + n * 4096 - offset);
                    targets.push_back(next_page + n * 4096 + 64 - offset);
                }
            }
            if (targets.size() != fixed_counts[index])
                throw std::runtime_error("independent fixed target count mismatch");
            for (const std::size_t length : {0u, 1u, 9u, 64u, 65u, 129u, 257u, 449u,
                                             4096u, 4097u, 4161u, 8192u, 32768u, 33000u}) {
                const auto span = std::span<const std::byte>(bytes).subspan(offset, length);
                const bool expected = std::all_of(targets.begin(), targets.end(),
                    [&](std::size_t target) { return target < length; });
                if (pf::permitted(kind, span, 4096) != expected)
                    throw std::runtime_error("unaligned/empty fixed target qualification mismatch");
                const auto bound = pf::bind(kind, span);
                if (!pf::permitted(bound.kind, span, bound.page_bytes))
                    throw std::runtime_error("cold binding escaped the qualified span");
                const auto shape = pf::bind_shape(kind, length);
                if (!pf::permitted(shape.kind, span, shape.page_bytes))
                    throw std::runtime_error("shape binding failed conservative address bounds");
                if (expected) {
                    const auto region = pf::prepare({kind, 4096}, span.data());
                    pf::entries()[index](region.base, region.first_following_page, region.page_bytes);
                    pf::issue_runtime(kind, region);
                }
            }
        }
    }
    Dataset chain_data(Workload::chains, 257, 64);
    for (unsigned index = 0; index < pf::classes; ++index)
        for (const auto path : {HintPath::legacy, HintPath::specialized, HintPath::runtime})
            for (const auto* variant : {"direct", "shared", "manual", "ring"}) {
                auto scalar = make_work(chain_data, 9, 37, 1, true, false);
                auto vector = make_work(chain_data, 9, 37, 1, true, true);
                bind_hints(scalar, pf::Class(index), false);
                bind_hints(vector, pf::Class(index), path == HintPath::runtime);
                classified_run<mt::ScalarRing>(scalar, variant, 3, 2, mt::Prefetch::first_line, {7, 0, 1}, path, true);
                classified_run<mt::VectorRing>(vector, variant, 3, 2, mt::Prefetch::none, {7, 1, 1}, path, true);
            }
    for (const auto page_bytes : {0u, 32u, 96u}) {
        bool rejected = false;
        try { (void)pf::bind_shape(pf::Class::page1, 8192, page_bytes); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("invalid hint page geometry accepted");
    }
    // All sixteen classes run through whole native consumers; direct/shared/
    // manual arms receive precisely the same bound hints as ring callbacks.
    for (const auto kind : {Workload::chains, Workload::ranges, Workload::bec256}) {
        Dataset data(kind, kind == Workload::ranges ? 19 : 257, 65536);
        for (unsigned index = 0; index < pf::classes; ++index)
            for (const auto path : {HintPath::specialized, HintPath::runtime})
                for (const auto* variant : {"direct", "shared", "manual", "ring"}) {
                    auto work = make_work(data, 5, 13, kind == Workload::ranges ? 1024 : 1, true, false);
                    for (auto& owner : work) owner.all_body_hints = true;
                    bind_hints(work, pf::Class(index), false);
                    const auto result = classified_run<mt::ScalarRing>(work, variant, 3, 2,
                        mt::Prefetch::none, {7, 0, 1}, path);
                    const auto extra = kind == Workload::bec256 ? 5 * 13 * 3 : 0;
                    if (result.counters.prefetches !=
                        (result.counters.visits + extra) * pf::hints(work.front().hint.kind))
                        throw std::runtime_error("derived hint count mismatch");
                }
        auto work = make_work(data, 19, 13, kind == Workload::ranges ? 1024 : 3, true, true);
        bind_hints(work, pf::Class::page8, true);
        classified_run<mt::VectorRing>(work, "ring", 3, 2, mt::Prefetch::none, {1, 1, 1}, HintPath::runtime);
    }
}

void checks() {
    prefetch_checks();
    ring_boundary_checks<mt::ScalarRing>();
    ring_boundary_checks<mt::VectorRing>();
    for (const auto kind : {Workload::chains, Workload::ranges, Workload::bec256}) {
        Dataset data(kind, kind == Workload::ranges ? 19 : 257, 8192);
        for (const auto* variant : {"direct", "shared", "manual", "ring"})
            for (const auto width : {1u, 3u, 16u, 32u})
              for (const auto overflow : {0u, 2u}) {
                auto scalar = make_work(data, 19, 23, 1, true, false);
                auto vector = make_work(data, 19, 23, 3, true, true);
                for (auto& owner : vector)
                    owner.all_body_hints = true;
                run<mt::ScalarRing>(scalar, variant, width, overflow, mt::Prefetch::pages, {7, 0, 1});
                run<mt::VectorRing>(vector, variant, width, overflow, mt::Prefetch::first_line,
                                    {5, 0, 1});
            }
    }
    // The elapsed budget is coarse sampled and may overshoot by one sampling
    // interval or one long callback; completion must still be exact on resume.
    Dataset data(Workload::chains, 257, 64);
    auto sampled = make_work(data, 2, 7, 1, false, false);
    mt::ScalarRing sampled_ring(2, 0, mt::Prefetch::none);
    for (auto& owner : sampled)
        sampled_ring.push(callback<mt::ScalarRing>(false), initial<mt::ScalarRing>(owner),
                           initial_dependency(owner));
    // Four samples after visits3/6/9/12. The final drain skips a sample; the
    // clock helper must not decrement the interval again before dispatching.
    if (sampled_ring.spin({1000, std::numeric_limits<std::uint64_t>::max(), 3}) != 14 ||
        sampled_ring.counters().clock_samples != 4 || sampled_ring.pending())
        throw std::runtime_error("clock helper duplicated/skipped sample or progress");
    for (auto& owner : sampled)
        if (owner.finished != 1 || owner.result != reference(owner, false))
            throw std::runtime_error("sampled clock/retirement lost a contribution");
    auto work = make_work(data, 17, 37, 1, true, false);
    run<mt::ScalarRing>(work, "ring", 3, 2, mt::Prefetch::none, {7, 1, 1});
    auto vector = make_work(data, 5, 37, 1, false, true);
    mt::VectorRing ring(3, 2, mt::Prefetch::none);
    for (auto& owner : vector)
        if (!ring.push(vector_frame_check, initial<mt::VectorRing>(owner),
                       initial_dependency(owner)))
            throw std::runtime_error("frame-check admission failed");
    realigned_spin(ring, 23);
    for (auto& owner : vector)
        if (owner.finished != 1 || owner.result != reference(owner, true))
            throw std::runtime_error("realigned vector ring result failed");
    auto abandoned = make_work(data, 5, 37, 1, true, false);
    mt::ScalarRing abort_ring(3, 2, mt::Prefetch::none);
    for (auto& owner : abandoned)
        if (!abort_ring.push(callback<mt::ScalarRing>(owner.alternate), initial<mt::ScalarRing>(owner),
                             initial_dependency(owner)))
            throw std::runtime_error("discard fixture admission failed");
    abort_ring.spin({1, 0, 1});
    if (abort_ring.discard() != 5 || abort_ring.pending() || abort_ring.discard() != 0 ||
        abort_ring.counters().accepted != abort_ring.counters().completed + abort_ring.counters().abandoned)
        throw std::runtime_error("discard lost pending accounting");
    for (const auto& owner : abandoned)
        if (owner.finished)
            throw std::runtime_error("discard reported an unfinished operation as completed");
    if (!abort_ring.push(callback<mt::ScalarRing>(false), initial<mt::ScalarRing>(abandoned.front()),
                         initial_dependency(abandoned.front())))
        throw std::runtime_error("discard did not restore finite inventory");
    while (abort_ring.pending())
        abort_ring.spin({1, 0, 1});
    if (abandoned.front().finished != 1 ||
        abandoned.front().result != reference(abandoned.front(), false) ||
        abort_ring.counters().accepted != abort_ring.counters().completed + abort_ring.counters().abandoned)
        throw std::runtime_error("discard/reuse result or accounting failed");
    unsigned rejected = 0;
    mt::ScalarRing reject_ring(1, 0, mt::Prefetch::none);
    reject_ring.push(reject_active_cleanup, {0, 0, 0, reinterpret_cast<std::uintptr_t>(&rejected), {}}, {});
    reject_ring.spin({1, 0, 1});
    if (rejected != 3 || reject_ring.counters().abandoned || reject_ring.pending())
        throw std::runtime_error("active cleanup/reentrant spin was not rejected");
}

} // namespace
} // namespace loom_spike

int main(int argc, char** argv) {
    using namespace loom_spike;
    try {
        Options options(argc, argv);
        if (options.has("check")) {
            checks();
            Record record;
            record.family = "microtask";
            record.variant = "check";
            record.unit = "operations";
            record.verified = true;
#if defined(LOOM_MICROTASK_ORDINARY_ABI)
            record.parameters["calling_convention"] = "ordinary";
#else
            record.parameters["calling_convention"] = "preserve_none";
#endif
            emit(record);
            return 0;
        }
        const auto cpus = options.cpus();
        if (!cpus.empty())
            pin(cpus.front());
        const auto workload_name = options.get("workload", "chains");
        if (workload_name != "chains" && workload_name != "ranges" && workload_name != "bec256")
            throw std::invalid_argument("workload must be chains/ranges/bec256");
        const auto workload = workload_name == "ranges" ? Workload::ranges :
                              workload_name == "bec256" ? Workload::bec256 : Workload::chains;
        const auto variant = options.get("variant", "ring");
        if (variant != "ring" && variant != "manual" && variant != "direct" && variant != "shared")
            throw std::invalid_argument("variant must be direct/shared/manual/ring");
        const auto policy_name = options.get("prefetch", "none");
        if (policy_name != "none" && policy_name != "first" && policy_name != "pages")
            throw std::invalid_argument("prefetch must be none/first/pages");
        const auto hint_path_name = options.get("hint-path", "legacy");
        if (hint_path_name != "legacy" && hint_path_name != "specialized" && hint_path_name != "runtime")
            throw std::invalid_argument("hint-path must be legacy/specialized/runtime");
        const auto hint_path = hint_path_name == "runtime" ? HintPath::runtime :
                               hint_path_name == "specialized" ? HintPath::specialized : HintPath::legacy;
        const auto requested_class = pf::parse(options.get("hint-class", policy_name == "none" ? "none" :
                                                          policy_name == "first" ? "l1-1" : "page8"));
        const bool varying_classes = options.has("varying-classes");
        if (varying_classes && hint_path != HintPath::runtime)
            throw std::invalid_argument("varying classes require the measured runtime path");
        const auto policy = policy_name == "pages" ? mt::Prefetch::pages :
                            policy_name == "first" ? mt::Prefetch::first_line : mt::Prefetch::none;
        const auto jobs = options.number("jobs", 128);
        const auto dataset_items = options.number("dataset-items", 131072);
        const auto range_bytes = options.number("range-bytes", 65536);
        const auto steps = options.number("steps", 256);
        const auto slots = options.number("ring", 16);
        const auto overflow = options.number("overflow", 32);
        const auto grain_number = options.number("grain", 1);
        if (grain_number > std::numeric_limits<unsigned>::max())
            throw std::invalid_argument("grain is outside unsigned range");
        const auto grain = unsigned(grain_number);
        const auto typed_chains = options.has("typed-chains");
        if (typed_chains && (workload != Workload::chains || grain != 1))
            throw std::invalid_argument("typed-chains requires chains and grain 1");
        const auto repeat = options.number("repeat", 0);
        const auto vectors = options.has("vectors");
        const auto heterogeneous = options.has("heterogeneous");
        const auto clock_interval = options.number("clock-interval", 16);
        if (clock_interval > std::numeric_limits<unsigned>::max())
            throw std::invalid_argument("clock interval is outside unsigned range");
        const mt::SpinBudget budget{options.number("visits", 128), options.number("budget-ns", 0),
                                    unsigned(clock_interval)};
        if (!jobs || !dataset_items || !slots || !grain || !steps || !range_bytes || !budget.visits)
            throw std::invalid_argument("positive jobs/dataset/ring/grain/steps/range/visits required");
        if (slots > std::numeric_limits<std::size_t>::max() - overflow ||
            range_bytes > std::numeric_limits<std::size_t>::max() - 63 ||
            dataset_items > std::numeric_limits<std::size_t>::max() / sizeof(Node) ||
            jobs > std::numeric_limits<std::size_t>::max() / sizeof(Work) ||
            (workload == Workload::ranges &&
             jobs > std::numeric_limits<std::size_t>::max() / (((range_bytes + 63) / 64) * 64)) ||
            (workload == Workload::bec256 &&
             dataset_items > std::numeric_limits<unsigned>::max() / bc::max_bytes))
            throw std::invalid_argument("input/ring geometry overflows storage or metadata offsets");
        constexpr std::uint64_t allocation_limit = 1024ull * 1024 * 1024;
        // Conservative peak construction bounds include temporary directories,
        // permutation arrays and caller state. This is a fixture admission cap,
        // not a proposed Loom memory-policy constant.
        const auto input_bound = workload == Workload::ranges ?
            static_cast<long double>(jobs) * ((range_bytes + 63) / 64) * 64 :
            static_cast<long double>(dataset_items) * (workload == Workload::bec256 ? 256 : 72);
        const auto slot_bytes = vectors ? sizeof(mt::VectorRing::Slot) : sizeof(mt::ScalarRing::Slot);
        const auto allocation_bound = input_bound + static_cast<long double>(jobs) * sizeof(Work) +
            static_cast<long double>(slots + overflow) * (slot_bytes + 3 * sizeof(void*));
        if (allocation_bound > allocation_limit)
            throw std::invalid_argument("fixture construction exceeds its 1 GiB allocation bound");
        Dataset data(workload, workload == Workload::ranges ? jobs : dataset_items, range_bytes);
        {
            auto work = make_work(data, jobs, steps, grain, heterogeneous, vectors);
            for (auto& owner : work)
                owner.all_body_hints = options.has("all-body-hints");
            bind_hints(work, requested_class, varying_classes);
            const auto result = vectors ? classified_run<mt::VectorRing>(work, variant, slots, overflow, policy, budget, hint_path, typed_chains) :
                                          classified_run<mt::ScalarRing>(work, variant, slots, overflow, policy, budget, hint_path, typed_chains);
            Record record;
            record.family = "microtask";
            record.variant = variant;
            record.unit = "native_accesses";
            record.repeat = repeat;
            record.elapsed_ns = result.elapsed_ns;
            record.items = jobs * (workload == Workload::ranges ? data.range_lines : steps) *
                           (workload == Workload::bec256 ? 2 : 1);
            record.verified = true;
            record.parameters = {{"workload", workload_name}, {"prefetch", policy_name},
                                 {"ring", std::to_string(slots)}, {"overflow", std::to_string(overflow)},
                                 {"jobs", std::to_string(jobs)}, {"grain", std::to_string(grain)},
                                 {"steps", std::to_string(steps)},
                                 {"vectors", vectors ? "true" : "false"},
                                 {"heterogeneous", heterogeneous ? "true" : "false"},
                                 {"visits", std::to_string(budget.visits)},
                                 {"budget_ns", std::to_string(budget.elapsed_ns)},
                                 {"dataset_items", std::to_string(dataset_items)},
                                 {"range_bytes", std::to_string(range_bytes)}};
            record.parameters["native_body"] = typed_chains ? "typed_chain_one_node" : "multi_workload_probe";
            record.parameters["hint_path"] = hint_path_name;
            record.parameters["hint_class_requested"] = std::string(pf::name(requested_class));
            record.parameters["hint_class_bound"] = varying_classes ? "varying_prebound" : std::string(pf::name(work.front().hint.kind));
            record.parameters["hint_counting"] = hint_path == HintPath::legacy ? "hot_legacy_counter" : "derived_after_timing";
            record.parameters["visit_counting"] = variant == "ring" ? "accumulated_at_spin_return" : "derived_after_timing";
            std::array<std::uint64_t, pf::classes> class_jobs{};
            for (const auto& owner : work) ++class_jobs[unsigned(owner.hint.kind)];
            for (unsigned index = 0; index < pf::classes; ++index)
                if (class_jobs[index]) record.counters["class_jobs_" + std::string(pf::name(pf::Class(index)))] = class_jobs[index];
            record.parameters["hint_minimum_bytes"] = std::to_string(minimum_dependency(work.front()));
            record.parameters["metadata"] = workload == Workload::bec256 ? "series_local_checkpoint64_point" : "none";
            record.parameters["hint_coverage"] = workload == Workload::chains ? "next_node" :
                workload == Workload::ranges ? "initial_range_then_next_region" :
                options.has("all-body-hints") ? "first_population_tile_both_bodies_and_both_queries" :
                                                "first_population_tile_first_body_only";
            record.parameters["input_preparation"] = "constructed_without_cache_eviction_reference_after_timing";
            record.parameters["slot_bytes"] = std::to_string(vectors ? sizeof(mt::VectorRing::Slot) : sizeof(mt::ScalarRing::Slot));
            record.parameters["allocation_bound_bytes"] = std::to_string(std::uint64_t(allocation_bound));
            record.parameters["bootstrap_page_bytes"] = "4096";
            record.parameters["timed_region"] = "enqueue_and_drain_excludes_storage_construction_and_verification";
            record.parameters["selected_cpu"] = cpus.empty() ? "unbound" : std::to_string(cpus.front());
#if defined(LOOM_MICROTASK_ORDINARY_ABI)
            record.parameters["calling_convention"] = "ordinary";
#else
            record.parameters["calling_convention"] = "preserve_none";
#endif
            record.counters.insert({{"accepted", result.counters.accepted}, {"refused", result.counters.refused},
                               {"completed", result.counters.completed}, {"visits", result.counters.visits},
                               {"abandoned", result.counters.abandoned},
                               {"promoted", result.counters.promoted}, {"prefetch_hints", result.counters.prefetches},
                               {"budget_stops", result.counters.budget_stops}, {"spins", result.spins},
                               {"clock_samples", result.counters.clock_samples},
                               {"high_ring", result.counters.high_ring},
                               {"high_overflow", result.counters.high_overflow},
                               {"scheduler_storage_bytes", result.scheduler_bytes},
                               {"fixture_input_bytes", data.storage_bytes()},
                               {"caller_state_bytes", work.capacity() * sizeof(Work)}});
            emit(record);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "loom_microtask: " << error.what() << '\n';
        return 1;
    }
}
