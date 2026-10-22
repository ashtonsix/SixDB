// Complete native publication phases. Private research controls, not a policy.
#include "phase_construction.h"
#include "phase_owner.h"
#include "../measure_order.h"
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace p = frame_lifecycle::phase;
namespace a = orbital::allocation;
namespace {
using Clock = std::chrono::steady_clock;
using p::take; using p::require;
std::uint64_t ns(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
}
template<class T> T load(const std::byte* s) { T x; std::memcpy(&x, s, sizeof x); return x; }
struct Edit { unsigned row; p::LogicalRow next; };
using Data = std::array<p::GroupData, p::groups>;
p::QueryResult expected(const Data& d, p::BlockSelection mask, p::u64 threshold) {
    p::QueryResult r;
    for (unsigned i = 0; i < p::rows; ++i) if (mask.row(i)) {
        const auto v = d[i / p::rows_per_group].row(i % p::rows_per_group);
        r.present += v.present;
        if (v.contributes()) { r.sum += v.value; r.count += v.value >= threshold; }
    }
    return r;
}
struct Costs {
    std::uint64_t prepare_ns{}, publish_ns{}, query_ns{}, conversion_ns{}, release_ns{};
    std::size_t copied_bytes{}, encoded_rows{}, peak_nonidle{}, peak_metadata{}, overlay_entries{};
};
enum class Policy { native_raw, native_for, reencode, overlay, overlay_fold, thaw };
const char* name(Policy policy) {
    switch (policy) {
    case Policy::native_raw:return "native-raw64"; case Policy::native_for:return "native-for";
    case Policy::reencode:return "whole-group-reencode"; case Policy::overlay:return "base-overrides-debt";
    case Policy::overlay_fold:return "base-overrides-fold"; case Policy::thaw:return "thaw-reseal";
    }
    std::abort();
}

template<class C> struct Machine {
    using Group = typename decltype(C{}.group)::value_type;
    using Store = orbital::ObjectStore<p::Physical<C>>;
    using Selection = orbital::RepresentationSelection<C>;
    static constexpr unsigned slots = p::groups * Group::parts;
    p::Runtime runtime;
    p::Memory memory;
    Store store{memory};
    p::fe::encode_options options;
    Policy policy;
    Costs costs;
    explicit Machine(Policy mode, p::fe::encode_options opts) : options(opts), policy(mode) {}
    ~Machine() { store = {}; require(!memory.live, "phase stranded metadata"); }
    void observe() {
        const auto stats = a::statistics(*runtime.pool);
        require(stats.idle_bytes <= stats.mapped_bytes, "invalid Pool accounting");
        costs.peak_nonidle = std::max(costs.peak_nonidle, std::size_t(stats.mapped_bytes - stats.idle_bytes));
        costs.peak_metadata = std::max(costs.peak_metadata, memory.peak);
    }
    Selection selected(const typename Store::Scope& scope) {
        const auto alternate = scope.bind_alternative(p::object);
        return alternate ? *alternate : scope.bind(p::object);
    }
    static p::Bytes part(Selection source, unsigned i) {
        return {take(source.part(i)).address(), *source.realization->control.extent(i)};
    }
    void publish(const typename Store::Owner& owner) {
        typename Store::Prepared packet(store);
        const std::array<typename Store::Change, 1> changes{{{p::object, owner}}};
        { typename Store::Scope basis(store); take(store.prepare(basis, changes, packet)); }
        take(store.install(packet));
    }
    struct Fresh {
        std::array<a::Allocation, slots> owned;
        std::array<a::Hold, slots> holds;
        std::array<orbital::PartSource, slots> source;
    };
    std::span<std::byte> allocate(Fresh& f, const C& c, unsigned slot, unsigned bytes) {
        f.owned[slot] = runtime.allocate(bytes);
        f.holds[slot] = take(a::hold(f.owned[slot], {0, bytes}));
        f.source[slot] = {c.id(slot), a::bytes(f.owned[slot]), {}, &f.owned[slot]};
        return {f.owned[slot].address, bytes};
    }
    void construct(C& c, Fresh& f, unsigned g, const p::GroupData& data, bool native) {
        auto& b = p::base(c.group[g]);
        const auto copy = [&](const auto& built) {
            if constexpr (std::is_same_v<Group, p::NativeBase>) b = built.base;
            else {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(built.base)>, p::NativeBase>) {
                    b = {}; b.header = built.base.header;
                } else b = built.base;
            }
            auto values = allocate(f, c, g * Group::parts, b.header.bytes);
            built.copy_values(values); costs.copied_bytes += values.size();
        };
        if constexpr (std::is_same_v<Group, p::NativeBase>) copy(p::NativeConstruction(data, policy == Policy::native_raw));
        else if (native) copy(p::NativeConstruction(data));
        else { copy(p::EncodedConstruction(data, options)); costs.encoded_rows += p::rows_per_group; }
        auto flags = allocate(f, c, g * Group::parts + 1, p::flag_bytes * 2);
        p::copy_flags(data, flags); costs.copied_bytes += flags.size();
        if constexpr (Group::parts == 3) {
            auto& delta = c.group[g].changes; delta = {0, b.header.identity};
            auto bytes = allocate(f, c, g * Group::parts + 2, p::Overlay::bytes(0));
            p::copy_overlay({}, bytes, delta.basis); costs.copied_bytes += bytes.size();
        }
    }
    void seed(const Data& data) {
        C c; c.memory = &memory; Fresh f;
        for (unsigned g = 0; g < p::groups; ++g) construct(c, f, g, data[g], false);
        const auto owner = take(store.capture(c, f.source)); publish(owner); observe();
    }
    p::QueryResult query(Selection source, p::BlockSelection mask, p::u64 threshold, p::QueryCache& cache) {
        p::QueryResult total;
        if (!mask.bits) return total;
        const auto& c = source.realization->control;
        for (unsigned g = 0; g < p::groups; ++g) if (mask.group(g)) {
            const auto& b = p::base(c.group[g]); const auto i = g * Group::parts;
            p::QueryResult r;
            if constexpr (Group::parts == 3)
                r = p::query_overlay(b, part(source, i), part(source, i + 1), c.group[g].changes,
                                     part(source, i + 2), mask.group(g), threshold, cache);
            else r = p::query(b, part(source, i), part(source, i + 1), mask.group(g), threshold, cache);
            total.sum += r.sum; total.count += r.count; total.present += r.present;
        }
        return total;
    }
    p::LogicalRow point(Selection source, unsigned row) {
        const auto g = row / p::rows_per_group, local = row % p::rows_per_group;
        const auto& group = source.realization->control.group[g];
        if constexpr (Group::parts == 3) {
            const auto n = group.changes.count; const auto extra = part(source, g * Group::parts + 2);
            unsigned low = 0, high = n;
            while (low < high) {
                const auto middle = (low + high) / 2;
                if (load<std::uint16_t>(extra.data + 8 + middle * 2) < local) low = middle + 1;
                else high = middle;
            }
            if (low < n && load<std::uint16_t>(extra.data + 8 + low * 2) == local) {
                const auto flags = load<std::uint8_t>(extra.data + 8 + n * 3 + low);
                return {flags == 3 ? load<p::u64>(extra.data + 8 + n * 12 + low * 8) : 0,
                        bool(flags & 1), bool(flags & 2)};
            }
        }
        return p::point(p::base(group), part(source, g * Group::parts), part(source, g * Group::parts + 1), local);
    }
    p::GroupData materialize(Selection source, unsigned g) {
        const auto& group = source.realization->control.group[g]; const auto& b = p::base(group);
        const auto v = part(source, g * Group::parts), f = part(source, g * Group::parts + 1);
        p::GroupData out;
        std::memcpy(out.present.data(), f.data, p::flag_bytes);
        std::memcpy(out.valid.data(), f.data + p::flag_bytes, p::flag_bytes);
        if constexpr (!std::is_same_v<Group, p::NativeBase>) {
            if (!b.header.native_bytes)
                for (unsigned block = 0; block < p::blocks_per_group; ++block)
                    p::fe::decode_block(p::borrow(b, v, block), out.value.data() + block * 256);
            else for (unsigned i = 0; i < p::rows_per_group; ++i) out.value[i] = p::point(b, v, f, i).value;
        } else for (unsigned i = 0; i < p::rows_per_group; ++i) out.value[i] = p::point(b, v, f, i).value;
        if constexpr (Group::parts == 3) {
            const auto entries = overlays(group.changes, part(source, g * Group::parts + 2));
            for (const auto& e : entries) out.set(e.position, e.current);
        }
        return out;
    }
    static std::vector<p::OverrideRow> overlays(p::Overlay h, p::Bytes body) {
        std::vector<p::OverrideRow> out; out.reserve(h.count);
        for (unsigned i = 0; i < h.count; ++i) {
            const auto old = load<std::uint8_t>(body.data + 8 + h.count * 2 + i);
            const auto next = load<std::uint8_t>(body.data + 8 + h.count * 3 + i);
            out.push_back({load<std::uint16_t>(body.data + 8 + i * 2),
                {load<p::u64>(body.data + 8 + h.count * 4 + i * 8), bool(old & 1), bool(old & 2)},
                {load<p::u64>(body.data + 8 + h.count * 12 + i * 8), bool(next & 1), bool(next & 2)}});
        }
        return out;
    }
    void update_native(C& c, Fresh& f, Selection source, unsigned g, std::span<const Edit> edits,
                       p::BlockSelection mask) {
        auto& h = p::base(c.group[g]).header;
        const auto original_values = part(source, g * Group::parts), original_flags = part(source, g * Group::parts + 1);
        std::span<std::byte> values, flags;
        for (const auto& e : edits) if (e.row / p::rows_per_group == g && mask.row(e.row)) {
            if (e.next.contributes() && (e.next.value < h.base ||
                (h.native_bytes != 8 && (e.next.value - h.base) >> (8 * h.native_bytes)))) {
                // Adaptive native FOR has to widen/rebase too. Replay the whole
                // batch against the original image before any preserving copy.
                auto data = materialize(source, g);
                for (const auto& x : edits) if (x.row / p::rows_per_group == g && mask.row(x.row))
                    data.set(x.row % p::rows_per_group, x.next);
                construct(c, f, g, data, true); return;
            }
        }
        for (const auto& e : edits) {
            if (e.row / p::rows_per_group != g || !mask.row(e.row)) continue;
            const auto i = e.row % p::rows_per_group;
            const auto* bits = flags.empty() ? original_flags.data : flags.data();
            const auto pword = load<p::u64>(bits + i / 64 * 8), vword = load<p::u64>(bits + p::flag_bytes + i / 64 * 8);
            const bool old_present = pword >> (i % 64) & 1, old_valid = vword >> (i % 64) & 1;
            const bool new_valid = e.next.contributes();
            if (old_present != e.next.present || old_valid != new_valid) {
                if (flags.empty()) {
                    flags = allocate(f, c, g * Group::parts + 1, original_flags.size);
                    std::memcpy(flags.data(), original_flags.data, flags.size()); costs.copied_bytes += flags.size();
                }
                const auto bit = p::u64{1} << (i % 64);
                const auto next_p = (pword & ~bit) | (e.next.present ? bit : 0);
                const auto next_v = (vword & ~bit) | (new_valid ? bit : 0);
                std::memcpy(flags.data() + i / 64 * 8, &next_p, 8);
                std::memcpy(flags.data() + p::flag_bytes + i / 64 * 8, &next_v, 8);
                h.present_count[i / 256] += int(e.next.present) - int(old_present);
                h.valid_count[i / 256] += int(new_valid) - int(old_valid);
            }
            if (new_valid) {
                if (values.empty()) {
                    values = allocate(f, c, g * Group::parts, original_values.size);
                    std::memcpy(values.data(), original_values.data, values.size()); costs.copied_bytes += values.size();
                }
                const auto residual = e.next.value - h.base;
                // Native lanes are unsigned little-endian on the target CPUs.
                std::memcpy(values.data() + i * h.native_bytes, &residual, h.native_bytes);
                // Conservative evidence stays sound when an old extremum dies.
                auto& bounds = h.bounds[i / 256];
                bounds.minimum = std::min(bounds.minimum, e.next.value); bounds.maximum = std::max(bounds.maximum, e.next.value);
            }
        }
        h.identity = p::fresh_identity();
    }
    void update(std::span<const Edit> edits, p::BlockSelection mask) {
        if (!mask.bits) return;
        const auto t = Clock::now();
        typename Store::Scope scope(store); const auto source = selected(scope);
        auto c = source.realization->control; Fresh f;
        for (unsigned i = 0; i < slots; ++i) f.source[i] = take(orbital::source(source, i));
        bool changed = false;
        for (unsigned g = 0; g < p::groups; ++g) {
            bool dirty = false;
            for (const auto& e : edits) dirty |= e.row / p::rows_per_group == g && mask.row(e.row);
            if (!dirty) continue; changed = true;
            if constexpr (Group::parts == 3) {
                auto& group = c.group[g]; const auto& b = group.frozen;
                auto entries = overlays(group.changes, part(source, g * Group::parts + 2));
                for (const auto& e : edits) if (e.row / p::rows_per_group == g && mask.row(e.row)) {
                    const auto row = e.row % p::rows_per_group;
                    const auto it = std::lower_bound(entries.begin(), entries.end(), row,
                        [](const auto& x, unsigned r) { return x.position < r; });
                    if (it != entries.end() && it->position == row) it->current = e.next;
                    else {
                        const auto old = p::point(b, part(source, g * Group::parts), part(source, g * Group::parts + 1), row);
                        entries.insert(it, {std::uint16_t(row), old, e.next});
                    }
                }
                std::erase_if(entries, [](const auto& e) {
                    return p::flags(e.original) == p::flags(e.current) &&
                        (!e.original.contributes() || e.original.value == e.current.value);
                });
                group.changes.count = entries.size();
                auto bytes = allocate(f, c, g * Group::parts + 2, p::Overlay::bytes(entries.size()));
                p::copy_overlay(entries, bytes, group.changes.basis); costs.copied_bytes += bytes.size();
                costs.overlay_entries = std::max(costs.overlay_entries, entries.size());
            } else if (p::base(c.group[g]).header.native_bytes) update_native(c, f, source, g, edits, mask);
            else {
                auto data = materialize(source, g);
                for (const auto& e : edits) if (e.row / p::rows_per_group == g && mask.row(e.row))
                    data.set(e.row % p::rows_per_group, e.next);
                construct(c, f, g, data, false);
            }
        }
        if (!changed) return;
        const auto owner = take(store.capture(c, f.source)); costs.prepare_ns += ns(t);
        const auto publish_start = Clock::now(); publish(owner); costs.publish_ns += ns(publish_start); observe();
    }
    void convert(bool native, p::BlockSelection mask) {
        if (!mask.bits) return;
        const auto start = Clock::now();
        typename Store::Scope scope(store); const auto source = selected(scope);
        auto c = source.realization->control; Fresh f;
        for (unsigned i = 0; i < slots; ++i) f.source[i] = take(orbital::source(source, i));
        for (unsigned g = 0; g < p::groups; ++g) if (mask.group(g))
            construct(c, f, g, materialize(source, g), native);
        const auto owner = take(store.capture(c, f.source));
        const auto target = take(store.conversion(scope, p::object)); take(store.adopt(target, owner));
        costs.conversion_ns += ns(start); observe();
    }
};

