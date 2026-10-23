#include "envelope.h"
#include <ikea/tuplepack.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

namespace {
namespace fc = frame_change;
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
using fc::word;
using fc::wide;
constexpr unsigned rows = 256;
unsigned packed_cases = 0, delta_cases = 0, refusals = 0, guard_controls = 0;
void need(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "change envelope: %s\n", why); std::abort(); }
}
template <class T> auto take(T result) {
    need(bool(result), "unexpected refusal"); return std::move(*result);
}
void report(const char* name, const fc::envelope& p) {
    std::printf("{\"case\":\"%s\",\"declared_read_bytes\":%zu,\"possible_store_bytes\":%zu,"
                "\"serialization_bytes\":%zu,\"read_spans\":%zu,\"store_spans\":%zu,\"record_bound\":%zu}\n",
        name, p.reads.bytes(), p.stores.bytes(), p.serialization().bytes(),
        p.reads.spans.size(), p.stores.spans.size(), p.records);
}

struct mapping {
    std::byte* pages;
    std::byte* data;
    std::size_t page, bytes;
    explicit mapping(std::size_t n) : page(std::size_t(sysconf(_SC_PAGESIZE))), bytes(n) {
        need(n + 64 < page, "bounded page fixture");
        pages = static_cast<std::byte*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        need(pages != MAP_FAILED, "mmap");
        data = pages + page - n;
        need(mprotect(pages + page, page, PROT_NONE) == 0, "suffix guard");
    }
    ~mapping() { munmap(pages, page * 2); }
    mapping(const mapping&) = delete;
    mapping& operator=(const mapping&) = delete;
    void protect(int flags) { need(mprotect(pages, page, flags) == 0, "mprotect"); }
    std::span<std::byte> span() { return {data, bytes}; }
};

template <class L> struct block {
    using view_type = sp::view<L>;
    static constexpr unsigned rows = ::rows;
    static constexpr word maximum = (word{1} << L::width) - 1;
    word base = 1000;
    mapping storage{rows * L::width / 8};
    std::optional<view_type> view;
    block() {
        view.emplace(take(view_type::attach(rows, {{{storage.span(), L::tile_bytes}, {}, {}}})));
        reset();
    }
    // Independent bit coordinates for just these two declared wire laws. No
    // Ikea offset, footprint, getter, setter or initialization helper is used.
    static std::pair<unsigned, unsigned> bit(unsigned row, unsigned value_bit) {
        constexpr bool local = L::storage == sp::geometry::local;
        static_assert(local || L::width == 12);
        constexpr unsigned tile_rows = local ? 8 : 64;
        const unsigned lane = row % tile_rows, tile = row / tile_rows;
        constexpr unsigned tail = L::width % 8, body = L::width / 8;
        if (value_bit >= tail)
            return {tile * (tile_rows * L::width / 8) + lane * body + (value_bit - tail) / 8,
                    (value_bit - tail) % 8};
        if constexpr (local)
            return {tile * L::width + 8 * body + value_bit, lane};
        else
            return {tile * 96 + 64 + lane % 32, (lane / 32) * 4 + value_bit};
    }
    static void encode(std::span<std::byte> wire, unsigned row, word value) {
        for (unsigned b = 0; b < L::width; ++b) {
            auto [offset, shift] = bit(row, b);
            const auto old = std::to_integer<unsigned>(wire[offset]);
            wire[offset] = std::byte((old & ~(1u << shift)) | (((value >> b) & 1u) << shift));
        }
    }
    static word decode(std::span<const std::byte> wire, unsigned row) {
        word value = 0;
        for (unsigned b = 0; b < L::width; ++b) {
            auto [offset, shift] = bit(row, b);
            value |= word((std::to_integer<unsigned>(wire[offset]) >> shift) & 1u) << b;
        }
        return value;
    }
    fc::ranges independent_bytes(unsigned row) const {
        fc::ranges r;
        for (unsigned b = 0; b < L::width; ++b) r.add(storage.data + bit(row, b).first, 1);
        return r;
    }
    void reset() {
        storage.protect(PROT_READ | PROT_WRITE);
        std::memset(storage.data - 64, 0xa5, storage.bytes + 64);
        for (unsigned i = 0; i < rows; ++i) encode(storage.span(), i, (i * 17 + 31) & maximum);
    }
    void canary() const {
        for (unsigned i = 1; i <= 64; ++i) need(storage.data[-int(i)] == std::byte{0xa5}, "prefix canary");
    }
};

