// Complete native publication phases. Private research controls, not a policy.
#include "phase_construction.h"
#include "phase_owner.h"
#include "phase_repair.h"
#include "phase_batch.h"
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
using p::Edit;
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
    std::uint64_t max_update_ns{}, max_transition_ns{}, max_release_ns{};
    std::size_t copied_bytes{}, encoded_rows{}, peak_nonidle{}, peak_metadata{}, overlay_entries{}, repaired_blocks{};
    std::size_t write_materialized_rows{},repair_examined_rows{},eligible_dirty_blocks{};
    std::size_t coalesced_away{},visible_noops{},publications{},conversions{};
    std::array<std::size_t,7> dirty_kinds{};
    std::array<std::size_t,9> repair_reasons{};
};
enum class Policy { native_raw, native_for, reencode, overlay, overlay_fold, thaw, block_fold, repair_fold, sparse_repair, native_coalesced,
    sparse_indexed, overlay_batch, overlay_batch_fold, sparse_inline, overlay_predictor_fold, overlay_zero_fold };
const char* name(Policy policy) {
    switch (policy) {
    case Policy::native_raw:return "native-raw64"; case Policy::native_for:return "native-for";
    case Policy::reencode:return "whole-group-reencode"; case Policy::overlay:return "base-overrides-debt";
    case Policy::overlay_fold:return "base-overrides-fold"; case Policy::thaw:return "thaw-reseal";
    case Policy::block_fold:return "changed-block-fold";
    case Policy::repair_fold:return "repair-or-block-fold";
    case Policy::sparse_repair:return "sparse-repair-or-fold";
    case Policy::native_coalesced:return "native-for-coalesced";
    case Policy::sparse_indexed:return "indexed-sparse-repair";
    case Policy::overlay_batch:return "batch-overrides-debt";
    case Policy::overlay_batch_fold:return "batch-overrides-fold";
    case Policy::sparse_inline:return "inline-plan-sparse-repair";
    case Policy::overlay_predictor_fold:return "batch-overrides-predictor-fold";
    case Policy::overlay_zero_fold:return "batch-overrides-zero-fold";
    }
    std::abort();
}
// Exact duplicates: native FOR (1/10), old fold (4/11), batch overrides (13/15), inline repair (16/17).
constexpr std::array policies{Policy::native_raw,Policy::native_for,Policy::reencode,Policy::overlay,
    Policy::overlay_fold,Policy::thaw,Policy::block_fold,Policy::repair_fold,Policy::sparse_repair,
    Policy::native_coalesced,Policy::native_for,Policy::overlay_fold,Policy::sparse_indexed,
    Policy::overlay_batch,Policy::overlay_batch_fold,Policy::overlay_batch,Policy::sparse_inline,Policy::sparse_inline,
    Policy::overlay_predictor_fold,Policy::overlay_zero_fold};

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
    bool record_capacity=true;
    explicit Machine(Policy mode, p::fe::encode_options opts) : options(opts), policy(mode) {}
    ~Machine() { store = {}; require(!memory.live, "phase stranded metadata"); }
    void observe() {
        if (!record_capacity) return;
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
        ++costs.publications;
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
        else {
            std::optional<p::fe::null_fill> fill;
            if (policy==Policy::overlay_predictor_fold) fill=p::fe::null_fill::predictor;
            if (policy==Policy::overlay_zero_fold) fill=p::fe::null_fill::zero;
            copy(p::EncodedConstruction(data, options,fill)); costs.encoded_rows += p::rows_per_group;
        }
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
        costs.write_materialized_rows+=p::rows_per_group;
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
            if (policy==Policy::native_coalesced && old_present==e.next.present && old_valid==new_valid) {
                if (!new_valid || p::point(p::base(c.group[g]),original_values,original_flags,i).value==e.next.value) {
                    ++costs.visible_noops;
                    continue;
                }
            }
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
    template<class Plan=p::BlockRepair>
    void fold_blocks(C& c, Fresh& f, Selection source, unsigned g, std::span<const Edit> edits,
                     p::BlockSelection mask,const p::IndexedBatch* batch=nullptr) {
        if constexpr (!std::is_same_v<Group, p::EncodedBase>) std::abort();
        else {
            const auto& old = source.realization->control.group[g]; auto& out = c.group[g];
            const auto body = part(source, g * Group::parts), flags = part(source, g * Group::parts + 1);
            struct Replacement { p::fe::column image; };
            std::array<std::optional<Replacement>, p::blocks_per_group> replacement;
            std::array<std::optional<Plan>, p::blocks_per_group> repair;
            std::span<std::byte> changed_flags;
            bool changed_values = false;
            const auto commit_flags = [&](unsigned b,const auto& present,const auto& valid,
                                           const auto& old_present,const auto& old_valid) {
                if (present == old_present && valid == old_valid) return;
                if (changed_flags.empty()) {
                    changed_flags = allocate(f, c, g * Group::parts + 1, flags.size);
                    std::memcpy(changed_flags.data(), flags.data, flags.size); costs.copied_bytes += flags.size;
                }
                std::memcpy(changed_flags.data() + b * 32, present.data(), 32);
                std::memcpy(changed_flags.data() + p::flag_bytes + b * 32, valid.data(), 32);
            };
            for (unsigned b = 0; b < p::blocks_per_group; ++b) {
                const auto block_edits=batch?batch->block(g*p::blocks_per_group+b):edits;
                bool dirty = false;
                for (const auto& e : block_edits) dirty |= e.row / 256 == g * p::blocks_per_group + b && mask.row(e.row);
                if (!dirty) continue;
                ++costs.dirty_kinds[unsigned(old.blocks[b].type)];
                costs.eligible_dirty_blocks+=!(old.coded>>b&1) &&
                    (old.blocks[b].type==p::fe::kind::packed || old.blocks[b].type==p::fe::kind::delta);
                std::array<p::u64, 4> present, valid;
                std::memcpy(present.data(), flags.data + b * 32, 32);
                std::memcpy(valid.data(), flags.data + p::flag_bytes + b * 32, 32);
                const auto old_present = present, old_valid = valid;
                p::BlockAssignments assignments;
                for (const auto& e : block_edits) if (e.row / 256 == g * p::blocks_per_group + b && mask.row(e.row)) {
                    const auto row = e.row % 256, word = row / 64; const auto bit = p::u64{1} << (row % 64);
                    present[word] = (present[word] & ~bit) | (e.next.present ? bit : 0);
                    valid[word] = (valid[word] & ~bit) | (e.next.contributes() ? bit : 0);
                    if (policy==Policy::sparse_repair || policy==Policy::sparse_indexed || policy==Policy::sparse_inline) {
                        if (e.next.contributes()) assignments.set(row,e.next.value);
                        else assignments.clear(row);
                    }
                }
                if (policy==Policy::sparse_repair || policy==Policy::sparse_indexed || policy==Policy::sparse_inline) {
                    Plan planned;
                    const bool fits=p::plan_sparse_repair(p::borrow(old,body,b),assignments,planned);
                    ++costs.repair_reasons[unsigned(planned.reason)];costs.repair_examined_rows+=planned.examined_positions;
                    if (fits) {
                        out.header.present_count[b]=out.header.valid_count[b]=0;
                        for (unsigned word=0;word<4;++word) {
                            out.header.present_count[b]+=std::popcount(present[word]);
                            out.header.valid_count[b]+=std::popcount(valid[word]);
                            auto bits=assignments.assigned[word];
                            while (bits) {
                                const auto row=word*64+std::countr_zero(bits);bits&=bits-1;
                                const auto value=assignments.values[row];auto& bounds=out.header.bounds[b];
                                bounds.minimum=std::min(bounds.minimum,value);bounds.maximum=std::max(bounds.maximum,value);
                            }
                        }
                        commit_flags(b,present,valid,old_present,old_valid);
                        const bool bytes_changed=!planned.plane.empty() || !planned.checkpoints.empty();
                        const bool anchor_changed=planned.anchor!=out.blocks[b].base;
                        out.blocks[b].base=planned.anchor;
                        if (bytes_changed) {repair[b].emplace(std::move(planned));changed_values=true;}
                        if (bytes_changed || anchor_changed) ++costs.repaired_blocks;
                        continue;
                    }
                }
                std::array<p::u64, 256> before, after;
                costs.write_materialized_rows+=256;
                p::fe::decode_block(p::borrow(old, body, b), before.data()); after = before;
                for (const auto& e : block_edits) if (e.row / 256 == g * p::blocks_per_group + b && mask.row(e.row) && e.next.contributes())
                    after[e.row%256]=e.next.value;
                p::summarize_block(out.header, b, after, present, valid);
                commit_flags(b,present,valid,old_present,old_valid);
                if (after == before) continue; // NULL-only changes retain the actual value Part.
                if (policy==Policy::repair_fold) {
                    Plan planned;
                    const bool fits=p::plan_repair(p::borrow(old,body,b),before,after,planned);
                    ++costs.repair_reasons[unsigned(planned.reason)];costs.repair_examined_rows+=planned.examined_positions;
                    if (fits) {
                        out.blocks[b].base=planned.anchor;
                        repair[b].emplace(std::move(planned));
                        ++costs.repaired_blocks; changed_values=true; continue;
                    }
                }
                auto block_options = options; block_options.delta_checkpoints = old.checkpoints;
                block_options.dictionary = p::fe::dictionary_order::none; block_options.dictionary_every_part = false;
                p::fm::EncodedRegion encoded(after, block_options, 0, 0);
                out.blocks[b] = encoded.metadata.blocks[0].descriptor;
                out.grants[b] = encoded.metadata.blocks[0].grant_bytes;
                replacement[b].emplace(Replacement{std::move(encoded.staging)});
                out.coded &= ~(1u << b); costs.encoded_rows += 256; changed_values = true;
            }
            out.header.identity = p::fresh_identity();
            if (!changed_values) return;
            if (!out.coded) out.dictionary = {}; // No remaining block depends on the old codes.
            unsigned size = 8 + out.dictionary.grant_bytes;
            for (unsigned b = 0; b < p::blocks_per_group; ++b) {
                out.blocks[b].offset = size; size += out.grants[b];
            }
            out.header.bytes = size;
            auto values = allocate(f, c, g * Group::parts, size);
            std::memcpy(values.data(), &p::object_tag, 8);
            if (out.dictionary.count) {
                out.dictionary.offset = 8;
                std::memcpy(values.data() + 8, body.data + old.dictionary.offset, out.dictionary.grant_bytes);
            }
            for (unsigned b = 0; b < p::blocks_per_group; ++b) if (out.grants[b]) {
                const auto* original = replacement[b]
                    ? reinterpret_cast<const std::byte*>(p::fe::borrow(replacement[b]->image, 0).body)
                    : body.data + old.blocks[b].offset;
                std::memcpy(values.data() + out.blocks[b].offset, original, out.grants[b]);
                if (repair[b]) p::apply_repair(*repair[b],out.blocks[b].width,
                    reinterpret_cast<std::uint8_t*>(values.data())+out.blocks[b].offset);
            }
            costs.copied_bytes += values.size();
        }
    }
    void update(std::span<const Edit> edits, p::BlockSelection mask) {
        if (!mask.bits) return;
        const auto t = Clock::now();
        typename Store::Scope scope(store); const auto source = selected(scope);
        auto c = source.realization->control; Fresh f;
        std::optional<p::IndexedBatch> indexed;
        if (policy==Policy::sparse_indexed || policy==Policy::overlay_batch || policy==Policy::overlay_batch_fold ||
            policy==Policy::overlay_predictor_fold || policy==Policy::overlay_zero_fold)
            indexed.emplace(edits,mask);
        std::vector<Edit> coalesced;
        if (policy==Policy::native_coalesced) {
            // Sorting and allocation are charged. Original ordinal breaks ties
            // so the last assignment wins; even a visible no-op still publishes.
            std::vector<unsigned> order(edits.size());std::iota(order.begin(),order.end(),0);
            std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b) {
                return edits[a].row!=edits[b].row?edits[a].row<edits[b].row:a<b;
            });
            coalesced.reserve(edits.size());
            std::size_t selected_commands=0;
            for (auto i:order) {
                if (!mask.row(edits[i].row)) continue;
                ++selected_commands;
                if (!coalesced.empty() && coalesced.back().row==edits[i].row) coalesced.back()=edits[i];
                else coalesced.push_back(edits[i]);
            }
            costs.coalesced_away+=selected_commands-coalesced.size();
            edits=coalesced;
        }
        for (unsigned i = 0; i < slots; ++i) f.source[i] = take(orbital::source(source, i));
        bool changed = false;
        for (unsigned g = 0; g < p::groups; ++g) {
            bool dirty = false;
            for (const auto& e : edits) dirty |= e.row / p::rows_per_group == g && mask.row(e.row);
            if (!dirty) continue; changed = true;
            if constexpr (Group::parts == 3) {
                auto& group = c.group[g]; const auto& b = group.frozen;
                std::vector<p::OverrideRow> entries;
                if (indexed) entries=p::merge_overrides(group.changes,part(source,g*Group::parts+2),
                    indexed->group(g),g*p::rows_per_group,indexed->distinct[g],
                    [&](unsigned row){return p::point(b,part(source,g*Group::parts),part(source,g*Group::parts+1),row);});
                else {
                entries = overlays(group.changes, part(source, g * Group::parts + 2));
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
                }
                group.changes.count = entries.size();
                auto bytes = allocate(f, c, g * Group::parts + 2, p::Overlay::bytes(entries.size()));
                p::copy_overlay(entries, bytes, group.changes.basis); costs.copied_bytes += bytes.size();
                costs.overlay_entries = std::max(costs.overlay_entries, entries.size());
            } else if (p::base(c.group[g]).header.native_bytes) update_native(c, f, source, g, edits, mask);
            else if (policy==Policy::sparse_inline) fold_blocks<p::InlineBlockRepair>(c,f,source,g,edits,mask);
            else if (policy == Policy::block_fold || policy==Policy::repair_fold || policy==Policy::sparse_repair || policy==Policy::sparse_indexed)
                fold_blocks(c, f, source, g, edits, mask,indexed?&*indexed:nullptr);
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
        costs.conversion_ns += ns(start); ++costs.conversions; observe();
    }
};

