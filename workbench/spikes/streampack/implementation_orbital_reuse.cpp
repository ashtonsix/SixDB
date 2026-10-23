#include "implementation_orbital.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <source_location>
#include <string_view>
#include <thread>
#include <vector>

namespace om = streampack_implementation::orbital_material;
namespace sp = ikea::streampack;
namespace oa = orbital::allocation;

namespace {
using Clock = std::chrono::steady_clock;
using Rows = std::vector<std::vector<std::byte>>;
constexpr orbital::ObjectId object{0x53505255, 1}, other{0x53505255, 2}, imported{0x53505255, 3};

void require(bool ok, const char* reason, std::source_location at = std::source_location::current()) {
    if (ok) return;
    std::fprintf(stderr, "%s:%u: %s\n", at.file_name(), at.line(), reason);
    std::abort();
}

template<class R> auto take(R&& result, const char* reason) {
    require(bool(result), reason);
    return std::move(*result);
}

std::uint64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

struct Memory final : std::pmr::memory_resource {
    std::size_t live{};
    bool fail_next{};
    ~Memory() { require(!live, "catalog/interpretation metadata leaked"); }
    void* do_allocate(std::size_t size, std::size_t alignment) override {
        if (std::exchange(fail_next, false)) throw std::bad_alloc{};
        auto* value = std::pmr::new_delete_resource()->allocate(size, alignment);
        live += size;
        return value;
    }
    void do_deallocate(void* value, std::size_t size, std::size_t alignment) override {
        require(size <= live, "metadata refund underflow");
        live -= size;
        std::pmr::new_delete_resource()->deallocate(value, size, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Runtime {
    loom::Worker worker;
    orbital::Ref<oa::Arena> arena;
    explicit Runtime(int cpu = -1) {
        require(bool(loom::adopt(worker, cpu)), "adopt owner Worker");
        arena = take(oa::open_arena(512ull << 20), "open byte budget");
        require(bool(loom::configure(worker, arena, 0)), "configure owner Pool");
    }
    ~Runtime() {
        const auto end = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto state = loom::shutdown(worker);
            if (state.kind() == orbital::Progress::Kind::done) break;
            require(state.kind() != orbital::Progress::Kind::failed && Clock::now() < end, "physical settlement");
            loom::poll(worker, {256, 0});
            std::this_thread::yield();
        }
        require(arena->charged_bytes.load() == 0, "residual allocation charge");
        loom::relinquish(worker);
    }
};

struct ReleasePart {
    void operator()(orbital::Part<om::Control>* value) const noexcept { orbital::release(value); }
};
using Export = std::unique_ptr<orbital::Part<om::Control>, ReleasePart>;
enum class Policy { full_copy, selected, exported };
const char* name(Policy policy) {
    return policy == Policy::full_copy ? "full-copy" : policy == Policy::selected ? "retained-selection" : "retained-parts";
}
const char* name(sp::geometry geometry) { return geometry == sp::geometry::absolute_ends ? "direct" : "width"; }
const char* name(om::Placement placement) { return placement == om::Placement::grouped ? "grouped" : "separate"; }

Rows rows(unsigned count) {
    Rows result;
    for (unsigned row = 0; row < count; ++row) {
        auto& value = result.emplace_back(12 + row % 17);
        for (unsigned b = 0; b < value.size(); ++b) value[b] = std::byte((row * 31 + b * 17) % 251);
        const std::uint64_t identity = 0x1357900000000000ull + row;
        std::memcpy(value.data(), &identity, sizeof(identity));
    }
    return result;
}

std::vector<std::uint32_t> holders(unsigned count) {
    std::vector<std::uint32_t> result(sp::slot_capacity, om::absent);
    std::iota(result.begin(), result.begin() + count, 0u);
    return result;
}

om::Store::Owner initial(Runtime& runtime, om::Store& store, Memory& memory,
    const om::FormatOwner& format, const Rows& input, om::Placement placement) {
    const auto bytes = std::accumulate(input.begin(), input.end(), std::size_t{},
        [](std::size_t total, const auto& value) { return total + value.size() + 3; });
    auto draft = take(om::Draft::create(runtime.worker, format, memory, bytes, placement), "construct actual StreamPack");
    std::array<ikea::journal::entry, 16> records; ikea::journal effects{records};
    for (unsigned row = 0; row < input.size(); ++row) {
        effects.used = 0;
        require(bool(sp::append(draft.write(), input[row], input[row].size() + 3, effects)), "append application value");
        draft.holders()[row] = row;
    }
    return take(draft.capture(store), "capture initial five Parts");
}

void verify(om::Selection selected, const Rows& input, std::span<const std::uint32_t> expected) {
    require(bool(selected), "selected material absent");
    const auto view = om::read(selected);
    require(bool(view.validate()) && view.size() == input.size(), "actual StreamPack geometry/content");
    require(std::ranges::equal(om::holders(selected), expected), "logical holder identities");
    for (unsigned row = 0; row < input.size(); ++row) {
        const auto value = take(sp::value(view, row), "actual row lookup");
        require(std::ranges::equal(value, input[row]), "independent complete value oracle");
    }
}

// Only the holder map changes. Encoded planes stay immutable; no mutable view
// is manufactured over retained bytes. Both arms copy exact admitted extents.
struct Revision {
    om::Control control;
    std::array<oa::Allocation, 5> fresh;
    std::array<orbital::PartSource, 4> retained;
    std::array<Export, 4> exports;
    std::uint64_t copied_bytes{};
    unsigned fresh_parts{}, retained_parts{}, exported_parts{};
    bool captured{};

    Revision(Runtime& runtime, om::Store& source_store, const om::Store::Scope& scope,
        orbital::ObjectId id, Memory& memory, Policy policy, std::uint32_t replacement,
        orbital::PartRetention retention = orbital::PartRetention::shared_control) {
        const auto selected = scope.bind(id);
        require(bool(selected), "revision source discovery");
        control = selected.control();
        control.memory = &memory;
        control.placement = om::Placement::separate;
        std::optional<om::Store::BoundSelection> binding;
        if (policy == Policy::exported) binding.emplace(take(source_store.bound(scope, id), "bound export source"));
        for (unsigned p = 0; p < 5; ++p) {
            if (p == 4 || policy == Policy::full_copy) {
                fresh[p] = take(loom::allocate(runtime.worker, {control.extents[p], 0, 64}), "allocate fresh Part");
                if (control.extents[p]) std::memcpy(fresh[p].address, selected.part(p)->address(), control.extents[p]);
                copied_bytes += control.extents[p];
                ++fresh_parts;
            } else {
                if (policy == Policy::exported) {
                    exports[p].reset(take(binding->export_part(p, retention), "escape certified old Part"));
                    retained[p] = take(orbital::source(exports[p].get()), "borrow escaped Part certificate");
                    ++exported_parts;
                } else retained[p] = take(orbital::source(selected, p), "borrow protected selected certificate");
                ++retained_parts;
            }
        }
        std::memcpy(fresh[4].address, &replacement, sizeof(replacement));
    }

    orbital::Result<om::Store::Owner> capture(om::Store& store) {
        require(!captured, "revision captured twice");
        std::array<orbital::PartSource, 5> sources;
        for (unsigned p = 0; p < 5; ++p) {
            if (fresh[p]) sources[p] = {control.id(p), {fresh[p].address, control.extents[p]}, {}, &fresh[p]};
            else { sources[p] = retained[p]; sources[p].id = control.id(p); }
        }
        auto result = store.capture(control, sources);
        if (result) {
            captured = true;
            for (auto& part : exports) part.reset();
        }
        return result;
    }
};

void history(sp::geometry geometry, om::Placement placement, Policy policy, orbital::PartRetention retention) {
    Runtime runtime;
    Memory original_memory, destination_memory;
    om::Store destination(destination_memory);
    const auto input = rows(33);
    const auto unchanged = holders(input.size());
    auto expected = unchanged;
    {
        om::Store store(original_memory);
        auto format = take(om::prepare_format({.family = geometry, .maximum = 255, .byte_limit = 256 * 255}), "prepare format");
        auto owner = initial(runtime, store, original_memory, format, input, placement);
        {
            om::Store::Scope basis(store);
            const std::array<om::Store::Change, 2> changes{{{object, owner}, {other, owner}}};
            om::Store::Prepared packet(store);
            require(bool(store.prepare(basis, changes, packet)) && bool(store.install(packet)), "initial paired publication");
        }
        owner = {};
        std::optional<om::Store::Scope> old;
        old.emplace(store);
        const auto original_bound = take(store.bound(*old, object), "original binding");
        Export escaped(take(original_bound.export_part(0, retention), "original escaped bytes"));
        const auto original_binding = orbital::binding(escaped.get());
        const auto original_address = old->bind(object).part(0)->address();

        for (unsigned generation = 1; generation <= 8; ++generation) {
            const auto replacement = std::uint32_t(generation % input.size());
            expected[0] = replacement;
            {
                om::Store::Scope basis(store);
                Revision revision(runtime, store, basis, object, original_memory, policy, replacement, retention);
                if (generation == 1) {
                    const auto address = revision.fresh[4].address;
                    original_memory.fail_next = true;
                    const auto refused = revision.capture(store);
                    require(!refused && refused.error() == orbital::Error::capacity, "metadata refusal");
                    require(revision.fresh[4] && revision.fresh[4].address == address, "refusal consumed fresh Allocation");
                    verify(old->bind(object), input, unchanged);
                }
                auto replacement_owner = take(revision.capture(store), "capture fresh plus retained Parts");
                require(bool(om::publish(store, basis, object, replacement_owner)), "publish holder replacement");
            }
            store.drain();
            {
                om::Store::Scope current(store);
                const auto bound = take(store.bound(current, object), "new binding");
                verify(bound.selection(), input, expected);
                require(bound.binding().object == object && bound.binding().version > original_binding.version,
                    "logical publication changed object identity or failed to advance version");
                if (policy != Policy::full_copy)
                    require(bound.selection().part(0)->address() == original_address, "unchanged payload was copied");
            }
            verify(old->bind(object), input, unchanged);
            verify(old->bind(other), input, unchanged); // Late discovery at the old cut.
        }
        {
            const std::array<om::Store::Change, 1> changes{{{object, {}}}};
            om::Store::Prepared stale(store);
            const auto refused = store.prepare(*old, changes, stale);
            require(!refused && refused.error() == orbital::Error::stale, "stale publication accepted");
        }
        {
            om::Store::Scope current(store);
            const auto replacement = std::uint32_t(17);
            Revision revision(runtime, store, current, object, destination_memory, policy, replacement, retention);
            auto independent_format = take(om::prepare_format(format->layout.schema()), "independent interpretation");
            revision.control.format = independent_format.get();
            auto replacement_owner = take(revision.capture(destination), "capture into another Store/resource");
            om::Store::Scope basis(destination);
            require(bool(om::publish(destination, basis, imported, replacement_owner)), "publish imported identity");
            expected[0] = replacement;
            const auto foreign = destination.bound(*old, imported);
            require(!foreign && foreign.error() == orbital::Error::conflict, "foreign Scope authorized destination lookup");
        }
        store = {};
        format.reset();
        verify(old->bind(object), input, unchanged);
        require(orbital::binding(escaped.get()).object == original_binding.object &&
            orbital::binding(escaped.get()).version == original_binding.version, "source Part was relabelled");
        old.reset();
        escaped.reset();
    }
    require(original_memory.live == 0, "new owner retained predecessor catalog/proof resource");
    {
        om::Store::Scope current(destination);
        const auto bound = take(destination.bound(current, imported), "destination identity");
        require(bound.binding().object == imported, "destination inherited source object identity");
        verify(bound.selection(), input, expected);
    }
    destination = {};
    require(destination_memory.live == 0, "destination metadata did not settle");
}

struct Digest {
    std::uint64_t bytes{}, checksum{0xcbf29ce484222325ull};
    void consume(sp::bytes value, unsigned logical) {
        checksum = (checksum ^ logical) * 0x100000001b3ull;
        for (auto byte : value) checksum = (checksum ^ std::to_integer<unsigned>(byte)) * 0x100000001b3ull;
        bytes += value.size();
    }
    bool operator==(const Digest&) const = default;
};

struct Options {
    bool check{}, bench{};
    int cpu{-1};
    unsigned rows{193}, rounds{128}, queries{32768}, batch{256};
};

Options options(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--check") { result.check = true; continue; }
        if (arg == "--bench") { result.bench = true; continue; }
        if (arg == "--help") {
            std::cout << "--check | --bench [--cpu CPU] [--rows 193] [--rounds 128] [--queries 32768] [--batch 256]\n";
            std::exit(0);
        }
        require(i + 1 < argc, "missing argument");
        const std::string_view text(argv[++i]);
        unsigned value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        require(error == std::errc{} && end == text.data() + text.size(), "invalid unsigned argument");
        if (arg == "--cpu") { require(value <= 0x7fffffffu, "CPU out of range"); result.cpu = int(value); }
        else if (arg == "--rows") result.rows = value;
        else if (arg == "--rounds") result.rounds = value;
        else if (arg == "--queries") result.queries = value;
        else if (arg == "--batch") result.batch = value;
        else require(false, "unknown argument");
    }
    require(result.check != result.bench, "choose exactly one of --check or --bench");
    require(result.rows >= 2 && result.rows <= 256 && result.rounds && result.rounds <= 65536 &&
        result.queries && result.batch, "invalid workload extent");
    return result;
}

Digest reads(om::Store& store, const Options& options, const char* mode) {
    Digest result;
    const auto consume = [&](const sp::const_view& source, std::span<const std::uint32_t> map, unsigned query) {
        const auto logical = (query * 137u) % options.rows;
        result.consume(take(sp::value(source, map[logical]), "timed point lookup"), logical);
    };
    if (std::string_view(mode) == "read-native") {
        om::Store::Scope scope(store);
        const auto selected = scope.bind(object);
        const auto source = om::read(selected);
        const auto map = om::holders(selected);
        for (unsigned query = 0; query < options.queries; ++query) consume(source, map, query);
    } else {
        const auto batch = std::string_view(mode) == "read-point" ? 1u : options.batch;
        for (unsigned at = 0; at < options.queries;) {
            om::Store::Scope scope(store);
            const auto bound = take(store.bound(scope, object), "timed coherent binding");
            const auto source = om::read(bound.selection());
            const auto map = om::holders(bound.selection());
            const auto count = std::min(batch, options.queries - at);
            for (unsigned query = at; query < at + count; ++query) consume(source, map, query);
            at += count;
        }
    }
    return result;
}

void bench(sp::geometry geometry, om::Placement placement, Policy policy, const Options& options) {
    const auto input = rows(options.rows);
    auto expected = holders(options.rows);
    Memory memory;
    auto start = Clock::now();
    auto runtime = std::make_unique<Runtime>(options.cpu);
    auto format = take(om::prepare_format({.family = geometry, .maximum = 255, .byte_limit = 256 * 255}), "prepare format");
    auto store = std::make_unique<om::Store>(memory);
    {
        auto owner = initial(*runtime, *store, memory, format, input, placement);
        om::Store::Scope basis(*store);
        require(bool(om::publish(*store, basis, object, owner)), "initial benchmark publication");
    }
    auto old = std::make_unique<om::Store::Scope>(*store);
    const auto setup = elapsed(start);
    const auto print = [&](const char* event, unsigned round, std::uint64_t ns, std::uint64_t copied,
        unsigned fresh, unsigned retained, unsigned exports, const Digest& digest = {}) {
        const auto stats = oa::statistics(*runtime->worker.memory);
        std::cout << name(geometry) << ',' << name(placement) << ',' << name(policy) << ',' << event << ','
            << round << ',' << options.rows << ',' << ns << ',' << copied << ',' << fresh << ',' << retained << ','
            << exports << ',' << stats.mapped_bytes << ',' << stats.reserved_bytes << ',' << stats.metadata_bytes << ','
            << memory.live << ',' << digest.bytes << ',' << digest.checksum << '\n';
    };
    print("setup", 0, setup, 0, 0, 0, 0);
    const auto unchanged = expected;
    for (unsigned round = 0; round < options.rounds; ++round) {
        expected[0] = (round + 1) % options.rows;
        std::uint64_t copied{};
        unsigned fresh{}, retained{}, exports{};
        start = Clock::now();
        {
            om::Store::Scope basis(*store);
            Revision revision(*runtime, *store, basis, object, memory, policy, expected[0]);
            auto owner = take(revision.capture(*store), "timed physical capture");
            copied = revision.copied_bytes; fresh = revision.fresh_parts;
            retained = revision.retained_parts; exports = revision.exported_parts;
            require(bool(om::publish(*store, basis, object, owner)), "timed logical publication");
        }
        store->drain(128);
        loom::poll(runtime->worker, {32, 0});
        const auto total = elapsed(start);
        { om::Store::Scope current(*store); verify(current.bind(object), input, expected); }
        verify(old->bind(object), input, unchanged);
        print("publish", round, total, copied, fresh, retained, exports);
    }
    Digest oracle;
    for (unsigned query = 0; query < options.queries; ++query) {
        const auto logical = (query * 137u) % options.rows;
        oracle.consume(input[expected[logical]], logical);
    }
    for (const auto* mode : {"read-native", "read-batch", "read-point"}) {
        start = Clock::now();
        const auto digest = reads(*store, options, mode);
        const auto total = elapsed(start);
        require(digest == oracle, "independent logical read digest");
        print(mode, 0, total, 0, 0, 0, 0, digest);
    }
    start = Clock::now();
    store.reset(); old.reset(); format.reset();
    require(memory.live == 0, "benchmark metadata settlement");
    print("logical-teardown", 0, elapsed(start), 0, 0, 0, 0);
    start = Clock::now();
    runtime.reset();
    const auto settled = elapsed(start);
    std::cout << name(geometry) << ',' << name(placement) << ',' << name(policy)
        << ",physical-teardown,0," << options.rows << ',' << settled << ",0,0,0,0,,,," << memory.live << ",0,0\n";
}
} // namespace

int main(int argc, char** argv) {
    const auto args = options(argc, argv);
    if (args.check) {
        for (auto geometry : {sp::geometry::absolute_ends, sp::geometry::sizes})
            for (auto placement : {om::Placement::grouped, om::Placement::separate})
                for (auto policy : {Policy::full_copy, Policy::selected, Policy::exported})
                    for (auto retention : {orbital::PartRetention::shared_control, orbital::PartRetention::selected_copy})
                        history(geometry, placement, policy, retention);
        std::puts("PASS: actual StreamPack, retained Parts/fresh Allocations, identities, refusal, generations and resource independence");
    } else {
        std::cout << "geometry,source_placement,policy,event,round,rows,total_ns,copied_bytes,fresh_parts,retained_parts,exported_parts,pool_mapped,pool_reserved,pool_metadata,catalog_metadata,consumed_bytes,checksum\n";
        for (auto geometry : {sp::geometry::absolute_ends, sp::geometry::sizes})
            for (auto placement : {om::Placement::grouped, om::Placement::separate})
                for (auto policy : {Policy::full_copy, Policy::selected, Policy::exported}) bench(geometry, placement, policy, args);
    }
}