struct Scenario {
    std::vector<std::vector<Edit>> batches;
    std::vector<Data> snapshots;
    p::BlockSelection mask;
    std::string pattern;
};
Scenario scenario(const Data& initial, std::string pattern, p::BlockSelection mask) {
    Scenario s; s.pattern = pattern; s.mask = mask; s.snapshots.push_back(initial);
    std::mt19937_64 random(713);
    for (unsigned b = 0; b < 8; ++b) {
        std::vector<Edit> edits; auto current = s.snapshots.back();
        for (unsigned i = 0; i < 64; ++i) {
            const unsigned row = pattern == "repeated-hot" ? i % 8 :
                pattern == "moving-hot" ? (b * 257 + i % 32) % p::rows : unsigned(random() % p::rows);
            auto next = initial[row / p::rows_per_group].row(row % p::rows_per_group);
            if (pattern == "novelty") next.value = random();
            else next.value += (i + b) % 3;
            next.present = i % 31 != 0; next.valid = next.present && i % 17 != 0;
            edits.push_back({row, next});
            if (mask.row(row)) current[row / p::rows_per_group].set(row % p::rows_per_group, next);
        }
        s.batches.push_back(std::move(edits)); s.snapshots.push_back(std::move(current));
    }
    return s;
}

template<class C> void run(Policy policy, const Data& initial, const Scenario& s, unsigned trial,
                           unsigned order, unsigned arm, bool keep_old, unsigned read_queries,
                           unsigned final_queries, bool verify_points, p::fe::encode_options options) {
    Machine<C> m(policy, options);
    const auto setup = Clock::now(); m.seed(initial); const auto setup_ns = ns(setup);
    m.costs.prepare_ns = m.costs.publish_ns = m.costs.copied_bytes = m.costs.encoded_rows = 0;
    using Scope = typename Machine<C>::Store::Scope;
    std::unique_ptr<Scope> old;
    if (keep_old) old = std::make_unique<Scope>(m.store);
    const auto threshold = initial[0].value[71];
    std::vector<p::QueryResult> answers;
    for (const auto& data : s.snapshots) answers.push_back(expected(data, s.mask, threshold));
    const auto verify = [&](const Scope& scope, p::QueryResult wanted, unsigned repetitions) {
        const auto selected = m.selected(scope); p::QueryCache cache(threshold);
        for (unsigned q = 0; q < repetitions; ++q) {
            const auto start = Clock::now(); const auto actual = m.query(selected, s.mask, threshold, cache);
            m.costs.query_ns += ns(start); p::fe::consume(actual.sum);
            require(actual == wanted, "phase query differs from original logical oracle");
        }
    };
    const auto all_points = [&](const Scope& scope, const Data& wanted) {
        const auto source = m.selected(scope);
        for (unsigned i = 0; i < p::rows; ++i) {
            const auto actual = m.point(source, i), original = wanted[i / p::rows_per_group].row(i % p::rows_per_group);
            require(actual.present == original.present && actual.valid == original.valid &&
                (!original.contributes() || actual.value == original.value), "phase point differs from original coordinates");
        }
    };
    const auto start = Clock::now();
    if (policy == Policy::thaw) m.convert(true, s.mask);
    for (unsigned b = 0; b < s.batches.size(); ++b) {
        m.update(s.batches[b], s.mask);
        { Scope current(m.store); verify(current, answers[b + 1], read_queries);
          if (verify_points && !trial) all_points(current, s.snapshots[b + 1]); }
        if (old) { verify(*old, answers[0], 1); if (verify_points && !trial) all_points(*old, initial); }
        const auto release = Clock::now(); m.store.drain(); m.costs.release_ns += ns(release);
    }
    if (policy == Policy::overlay_fold || policy == Policy::thaw) m.convert(false, s.mask);
    { Scope current(m.store); verify(current, answers.back(), final_queries);
      if (verify_points && !trial) all_points(current, s.snapshots.back()); }
    const auto release = Clock::now(); old.reset(); m.store.drain(); m.costs.release_ns += ns(release);
    const auto elapsed = ns(start); m.observe();
    const auto stats = a::statistics(*m.runtime.pool);
    std::printf("{\"policy\":\"%s\",\"pattern\":\"%s\",\"mask\":%u,\"retain_old\":%u,\"trial\":%u,\"order_position\":%u,"
        "\"arm\":%u,\"read_queries_per_batch\":%u,\"final_read_queries\":%u,\"verification_run\":%u,"
        "\"total_ns\":%llu,\"setup_ns\":%llu,\"prepare_ns\":%llu,\"publish_ns\":%llu,\"query_ns\":%llu,\"conversion_ns\":%llu,\"release_ns\":%llu,"
        "\"copied_bytes\":%zu,\"encoded_rows\":%zu,\"peak_nonidle_bytes\":%zu,\"peak_metadata_bytes\":%zu,\"overlay_entries\":%zu,"
        "\"final_nonidle_bytes\":%llu,\"pool_mapped_bytes\":%llu,\"pool_idle_bytes\":%llu,\"final_metadata_bytes\":%zu}\n",
        name(policy),s.pattern.c_str(),s.mask.bits,unsigned(keep_old),trial,order,
        arm,read_queries,final_queries,unsigned(verify_points),
        (unsigned long long)elapsed,(unsigned long long)setup_ns,(unsigned long long)m.costs.prepare_ns,
        (unsigned long long)m.costs.publish_ns,(unsigned long long)m.costs.query_ns,(unsigned long long)m.costs.conversion_ns,
        (unsigned long long)m.costs.release_ns,m.costs.copied_bytes,m.costs.encoded_rows,m.costs.peak_nonidle,m.costs.peak_metadata,
        m.costs.overlay_entries,(unsigned long long)(stats.mapped_bytes-stats.idle_bytes),(unsigned long long)stats.mapped_bytes,
        (unsigned long long)stats.idle_bytes,m.memory.live);
}
} // namespace
int main(int argc,char** argv) {
    require(argc >= 2 && argc <= 6, "usage: frame_phase_measure INPUT.u64 [trials=8] [queries_per_batch=8] [final_queries=64] [verify=0]");
    Data initial;
    std::ifstream input(argv[1],std::ios::binary);
    for (auto& g : initial) require(bool(input.read(reinterpret_cast<char*>(g.value.data()),sizeof(g.value))),"need at least8192 original values");
    const unsigned requested = argc > 2 ? std::stoul(argv[2]) : 8;
    const unsigned read_queries = argc > 3 ? std::stoul(argv[3]) : 8;
    const unsigned final_queries = argc > 4 ? std::stoul(argv[4]) : 64;
    const unsigned verification = argc > 5 ? std::stoul(argv[5]) : 0;
    require(requested && requested <= 120,"invalid trial count");
    require(read_queries && read_queries <= 4096 && final_queries <= 4096 && verification <= 1,"invalid query/verification count");
    // Last two arms repeat the exact policy and native implementation.
    const std::array policies{Policy::native_raw,Policy::native_for,Policy::reencode,Policy::overlay,
        Policy::overlay_fold,Policy::thaw,Policy::native_for,Policy::overlay_fold};
    std::mt19937 order_random(1729);
    const unsigned arms = policies.size(), trials = (requested + arms - 1) / arms * arms;
    for (const auto pattern : {"repeated-hot","dispersed","moving-hot","novelty"})
        for (const auto mask : {UINT32_MAX,UINT32_C(0xaaaaaaaa),UINT32_C(0x0000ffff),UINT32_C(0)})
            for (bool old : {false,true}) {
                const auto s = scenario(initial,pattern,{mask});
                std::vector<std::vector<unsigned>> orders;
                for (unsigned trial = 0; trial < trials; ++trial) {
                    if (trial % arms == 0) orders = frame_measure::balanced_round(arms,order_random);
                    for (unsigned pos = 0; pos < policies.size(); ++pos) {
                        const auto arm = orders[trial % arms][pos]; const auto policy = policies[arm];
                        if (policy == Policy::overlay || policy == Policy::overlay_fold)
                            run<p::OverlayLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,{});
                        else if (policy == Policy::native_raw || policy == Policy::native_for)
                            run<p::NativeLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,{});
                        else run<p::EncodedLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,{});
                    }
                }
            }
}