template <class View> fc::ranges resolve(const ikea::journal& journal, const View& view) {
    fc::ranges out;
    for (auto e : journal.entries()) {
        need(e.source == &view && e.plane == 0, "actual named point source");
        const auto bytes = view.plane_storage(e.plane);
        need(e.offset <= bytes.size() && e.size <= bytes.size() - e.offset, "effect bounds");
        out.add(bytes.data() + e.offset, e.size);
    }
    return out;
}

template <class L> void packed() {
    std::array<block<L>, 4> blocks;
    std::array<fc::replacement, 4> commands;
    std::array<ikea::journal::entry, 32> records;
    for (unsigned row = 0; row < rows; ++row) for (word coarse : {word{0}, word{5}, word{15}}) {
        std::array<std::vector<std::byte>, 4> expected;
        wide contribution = 0;
        fc::ranges independent;
        for (unsigned b = 0; b < blocks.size(); ++b) {
            auto& x = blocks[b]; x.reset();
            commands[b] = {row, x.base + ((row * 19 + b * 13 + 7) & x.maximum)};
            expected[b].assign(x.storage.data, x.storage.data + x.storage.bytes);
            if (coarse & (word{1} << b)) {
                const auto old = block<L>::decode(expected[b], row);
                const auto next = commands[b].logical - x.base;
                contribution += wide(next) - wide(old);
                block<L>::encode(expected[b], row, next);
                independent.add(x.independent_bytes(row));
            } else x.storage.protect(PROT_NONE);
        }
        const auto p = take(fc::declare_packed(std::span(blocks), std::span<const fc::replacement>(commands), coarse));
        need(p.access.stores.contains(independent) && independent.contains(p.access.stores), "independent bit-wire store footprint");
        ikea::journal effects{records};
        const auto actual = take(fc::apply_packed(p, fc::full_grant(p.access), effects));
        need(actual == contribution, "exact FOR logical contribution");
        fc::ranges issued;
        for (const auto& e : effects.entries()) {
            bool found = false;
            for (auto& b : blocks) if (e.source == &*b.view) {
                const auto bytes = b.view->plane_storage(e.plane);
                need(e.offset <= bytes.size() && e.size <= bytes.size() - e.offset, "effect resolves live source");
                issued.add(bytes.data() + e.offset, e.size); found = true;
            }
            need(found, "no expired or foreign source");
        }
        need(p.access.stores.contains(issued) && issued.contains(p.access.stores), "predeclared versus issued stores");
        for (unsigned b = 0; b < blocks.size(); ++b) {
            auto& x = blocks[b]; x.storage.protect(PROT_READ | PROT_WRITE); x.canary();
            need(std::equal(expected[b].begin(), expected[b].end(), x.storage.data), "independent exact bytes and preserved neighbours");
            for (unsigned i = 0; i < rows; ++i)
                need(sp::get_unchecked(*x.view, i) == block<L>::decode(expected[b], i), "independent all-point oracle");
        }
        ++packed_cases;
    }

    for (auto& b : blocks) b.reset();
    for (unsigned b = 0; b < 4; ++b) commands[b] = {7, blocks[b].base + 99};
    for (auto& b : blocks) b.storage.protect(PROT_NONE);
    const auto p = take(fc::declare_packed(std::span(blocks), std::span<const fc::replacement>(commands), 15));
    for (auto& b : blocks) b.storage.protect(PROT_READ | PROT_WRITE);
    for (auto reason : {fc::refusal::reads, fc::refusal::stores, fc::refusal::serialization, fc::refusal::effects}) {
        auto g = fc::full_grant(p.access);
        if (reason == fc::refusal::reads) g.readable = {};
        if (reason == fc::refusal::stores) g.writable = {};
        if (reason == fc::refusal::serialization) g.serialized = {};
        for (auto& b : blocks) b.storage.protect(PROT_NONE);
        ikea::journal effects{std::span(records).first(reason == fc::refusal::effects ? 0 : records.size())};
        const auto result = fc::apply_packed(p, g, effects);
        need(!result && result.error() == reason && !effects.used, "prior grant/capacity refusal before payload access");
        for (auto& b : blocks) b.storage.protect(PROT_READ | PROT_WRITE);
        ++refusals;
    }
    commands[3].logical = blocks[3].base + blocks[3].maximum + 1;
    for (auto& b : blocks) b.storage.protect(PROT_NONE);
    need(!fc::declare_packed(std::span(blocks), std::span<const fc::replacement>(commands), 15), "late invalid input refuses whole declaration");
    commands[3] = {rows, blocks[3].base};
    need(!fc::declare_packed(std::span(blocks), std::span<const fc::replacement>(commands), 15), "late invalid coordinate refuses");
    for (auto& b : blocks) b.storage.protect(PROT_READ | PROT_WRITE);
    refusals += 2;
    mapping unreadable(sizeof(commands));
    auto* opaque_commands = reinterpret_cast<fc::replacement*>(unreadable.data);
    for (unsigned i = 0; i < 4; ++i) std::construct_at(opaque_commands + i);
    unreadable.protect(PROT_NONE);
    const auto empty = take(fc::declare_packed(std::span(blocks),
        std::span<const fc::replacement>(opaque_commands, 4), 0));
    need(empty.changes.empty() && !empty.access.reads.bytes(), "coarse none before command acquisition");

    // Two logical rows can share preserving-store bytes. Their byte-level
    // serialization grants must conflict even though their value bits differ.
    const auto a = fc::point_envelope(*blocks[0].view, 0), b = fc::point_envelope(*blocks[0].view,
        L::storage == sp::geometry::local ? 1 : 32);
    fc::ranges both = a.stores; both.add(b.stores);
    need(both.bytes() < a.stores.bytes() + b.stores.bytes(), "disjoint logical rows share physical stores");
    report(L::storage == sp::geometry::local ? "Local13_point" : "Striped12_point", a);
}