struct Scenario {
    std::vector<std::vector<Edit>> batches;
    std::vector<Data> snapshots;
    p::BlockSelection mask;
    std::string pattern;
};
Scenario scenario(const Data& initial, std::string pattern, p::BlockSelection mask,bool mixed=true,
                  unsigned publications=8) {
    Scenario s; s.pattern = pattern; s.mask = mask; s.snapshots.push_back(initial);
    std::mt19937_64 random(713);
    for (unsigned b = 0; b < publications; ++b) {
        std::vector<Edit> edits; auto current = s.snapshots.back();
        for (unsigned i = 0; i < 64; ++i) {
            const unsigned row = pattern == "repeated-hot" ? i % 8 :
                pattern == "moving-hot" ? (b * 257 + i % 32) % p::rows : unsigned(random() % p::rows);
            auto next = initial[row / p::rows_per_group].row(row % p::rows_per_group);
            if (pattern == "novelty") next.value = random();
            else next.value += (i + b) % 3;
            next.present = !mixed || i % 31 != 0;
            next.valid = next.present && (!mixed || i % 17 != 0);
            edits.push_back({row, next});
            if (mask.row(row)) current[row / p::rows_per_group].set(row % p::rows_per_group, next);
        }
        s.batches.push_back(std::move(edits)); s.snapshots.push_back(std::move(current));
    }
    return s;
}

void check_block_fold(Policy policy=Policy::block_fold) {
    Data original; std::mt19937_64 random(1729);
    for (unsigned g = 0; g < p::groups; ++g) for (unsigned b = 0; b < p::blocks_per_group; ++b)
        for (unsigned i = 0; i < 256; ++i) {
            auto& value = original[g].value[b * 256 + i];
            switch (b % 6) {
            case 0:value = 77; break;
            case 1:value = (i / 19) * 7; break;
            case 2:value = random() & 8191; break;
            case 3:value = 100000 + i * 7 + (i % 3); break;
            case 4:value = 100000 - i * 7 - (i % 3); break;
            default:value = random(); break;
            }
        }
    unsigned long long checks = 0;
    for (unsigned cp = 0; cp < 4; ++cp)
        for (auto dictionary : {p::fe::dictionary_order::none, p::fe::dictionary_order::sorted,
                                 p::fe::dictionary_order::frequency}) {
            Machine<p::EncodedLayout> machine(policy, {.delta_checkpoints=p::fe::checkpoints(cp),
                .delta_exceptions=true,.dictionary=dictionary,.dictionary_every_part=dictionary!=p::fe::dictionary_order::none});
            machine.seed(original);
            using Scope = Machine<p::EncodedLayout>::Store::Scope;
            Scope old(machine.store); const auto first = machine.selected(old);
            const auto verify = [&](const Scope& scope, const Data& wanted) {
                const auto source = machine.selected(scope);
                for (unsigned g = 0; g < p::groups; ++g)
                    require(p::admit(source.realization->control.group[g], machine.part(source,g*2), machine.part(source,g*2+1)),
                            "block-fold result lost native admission");
                for (unsigned i = 0; i < p::rows; ++i) {
                    const auto actual = machine.point(source,i), expected = wanted[i/p::rows_per_group].row(i%p::rows_per_group);
                    require(actual.present==expected.present && actual.valid==expected.valid &&
                        (!expected.contributes() || actual.value==expected.value),"block-fold original point differs");
                    ++checks;
                }
                for (auto mask : {UINT32_MAX,UINT32_C(0xaaaaaaaa),UINT32_C(0x0000ffff),UINT32_C(0)})
                    for (auto threshold : {p::u64{0},p::u64{500},UINT64_MAX}) {
                        p::QueryCache cache(threshold);
                        require(machine.query(source,{mask},threshold,cache)==expected(wanted,{mask},threshold),
                                "block-fold aggregate differs"); ++checks;
                    }
            };
            auto current = original;
            const std::array null_edit{Edit{17,{0,true,false}}}; current[0].set(17,null_edit[0].next);
            const auto copied_before = machine.costs.copied_bytes;
            machine.update(null_edit,{});
            { Scope now(machine.store); const auto next=machine.selected(now);
              require(machine.part(first,0).data==machine.part(next,0).data &&
                      machine.costs.copied_bytes-copied_before==p::flag_bytes*2,
                      "NULL-only block fold copied a value Part"); verify(now,current); }
            if ((policy==Policy::repair_fold || policy==Policy::sparse_repair || policy==Policy::sparse_indexed || policy==Policy::sparse_inline) && dictionary==p::fe::dictionary_order::none) {
                const unsigned row=3*256;
                require(first.realization->control.group[0].blocks[3].type==p::fe::kind::delta,
                        "fitting native fixture lost its delta block");
                const std::array fitting{Edit{row,{current[0].value[row]+1,true,true}},
                    Edit{row+15,{current[0].value[row+15]+1,true,true}}};
                const auto encoded_before=machine.costs.encoded_rows, repaired_before=machine.costs.repaired_blocks;
                for (const auto& e:fitting) current[0].set(e.row,e.next);
                machine.update(fitting,{});
                require(machine.costs.encoded_rows==encoded_before && machine.costs.repaired_blocks==repaired_before+1,
                        "fitting native delta update fell back to encoding");
                Scope now(machine.store);verify(now,current);
            }
            const auto& old_group=first.realization->control.group[1];
            const unsigned changed = old_group.coded ? std::countr_zero(old_group.coded) : 3;
            if (dictionary!=p::fe::dictionary_order::none)
                require(std::popcount(old_group.coded)>=2,"dictionary fixture lost a retained coded block");
            const unsigned row=p::rows_per_group+changed*256+31;
            const std::array edits{Edit{row,{UINT64_MAX,true,true}},Edit{row+1,{0,false,false}},
                Edit{row,{UINT64_MAX-1,true,true}},Edit{row+257,{12,true,true}}};
            for (const auto& e:edits) if (p::BlockSelection{UINT32_C(0x00010000)<<changed}.row(e.row))
                current[e.row/p::rows_per_group].set(e.row%p::rows_per_group,e.next);
            const auto encoded_before=machine.costs.encoded_rows;
            machine.update(edits,{UINT32_C(0x00010000)<<changed});
            { Scope now(machine.store); const auto next=machine.selected(now);
              const auto& group=next.realization->control.group[1];
              require(machine.costs.encoded_rows-encoded_before==256,"block fold re-encoded an unselected neighbour");
              require(!(group.coded>>changed&1),"changed block retained an obsolete dictionary code interpretation");
              if (old_group.coded) require(group.dictionary.edition==old_group.dictionary.edition &&
                  group.dictionary.grant_bytes==old_group.dictionary.grant_bytes &&
                  !std::memcmp(machine.part(first,2).data+old_group.dictionary.offset,
                               machine.part(next,2).data+group.dictionary.offset,group.dictionary.grant_bytes),
                  "unchanged blocks lost their dictionary dependency");
              for (unsigned b=0;b<p::blocks_per_group;++b) if (b!=changed)
                  require(group.grants[b]==old_group.grants[b] && (!group.grants[b] ||
                      !std::memcmp(machine.part(first,2).data+old_group.blocks[b].offset,
                                   machine.part(next,2).data+group.blocks[b].offset,group.grants[b])),
                      "block fold changed retained neighbour bytes");
              verify(now,current); }
            verify(old,original);
        }
    std::printf("%s: %llu checks pass: four CP modes, raw/sorted/frequency, NULL-only reuse,"
                " original masks, repeated edits, retained neighbours/dictionaries and old readers.\n",name(policy),checks);
}
void check_native_coalesced() {
    Data original;original[0].value.fill(1000);
    Machine<p::NativeLayout> machine(Policy::native_coalesced,{});machine.seed(original);
    using Scope=Machine<p::NativeLayout>::Store::Scope;
    Scope old(machine.store);const auto first=machine.selected(old);
    std::vector<Edit> edits;
    for (unsigned i=0;i<64;++i) edits.push_back({i%2,{UINT64_MAX,true,i%2==0}});
    edits[62]={0,{1000,true,true}};edits[63]={1,{1000,true,true}};
    const auto copies=machine.costs.copied_bytes;
    machine.update(edits,{});
    { Scope now(machine.store);const auto next=machine.selected(now);
      require(now.cut()==2 && machine.costs.coalesced_away==62 && machine.costs.visible_noops==2,
              "native coalescing lost ordered/no-op publication semantics");
      require(machine.costs.copied_bytes==copies && machine.part(first,0).data==machine.part(next,0).data &&
              next.realization->control.group[0].header.native_bytes==1,
              "discarded intermediate assignment widened or copied native values"); }
    const std::array flags{Edit{0,{0,true,false}},Edit{1,{0,false,false}}};
    auto current=original;for (const auto& e:flags) current[0].set(e.row,e.next);
    machine.update(flags,{});
    { Scope now(machine.store);const auto next=machine.selected(now);p::QueryCache cache(1000);
      require(machine.costs.copied_bytes-copies==p::flag_bytes*2 &&
              machine.part(first,0).data==machine.part(next,0).data,"native flags-only update copied value plane");
      require(machine.query(next,{},1000,cache)==expected(current,{},1000) &&
              machine.query(first,{},1000,cache)==expected(original,{},1000),"native coalesced retained query differs"); }
    std::puts("Native coalescing passes: latest assignment, no-op publication, no transient widening, flags-only reuse and old reader.");
}