struct delta_block {
    block<sp::layout<13>> residual;
    mapping checkpoints{15 * 8};
    bool indexed;
    static constexpr word anchor = 10000, rbase = 11;
    std::array<word, rows> values{};
    explicit delta_block(bool cp) : indexed(cp) { reset(); }
    std::span<const std::byte> plane_storage(unsigned plane) const {
        return plane == 1 ? std::span<const std::byte>{checkpoints.data, checkpoints.bytes} : std::span<const std::byte>{};
    }
    word load_cp(unsigned mark) const {
        word v; std::memcpy(&v, checkpoints.data + mark * 8, 8);
        if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
        return v;
    }
    void store_cp(unsigned mark, word v) {
        if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
        std::memcpy(checkpoints.data + mark * 8, &v, 8);
    }
    void reset() {
        residual.reset(); checkpoints.protect(PROT_READ | PROT_WRITE);
        std::memset(checkpoints.data - 64, 0xa5, checkpoints.bytes + 64);
        word prefix = 0; values[0] = anchor;
        for (unsigned i = 0; i < rows; ++i) {
            const word s = i ? 40 + i % 7 : 0;
            block<sp::layout<13>>::encode(residual.storage.span(), i, s);
            prefix += s;
            values[i] = anchor + i * rbase + prefix;
            if (i % 16 == 15 && i + 1 < rows) store_cp(i / 16, prefix);
        }
    }
};
struct delta_plan {
    delta_block* target = nullptr;
    unsigned row = 0, start = 1;
    int previous_cp = -1, changed_cp = -1;
    word value = 0;
    fc::envelope access;
};
std::expected<delta_plan, fc::refusal> declare_delta(delta_block& d, unsigned row, word value, bool selected) {
    delta_plan p;
    if (!selected) return p;
    if (!row) return std::unexpected(fc::refusal::kind); // anchor repair outside this witness
    if (row >= rows) return std::unexpected(fc::refusal::range);
    p.target = &d; p.row = row; p.value = value;
    if (d.indexed && row >= 16) {
        p.start = row / 16 * 16; p.previous_cp = int(row / 16) - 1;
        p.access.reads.add(d.checkpoints.data + p.previous_cp * 8, 8);
    }
    for (unsigned i = p.start; i <= row; ++i) p.access.reads.add(fc::point_reads(*d.residual.view, i));
    p.access.add(fc::point_envelope(*d.residual.view, row));
    if (row + 1 < rows) p.access.add(fc::point_envelope(*d.residual.view, row + 1));
    if (d.indexed && (row + 1) % 16 == 0 && row + 1 < rows) {
        p.changed_cp = int(row / 16);
        p.access.stores.add(d.checkpoints.data + p.changed_cp * 8, 8); ++p.access.records;
    }
    return p;
}
struct delta_result { wide logical; word physical; };
[[gnu::noinline]] std::expected<delta_result, fc::refusal>
apply_delta(const delta_plan& p, const fc::grant& permission, ikea::journal& effects) {
    if (effects.used > effects.storage.size()) return std::unexpected(fc::refusal::effects);
    const auto accepted = fc::admit(p.access, permission, effects.remaining());
    if (!accepted) return std::unexpected(accepted.error());
    if (!p.target) return delta_result{};
    auto& d = *p.target;
    word prefix = p.previous_cp < 0 ? 0 : d.load_cp(unsigned(p.previous_cp));
    word current = 0;
    for (unsigned i = p.start; i <= p.row; ++i) {
        current = sp::get_unchecked(*d.residual.view, i); prefix += current;
    }
    const wide old = wide(d.anchor) + wide(p.row) * d.rbase + prefix;
    const wide delta = wide(p.value) - old;
    const wide first = wide(current) + delta;
    const wide second = p.row + 1 < rows ? wide(sp::get_unchecked(*d.residual.view, p.row + 1)) - delta : 0;
    const wide cp = wide(prefix) + delta;
    if (first < 0 || first > d.residual.maximum || second < 0 || second > d.residual.maximum ||
        (p.changed_cp >= 0 && (cp < 0 || cp > wide(UINT64_MAX))))
        return std::unexpected(fc::refusal::value);
    // Both residuals, optional checkpoint and all capacities were admitted
    // before the first store; local per-set checks are now redundant safeguards.
    const auto writer = take(sp::bind_writer(*d.residual.view));
    sp::author::sum_change stored;
    need(bool(writer.set(p.row, word(first), effects, stored)), "first admitted residual");
    if (p.row + 1 < rows)
        need(bool(writer.set(p.row + 1, word(second), effects, stored)), "second admitted residual");
    if (p.changed_cp >= 0) {
        effects.before(d, 1, unsigned(p.changed_cp) * 8, 8);
        d.store_cp(unsigned(p.changed_cp), word(cp));
    }
    return delta_result{delta, stored.finish()};
}