void check_batch_overrides(Policy policy=Policy::overlay_batch) {
    unsigned long long checks=0;
    Data initial;
    for (unsigned g=0;g<p::groups;++g) for (unsigned row=0;row<p::rows_per_group;++row)
        initial[g].value[row]=(row/19)*7+g*1000;
    for (unsigned cp=0;cp<4;++cp) for (auto dictionary:{p::fe::dictionary_order::none,
            p::fe::dictionary_order::sorted,p::fe::dictionary_order::frequency}) {
        Machine<p::OverlayLayout> m(policy,{.delta_checkpoints=p::fe::checkpoints(cp),
            .dictionary=dictionary,.dictionary_every_part=dictionary!=p::fe::dictionary_order::none});
        m.seed(initial);using Scope=Machine<p::OverlayLayout>::Store::Scope;
        Scope old(m.store);const auto original=m.selected(old);auto current=initial;
        const auto verify=[&](const Scope& scope,const Data& wanted) {
            const auto source=m.selected(scope);
            for (unsigned g=0;g<p::groups;++g) {
                const auto& group=source.realization->control.group[g];
                require(p::admit(group.frozen,m.part(source,g*3),m.part(source,g*3+1)) &&
                    p::admit(group.changes,m.part(source,g*3+2)),"batch overlay lost source admission");
            }
            for (unsigned row=0;row<p::rows;++row) {
                require(p::same_logical(m.point(source,row),wanted[row/p::rows_per_group].row(row%p::rows_per_group)),
                    "native batch overlay differs from sequential assignment");++checks;
            }
            for (auto bits:{UINT32_MAX,0xaaaaaaaau,0x0000ffffu,0u}) for (auto rhs:{p::u64{0},p::u64{127},UINT64_MAX}) {
                p::QueryCache cache(rhs);
                require(m.query(source,{bits},rhs,cache)==expected(wanted,{bits},rhs),"native batch correction differs");++checks;
            }
        };
        for (unsigned batch=0;batch<3;++batch) {
            std::vector<Edit> edits;
            const p::BlockSelection mask{batch==1?0xaaaaaaaau:UINT32_MAX};
            for (unsigned i=0;i<193;++i) {
                const unsigned row=(i*1021+batch*17)%p::rows;
                auto next=initial[row/p::rows_per_group].row(row%p::rows_per_group);
                next.value+=i%7;next.present=i%13!=0;next.valid=next.present && i%17!=0;
                edits.push_back({row,next});
            }
            edits.push_back({17,{UINT64_MAX,true,true}});
            edits.push_back({17,{0,true,false}});
            edits.push_back({17,initial[0].row(17)}); // Remove a correction to the immutable base.
            for (const auto& e:edits) if (mask.row(e.row)) current[e.row/p::rows_per_group].set(e.row%p::rows_per_group,e.next);
            m.update(edits,mask);
            {Scope now(m.store);const auto source=m.selected(now);verify(now,current);
             for (unsigned g=0;g<p::groups;++g) for (unsigned field=0;field<2;++field)
                 require(m.part(source,g*3+field).data==m.part(original,g*3+field).data,
                     "batch correction copied the immutable base or its flags");}
            verify(old,initial);
        }
        const auto pubs=m.costs.publications,copies=m.costs.copied_bytes;
        const std::array excluded{Edit{0,{123,true,true}}};m.update(excluded,{0});
        require(m.costs.publications==pubs && m.costs.copied_bytes==copies,"excluded batch published or copied");
        m.convert(false,{});
        Scope folded(m.store);const auto before=current;verify(folded,before);
        const std::array next{Edit{17,{1234,true,true}},Edit{p::rows-1,{5678,true,true}}};
        for (const auto& e:next) current[e.row/p::rows_per_group].set(e.row%p::rows_per_group,e.next);
        m.update(next,{}); // A genuine next publication clones the folded alternative.
        {Scope now(m.store);require(now.cut()==5,"fold/successor changed logical publication count");verify(now,current);}
        verify(folded,before);verify(old,initial);
    }
    std::printf("%s: %llu native batch checks pass: all CP/dictionaries, ordered assignments, immutable-base reuse, masked/old/folded readers and genuine successor publication.\n",name(policy),checks);
}

struct CycleOptions {
    unsigned fold_every{};
    // Zero retains the original reader throughout; positive ages renew it
    // before the next update. These are real readers, not fake publications.
    unsigned reader_age{};
};
template<class C> void run(Policy policy, const Data& initial, const Scenario& s, unsigned trial,
                           unsigned order, unsigned arm, bool keep_old, unsigned read_queries,
                           unsigned final_queries, bool verify_points, bool reference_queries, p::fe::encode_options options,
                           bool capacity=false, CycleOptions cycle={}) {
    Machine<C> m(policy, options);
    m.record_capacity=capacity || verify_points;
    const auto setup = Clock::now(); m.seed(initial); const auto setup_ns = ns(setup);
    m.costs.prepare_ns = m.costs.publish_ns = m.costs.copied_bytes = m.costs.encoded_rows = 0;
    m.costs.publications=0;
    using Scope = typename Machine<C>::Store::Scope;
    std::unique_ptr<Scope> old;
    if (keep_old) old = std::make_unique<Scope>(m.store);
    unsigned old_snapshot=0,reader_opened_at=0;
    std::optional<std::size_t> folded_at;
    const auto threshold = initial[0].value[71];
    std::vector<p::QueryResult> answers;
    for (const auto& data : s.snapshots) answers.push_back(expected(data, s.mask, threshold));
    const auto verify = [&](const Scope& scope, p::QueryResult wanted, unsigned repetitions) {
        const auto selected = m.selected(scope); p::QueryCache cache(threshold,reference_queries);
        for (unsigned q = 0; q < repetitions; ++q) {
            const auto start = Clock::now(); const auto actual = m.query(selected, s.mask, threshold, cache);
            m.costs.query_ns += ns(start); p::fe::consume(actual.sum);
            require(actual == wanted, "phase query differs from original logical oracle");
        }
    };
    const auto all_points = [&](const Scope& scope, const Data& wanted) {
        const auto source = m.selected(scope);
        const auto& control = source.realization->control;
        for (unsigned g = 0; g < p::groups; ++g) {
            const auto i = g * Machine<C>::Group::parts;
            require(p::admit(p::base(control.group[g]), m.part(source, i), m.part(source, i + 1)),
                    "published phase image lost admission");
        }
        for (unsigned i = 0; i < p::rows; ++i) {
            const auto actual = m.point(source, i), original = wanted[i / p::rows_per_group].row(i % p::rows_per_group);
            require(actual.present == original.present && actual.valid == original.valid &&
                (!original.contributes() || actual.value == original.value), "phase point differs from original coordinates");
        }
    };
    const auto start = Clock::now();
    const auto transition = [&](bool native) {
        const auto at=Clock::now(); m.convert(native,s.mask);
        m.costs.max_transition_ns=std::max(m.costs.max_transition_ns,ns(at));
    };
    const bool fold_at_end=policy==Policy::overlay_fold || policy==Policy::overlay_batch_fold ||
        policy==Policy::overlay_predictor_fold || policy==Policy::overlay_zero_fold || policy==Policy::thaw;
    require(!cycle.fold_every || (fold_at_end && policy!=Policy::thaw),"periodic fold needs an override program");
    if (policy == Policy::thaw) transition(true);
    for (unsigned b = 0; b < s.batches.size(); ++b) {
        if (old && cycle.reader_age && m.costs.publications-reader_opened_at>=cycle.reader_age) {
            const auto release=Clock::now();old.reset();m.store.drain();
            const auto duration=ns(release);m.costs.release_ns+=duration;
            m.costs.max_release_ns=std::max(m.costs.max_release_ns,duration);
            old=std::make_unique<Scope>(m.store);old_snapshot=b;reader_opened_at=m.costs.publications;
        }
        const auto previous_publications=m.costs.publications;
        const auto at=Clock::now();
        m.update(s.batches[b], s.mask);
        m.costs.max_update_ns=std::max(m.costs.max_update_ns,ns(at));
        if (cycle.fold_every && m.costs.publications!=previous_publications &&
            m.costs.publications%cycle.fold_every==0) {
            transition(false);folded_at=m.costs.publications;
        }
        { Scope current(m.store); verify(current, answers[b + 1], read_queries);
          if (verify_points && !trial) all_points(current, s.snapshots[b + 1]); }
        if (old) { verify(*old, answers[old_snapshot], 1);
          if (verify_points && !trial) all_points(*old, s.snapshots[old_snapshot]); }
        const auto release = Clock::now(); m.store.drain(); const auto duration=ns(release);
        m.costs.release_ns += duration; m.costs.max_release_ns=std::max(m.costs.max_release_ns,duration);
    }
    if (fold_at_end && (!cycle.fold_every ||
        (m.costs.publications && folded_at!=m.costs.publications))) transition(false);
    { Scope current(m.store); verify(current, answers.back(), final_queries);
      if (verify_points && !trial) all_points(current, s.snapshots.back()); }
    const auto release = Clock::now(); old.reset(); m.store.drain(); const auto duration=ns(release);
    m.costs.release_ns += duration; m.costs.max_release_ns=std::max(m.costs.max_release_ns,duration);
    const auto elapsed = ns(start); m.observe();
    m.costs.peak_metadata=m.memory.peak; // Cheap PMR high water needs no Pool census.
    const auto stats = a::statistics(*m.runtime.pool);
    std::printf("{\"policy\":\"%s\",\"pattern\":\"%s\",\"mask\":%u,\"retain_old\":%u,\"trial\":%u,\"order_position\":%u,"
        "\"reference_queries\":%u,\"capacity_accounting\":%u,\"arm\":%u,\"read_queries_per_batch\":%u,\"final_read_queries\":%u,\"verification_run\":%u,"
        "\"fold_every\":%u,\"reader_age\":%u,\"batches\":%zu,\"conversions\":%zu,"
        "\"max_update_ns\":%llu,\"max_transition_ns\":%llu,\"max_release_ns\":%llu,"
        "\"total_ns\":%llu,\"setup_ns\":%llu,\"prepare_ns\":%llu,\"publish_ns\":%llu,\"query_ns\":%llu,\"conversion_ns\":%llu,\"release_ns\":%llu,"
        "\"copied_bytes\":%zu,\"encoded_rows\":%zu,\"repaired_blocks\":%zu,\"peak_nonidle_bytes\":%zu,\"peak_metadata_bytes\":%zu,\"overlay_entries\":%zu,"
        "\"write_materialized_rows\":%zu,\"repair_examined_rows\":%zu,\"eligible_dirty_blocks\":%zu,"
        "\"coalesced_away\":%zu,\"visible_noops\":%zu,\"publications\":%zu,"
        "\"dirty_kinds\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu],\"repair_reasons\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu],"
        "\"final_nonidle_bytes\":%llu,\"pool_mapped_bytes\":%llu,\"pool_idle_bytes\":%llu,\"final_metadata_bytes\":%zu}\n",
        name(policy),s.pattern.c_str(),s.mask.bits,unsigned(keep_old),trial,order,
        unsigned(reference_queries),unsigned(m.record_capacity),arm,read_queries,final_queries,unsigned(verify_points),
        cycle.fold_every,cycle.reader_age,s.batches.size(),m.costs.conversions,
        (unsigned long long)m.costs.max_update_ns,(unsigned long long)m.costs.max_transition_ns,(unsigned long long)m.costs.max_release_ns,
        (unsigned long long)elapsed,(unsigned long long)setup_ns,(unsigned long long)m.costs.prepare_ns,
        (unsigned long long)m.costs.publish_ns,(unsigned long long)m.costs.query_ns,(unsigned long long)m.costs.conversion_ns,
        (unsigned long long)m.costs.release_ns,m.costs.copied_bytes,m.costs.encoded_rows,m.costs.repaired_blocks,m.costs.peak_nonidle,m.costs.peak_metadata,
        m.costs.overlay_entries,m.costs.write_materialized_rows,m.costs.repair_examined_rows,m.costs.eligible_dirty_blocks,
        m.costs.coalesced_away,m.costs.visible_noops,m.costs.publications,
        m.costs.dirty_kinds[0],m.costs.dirty_kinds[1],m.costs.dirty_kinds[2],m.costs.dirty_kinds[3],m.costs.dirty_kinds[4],m.costs.dirty_kinds[5],m.costs.dirty_kinds[6],
        m.costs.repair_reasons[0],m.costs.repair_reasons[1],m.costs.repair_reasons[2],m.costs.repair_reasons[3],m.costs.repair_reasons[4],m.costs.repair_reasons[5],m.costs.repair_reasons[6],m.costs.repair_reasons[7],m.costs.repair_reasons[8],
        (unsigned long long)(stats.mapped_bytes-stats.idle_bytes),(unsigned long long)stats.mapped_bytes,
        (unsigned long long)stats.idle_bytes,m.memory.live);
}
} // namespace
int main(int argc,char** argv) {
    if (argc>=2 && std::string_view(argv[1])=="--sustained") {
        require(argc==11 || argc==12,
            "--sustained INPUT PUBLICATIONS TRIALS QUERIES FINAL PATTERN READER_AGE TRACE MODE [MASK]");
        Data initial;std::ifstream input(argv[2],std::ios::binary);
        for (auto& g:initial) require(bool(input.read(reinterpret_cast<char*>(g.value.data()),sizeof(g.value))),
            "short sustained input");
        const auto publications=std::stoul(argv[3]),requested=std::stoul(argv[4]);
        const auto queries=std::stoul(argv[5]),final=std::stoul(argv[6]);
        const std::string pattern=argv[7];const auto age=std::stol(argv[8]);
        const bool mixed=std::string_view(argv[9])=="mixed";
        const std::string_view mode=argv[10];
        require(publications && publications<=512 && requested && requested<=72,"invalid sustained count");
        require(queries && queries<=4096 && final<=4096,"invalid sustained queries");
        require(age>=-1 && age<=long(publications),"invalid reader age (-1 none,0 whole history,positive renewal age)");
        require(mixed || std::string_view(argv[9])=="values-only","invalid sustained trace");
        require(mode=="plain" || mode=="capacity" || mode=="verify","invalid sustained accounting mode");
        require(pattern=="repeated-hot" || pattern=="dispersed" || pattern=="moving-hot" || pattern=="novelty",
            "invalid sustained pattern");
        const auto mask=argc==12?std::stoull(argv[11],nullptr,0):UINT32_MAX;
        require(mask<=UINT32_MAX,"invalid block mask");
        const auto s=scenario(initial,pattern,{std::uint32_t(mask)},mixed,publications);
        // Same owner and operation bodies. Exact duplicate arms0/7 and5/8;
        // odd Williams square balances position/predecessor twice in18 trials.
        struct Arm {Policy policy;unsigned fold;};
        constexpr std::array<Arm,9> arms{{{Policy::native_for,0},{Policy::native_coalesced,0},
            {Policy::sparse_inline,0},{Policy::overlay_batch,0},{Policy::overlay_batch_fold,8},
            {Policy::overlay_batch_fold,64},{Policy::thaw,0},{Policy::native_for,0},
            {Policy::overlay_batch_fold,64}}};
        const unsigned trials=(requested+17)/18*18;
        std::mt19937 random(1729);std::vector<std::vector<unsigned>> orders;
        for (unsigned trial=0;trial<trials;++trial) {
            if (trial%18==0) orders=frame_measure::balanced_round(arms.size(),random);
            for (unsigned position=0;position<arms.size();++position) {
                const auto index=orders[trial%18][position];const auto arm=arms[index];
                const CycleOptions cycle{arm.fold,unsigned(std::max(0l,age))};
                const auto invoke=[&]<class C>() {
                    run<C>(arm.policy,initial,s,trial,position,index,age>=0,queries,final,mode=="verify",
                        false,{},mode=="capacity",cycle);
                };
                if (arm.policy==Policy::native_for || arm.policy==Policy::native_coalesced)
                    invoke.template operator()<p::NativeLayout>();
                else if (arm.policy==Policy::overlay_batch || arm.policy==Policy::overlay_batch_fold)
                    invoke.template operator()<p::OverlayLayout>();
                else invoke.template operator()<p::EncodedLayout>();
            }
        }
        return 0;
    }
    if (argc==4 && std::string_view(argv[1])=="--accounting-probe") {
        Data initial;std::ifstream input(argv[2],std::ios::binary);
        for (auto& g:initial) require(bool(input.read(reinterpret_cast<char*>(g.value.data()),sizeof(g.value))),"short accounting input");
        const auto requested=std::stoul(argv[3]);require(requested && requested<=128,"invalid accounting trials");
        const unsigned trials=(requested+7)/8*8;
        const std::array modes{Policy::native_for,Policy::native_for,Policy::sparse_repair,Policy::sparse_repair,
            Policy::sparse_inline,Policy::sparse_inline,Policy::native_for,Policy::sparse_inline};
        std::mt19937 random(1729);
        for (const auto pattern:{"repeated-hot","dispersed"}) {
            const auto s=scenario(initial,pattern,{});std::vector<std::vector<unsigned>> order;
            for (unsigned trial=0;trial<trials;++trial) {
                if (trial%8==0) order=frame_measure::balanced_round(8,random);
                for (unsigned pos=0;pos<8;++pos) {
                    const auto arm=order[trial%8][pos];const bool capacity=arm==1 || arm==3 || arm==5;
                    if (modes[arm]==Policy::native_for)
                        run<p::NativeLayout>(modes[arm],initial,s,trial,pos,arm,false,1,0,false,false,{},capacity);
                    else run<p::EncodedLayout>(modes[arm],initial,s,trial,pos,arm,false,1,0,false,false,{},capacity);
                }
            }
        }
        return 0;
    }
    if (argc==2 && std::string_view(argv[1])=="--check-native-coalesced") {check_native_coalesced();return 0;}
    if (argc==2 && std::string_view(argv[1])=="--check-block-fold") { check_block_fold(); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--check-sparse-repair") { check_block_fold(Policy::sparse_repair); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--check-indexed-repair") { check_block_fold(Policy::sparse_indexed); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--check-inline-repair") { check_block_fold(Policy::sparse_inline); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--check-batch-overrides") { check_batch_overrides(); return 0; }
    if (argc==2 && std::string_view(argv[1])=="--check-nullable-fold") {
        check_batch_overrides(Policy::overlay_predictor_fold);check_batch_overrides(Policy::overlay_zero_fold);return 0;
    }
    if (argc==2 && std::string_view(argv[1])=="--check-repair-fold") { check_block_fold(Policy::repair_fold); return 0; }
    if (argc==6 && std::string_view(argv[1])=="--profile") {
        // Diagnostic single-program repetitions for external perf/DHAT tools.
        // Setup remains visible to those tools; use update call stacks to
        // distinguish it. Instrumented elapsed times are not benchmark data.
        Data initial;std::ifstream input(argv[2],std::ios::binary);
        for (auto& g:initial) require(bool(input.read(reinterpret_cast<char*>(g.value.data()),sizeof(g.value))),
            "need at least8192 original values");
        const auto found=std::find_if(policies.begin(),policies.end(),[&](auto policy){return name(policy)==std::string_view(argv[3]);});
        require(found!=policies.end(),"unknown profile policy");const auto policy=*found;
        const std::string pattern=argv[4];
        require(pattern=="repeated-hot" || pattern=="moving-hot" || pattern=="dispersed" || pattern=="novelty","unknown profile pattern");
        const auto repeats=std::stoul(argv[5]);require(repeats && repeats<=65536,"invalid profile phase count");
        const auto s=scenario(initial,pattern,{});
        for (unsigned repeat=0;repeat<repeats;++repeat) {
            if (policy==Policy::overlay || policy==Policy::overlay_fold || policy==Policy::overlay_batch || policy==Policy::overlay_batch_fold ||
                policy==Policy::overlay_predictor_fold || policy==Policy::overlay_zero_fold)
                run<p::OverlayLayout>(policy,initial,s,repeat,0,0,false,1,0,false,false,{});
            else if (policy==Policy::native_raw || policy==Policy::native_for || policy==Policy::native_coalesced)
                run<p::NativeLayout>(policy,initial,s,repeat,0,0,false,1,0,false,false,{});
            else run<p::EncodedLayout>(policy,initial,s,repeat,0,0,false,1,0,false,false,{});
        }
        return 0;
    }
    require(argc >= 2 && argc <= 10, "usage: frame_phase_measure INPUT.u64 [trials=8] [queries_per_batch=8] [final_queries=64] [verify=0] [mixed|values-only] [bounded] [masked|reference] [capacity|plain]");
    Data initial;
    std::ifstream input(argv[1],std::ios::binary);
    for (auto& g : initial) require(bool(input.read(reinterpret_cast<char*>(g.value.data()),sizeof(g.value))),"need at least8192 original values");
    const unsigned requested = argc > 2 ? std::stoul(argv[2]) : 8;
    const unsigned read_queries = argc > 3 ? std::stoul(argv[3]) : 8;
    const unsigned final_queries = argc > 4 ? std::stoul(argv[4]) : 64;
    const unsigned verification = argc > 5 ? std::stoul(argv[5]) : 0;
    const bool mixed=argc<=6 || std::string_view(argv[6])=="mixed";
    const bool bounded=argc>7;
    const bool reference_queries=argc>8 && std::string_view(argv[8])=="reference";
    const bool capacity=argc>9 && std::string_view(argv[9])=="capacity";
    require(argc<=9 || capacity || std::string_view(argv[9])=="plain","invalid accounting mode");
    require(argc<=8 || reference_queries || std::string_view(argv[8])=="masked","invalid query program");
    require(argc<=6 || mixed || std::string_view(argv[6])=="values-only","invalid trace kind");
    require(!bounded || std::string_view(argv[7])=="bounded","invalid case restriction");
    require(requested && requested <= 120,"invalid trial count");
    require(read_queries && read_queries <= 4096 && final_queries <= 4096 && verification <= 1,"invalid query/verification count");
    std::mt19937 order_random(1729);
    const unsigned arms = policies.size(), round = arms * (arms % 2 ? 2 : 1);
    const unsigned trials = (requested + round - 1) / round * round;
    for (const auto pattern : {"repeated-hot","dispersed","moving-hot","novelty"})
        for (const auto mask : {UINT32_MAX,UINT32_C(0xaaaaaaaa),UINT32_C(0x0000ffff),UINT32_C(0)})
            for (bool old : {false,true}) {
                if (bounded && (mask!=UINT32_MAX || old ||
                    (std::string_view(pattern)!="repeated-hot" && std::string_view(pattern)!="dispersed"))) continue;
                const auto s = scenario(initial,pattern,{mask},mixed);
                std::vector<std::vector<unsigned>> orders;
                for (unsigned trial = 0; trial < trials; ++trial) {
                    if (trial % round == 0) orders = frame_measure::balanced_round(arms,order_random);
                    for (unsigned pos = 0; pos < policies.size(); ++pos) {
                        const auto arm = orders[trial % round][pos]; const auto policy = policies[arm];
                        if (policy == Policy::overlay || policy == Policy::overlay_fold ||
                            policy==Policy::overlay_batch || policy==Policy::overlay_batch_fold ||
                            policy==Policy::overlay_predictor_fold || policy==Policy::overlay_zero_fold)
                            run<p::OverlayLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,reference_queries,{},capacity);
                        else if (policy == Policy::native_raw || policy == Policy::native_for || policy==Policy::native_coalesced)
                            run<p::NativeLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,reference_queries,{},capacity);
                        else run<p::EncodedLayout>(policy,initial,s,trial,pos,arm,old,read_queries,final_queries,verification,reference_queries,{},capacity);
                    }
                }
            }
}