void delta() {
    for (bool indexed : {false, true}) {
        delta_block d(indexed);
        d.residual.storage.protect(PROT_NONE); d.checkpoints.protect(PROT_NONE);
        const auto anchor = declare_delta(d, 0, 0, true), outside = declare_delta(d, rows, 0, true);
        need(!anchor && anchor.error() == fc::refusal::kind, "fixed-basis anchor refuses before payload");
        need(!outside && outside.error() == fc::refusal::range, "delta coordinate refuses before payload");
        refusals += 2;
        for (unsigned row = 1; row < rows; ++row) for (int increment : {-1, 1}) {
            d.reset(); const auto before = d.values;
            if (!indexed) d.checkpoints.protect(PROT_NONE);
            const auto next = word(wide(before[row]) + increment);
            const auto p = take(declare_delta(d, row, next, true));
            std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
            const auto result = take(apply_delta(p, fc::full_grant(p.access), effects));
            need(result.logical == increment, "delta exact logical contribution");
            need(result.physical == (row + 1 < rows ? 0 : word(increment)), "stored delta is not logical SUM delta");
            word prefix = 0; wide total_before = 0, total_after = 0;
            fc::ranges actual;
            for (auto e : effects.entries()) {
                if (e.source == &*d.residual.view) {
                    need(e.plane == 0, "residual effect plane");
                    const auto s = d.residual.view->plane_storage(0);
                    need(e.offset <= s.size() && e.size <= s.size() - e.offset, "residual effect bounds");
                    actual.add(s.data() + e.offset, e.size);
                } else {
                    need(e.source == &d && e.plane == 1, "checkpoint source/plane");
                    const auto s = d.plane_storage(1);
                    need(e.offset <= s.size() && e.size <= s.size() - e.offset, "checkpoint bounds");
                    actual.add(s.data() + e.offset, e.size);
                }
            }
            need(p.access.stores.contains(actual) && actual.contains(p.access.stores), "delta stores versus declaration");
            for (unsigned i = 0; i < rows; ++i) {
                const auto s = block<sp::layout<13>>::decode(d.residual.storage.span(), i);
                // Independent logical inverse, using the bit-wire decoder.
                prefix += s;
                const auto value = d.anchor + i * d.rbase + prefix;
                need(value == (i == row ? next : before[i]), "adjacent repair all logical rows");
                total_before += before[i]; total_after += value;
                if (indexed && i % 16 == 15 && i + 1 < rows) {
                    word cp = 0;
                    for (unsigned b = 0; b < 8; ++b)
                        cp |= word(std::to_integer<unsigned>(d.checkpoints.data[i / 16 * 8 + b])) << (8 * b);
                    need(cp == prefix, "independent checkpoint bytes");
                }
            }
            need(total_after - total_before == increment, "wide aggregate oracle");
            d.residual.canary(); ++delta_cases;
        }
        d.reset(); const auto p = take(declare_delta(d, 31, UINT64_MAX, true));
        report(indexed ? "delta_row31_checkpoints16" : "delta_row31_no_checkpoints", p.access);
        const std::vector<std::byte> before(d.residual.storage.data, d.residual.storage.data + d.residual.storage.bytes);
        const std::vector<std::byte> cp(d.checkpoints.data, d.checkpoints.data + d.checkpoints.bytes);
        std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
        const auto nofit = apply_delta(p, fc::full_grant(p.access), effects);
        need(!nofit && nofit.error() == fc::refusal::value && !effects.used, "delta nonfit before either residual store");
        need(std::equal(before.begin(), before.end(), d.residual.storage.data) &&
             std::equal(cp.begin(), cp.end(), d.checkpoints.data), "delta refusal preserves every plane");
        ++refusals;
        const auto small = take(declare_delta(d, 31, d.values[31] + 1, true));
        auto grant = fc::full_grant(small.access);
        // Actual future stores alone omit prefix/checkpoint dependencies.
        grant.readable = small.access.stores;
        d.residual.storage.protect(PROT_NONE); d.checkpoints.protect(PROT_NONE);
        const auto missing = apply_delta(small, grant, effects);
        need(!missing && missing.error() == fc::refusal::reads && !effects.used,
             "store-only read permission refuses before delta dependency acquisition");
        const auto none = take(declare_delta(d, 31, 0, false));
        need(bool(apply_delta(none, {}, effects)), "coarse skip protects delta payload/checkpoints");
        d.residual.storage.protect(PROT_READ | PROT_WRITE); d.checkpoints.protect(PROT_READ | PROT_WRITE);
    }
}

template <class F> void fault(const char* label, F&& f) {
    const auto child = fork(); need(child >= 0, "fork");
    if (!child) {
        const rlimit limit{0, 0}; setrlimit(RLIMIT_CORE, &limit);
        signal(SIGSEGV, SIG_DFL); signal(SIGBUS, SIG_DFL);
        f(); _exit(0);
    }
    int status = 0; need(waitpid(child, &status, 0) == child, "waitpid");
    if (!(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS))) {
        std::fprintf(stderr, "guard %s: child status %d\n", label, status);
        need(false, "insufficient grant must fault");
    }
    ++guard_controls;
}

void tuplepack() {
    const auto law = take(tp::layout::make(64,
        std::array<tp::code, 3>{{{0, 0, 4}, {0, 4, 4}, {63, 0, 8}}}));
    const auto plan = take(tp::write_plan<64>::make(law, std::array<std::uint8_t, 1>{0}));
    need(plan.read_bytes() == UINT64_MAX && plan.write_bytes() == 1, "wide old-data read versus one issued byte");
    mapping memory(64);
    // A view may describe accessible extent metadata without proving actual
    // page readiness. Only its first byte is readable in the negative control.
    auto* first = memory.pages + memory.page - 1;
    const auto destination = take(tp::view::attach(law, std::span(first, 64), 1, 64));
    tp::packet<64> input{}; input[0] = 7;
    fc::envelope access; access.reads.add(first, 64); access.stores.add(first, 1); access.records = plan.store_runs();
    report("TuplePack_native_partial", access);
    auto insufficient = fc::full_grant(access); insufficient.readable = access.stores;
    need(!fc::admit(access, insufficient, 8), "prior TP read refusal");
    // Explicit supported native author body. The ordinary buffered sparse
    // setter can choose a narrower scalar route; read_bytes is a possible
    // envelope, not a claim that every caller executes the wider load.
    auto execute = [&](ikea::journal& e) {
        e.before(destination, 0, 0, 1);
        tp::native::write_body(plan.controls(), ikea::to_u8(first), tp::native::load_packet(input.data()));
    };
    fault("native TuplePack old read", [&] { std::array<ikea::journal::entry, 8> r; ikea::journal e{r}; execute(e); });
    need(mprotect(memory.pages + memory.page, memory.page, PROT_READ | PROT_WRITE) == 0, "full read authority");
    std::memset(first, 0xa0, 64);
    std::array<ikea::journal::entry, 8> records; ikea::journal effects{records};
    need(bool(fc::admit(access, fc::full_grant(access), records.size())), "TP full admission");
    execute(effects);
    need(first[0] == std::byte{0xa7}, "partial field preserves sibling nibble");
    for (unsigned i = 1; i < 64; ++i) need(first[i] == std::byte{0xa0}, "TP untouched bytes");
    const auto issued = resolve(effects, destination);
    need(issued.bytes() == 1 && access.reads.bytes() == 64, "effects cannot stand in for prior reads");
}

struct marked_effects {
    std::atomic<unsigned>* calls;
    template <class Source> void before(const Source&, unsigned, std::size_t, std::size_t) noexcept {
        calls->fetch_add(1, std::memory_order_relaxed);
    }
};
struct observed_summary {
    static constexpr bool needs_before = true;
    std::atomic<unsigned>* observation;
    void observe_scalar(word before, word after) {
        // An actual observable use; a dead sum_change accumulator would permit
        // the compiler to remove the very old-data read this control tests.
        observation->store(unsigned(before + after) + 1000, std::memory_order_relaxed);
    }
};
void callback_order() {
    block<sp::layout<8>> b;
    auto* shared = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    need(shared != MAP_FAILED, "shared marker");
    auto* calls = std::construct_at(static_cast<std::atomic<unsigned>*>(shared), 0u);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    b.storage.protect(PROT_NONE);
    fault("summary before callback", [&] {
        observed_summary summary{calls}; marked_effects e{calls};
        sp::author::replace_point_unchecked(*b.view, 0, 99, summary, e);
    });
    need(calls->load() == 0, "old-summary read precedes first effect callback");
    b.storage.protect(PROT_READ);
    fault("store after callback", [&] {
        sp::author::no_summary summary; marked_effects e{calls};
        sp::author::replace_point_unchecked(*b.view, 0, 99, summary, e);
    });
    need(calls->load() != 0, "positive marker control: callback before store");
    b.storage.protect(PROT_READ | PROT_WRITE); munmap(shared, 4096);
}

void insufficient_local_canary() {
    block<sp::layout<13>> b;
    const std::vector<std::byte> before(b.storage.data, b.storage.data + b.storage.bytes);
    // The tempting two-byte native-row grant misses the Local residual plane.
    fc::ranges naive; naive.add(b.storage.data, 2);
    const auto access = fc::point_envelope(*b.view, 0);
    need(!naive.contains(access.stores), "naive logical-row bytes insufficient");
    std::array<ikea::journal::entry, 8> records; ikea::journal effects{records};
    const auto writer = take(sp::bind_writer(*b.view));
    need(bool(writer.set(0, word{0}, effects)), "deliberate unprotected negative control");
    unsigned outside = 0;
    for (unsigned i = 2; i < before.size(); ++i) outside += before[i] != b.storage.data[i];
    need(outside == 5, "independent canaries expose five modified bytes outside naive grant");
    ++guard_controls;
}
} // namespace
int main() {
    packed<sp::layout<13>>();
    packed<sp::layout<12, sp::geometry::striped>>();
    delta(); tuplepack(); callback_order(); insufficient_local_canary();
    std::printf("{\"packed_coarse_cases\":%u,\"delta_repairs\":%u,\"refusals\":%u,\"independent_negative_controls\":%u,\"pass\":true}\n",
                packed_cases, delta_cases, refusals, guard_controls);
}
