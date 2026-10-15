// Native admission of layout_emit.py's current solver output. This is a field
// transfer diagnostic, not a table benchmark or a new layout implementation.
// Build with both vectorizers disabled; wide native bodies use explicit SIMD.
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/tuplepack/author/native.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

namespace tp = ikea::tuplepack;
using byte = tp::byte;
using word = std::uint64_t;
static std::string context;
void require(bool value, const char* what) {
    if (!value) {
        std::fprintf(stderr, "layout_check: %s: %s\n", context.c_str(), what);
        std::abort();
    }
}
word mask(unsigned bits) { return bits == 64 ? ~word{} : (word{1} << bits) - 1; }
template<class T> T number(std::istream& in) {
    T value{}; in >> value; require(bool(in), "numeric bridge input"); return value;
}
std::string text(std::istream& in) {
    std::string value; in >> std::quoted(value); require(bool(in), "text bridge input"); return value;
}
unsigned count(std::istream& in, unsigned limit) {
    auto n = number<unsigned>(in); require(n <= limit, "bridge count bound"); return n;
}
struct Field { std::string name; int slot; unsigned start, bits; };
struct Projection {
    std::string field;
    unsigned packet;
    std::vector<byte> map;
    std::vector<unsigned> logical;
};
struct Description {
    unsigned bytes;
    std::vector<tp::code> codes;
    std::vector<Projection> projections;
};
struct Binding { std::string field; int slot; };
struct Unit { unsigned offset, description; std::vector<Binding> bindings; };
struct Case {
    std::string name, identity;
    unsigned bytes;
    std::vector<Field> fields;
    std::vector<Description> descriptions;
    std::vector<Unit> units;
};
struct Input { std::string solver_sha; std::vector<Case> cases; };
Input read_input(const char* path) {
    std::ifstream in(path); require(bool(in), "open emitted cases");
    require(text(in) == "HTLAYOUT1", "bridge version");
    Input out; out.solver_sha = text(in);
    require(out.solver_sha.size() == 64, "solver source SHA256");
    const auto cases = count(in, 4096);
    for (unsigned ci = 0; ci < cases; ++ci) {
        Case c; c.name = text(in); c.identity = text(in); c.bytes = count(in, 1 << 20);
        const auto nf = count(in, 4096), nd = count(in, 4096), nu = count(in, 16384);
        for (unsigned i = 0; i < nf; ++i)
            c.fields.push_back({text(in), number<int>(in), number<unsigned>(in), number<unsigned>(in)});
        for (unsigned i = 0; i < nd; ++i) {
            Description d; d.bytes = count(in, 64);
            const auto nc = count(in, 128), np = count(in, 4096);
            for (unsigned j = 0; j < nc; ++j) {
                const auto offset = count(in, 63), shift = count(in, 7), width = count(in, 8);
                d.codes.push_back({byte(offset), byte(shift), byte(width)});
            }
            for (unsigned j = 0; j < np; ++j) {
                Projection p; p.field = text(in); p.packet = count(in, 64);
                const auto nm = count(in, 64);
                for (unsigned k = 0; k < nm; ++k) p.map.push_back(byte(count(in, 127)));
                for (unsigned k = 0; k < nm; ++k) p.logical.push_back(number<unsigned>(in));
                d.projections.push_back(std::move(p));
            }
            c.descriptions.push_back(std::move(d));
        }
        for (unsigned i = 0; i < nu; ++i) {
            Unit u; u.offset = number<unsigned>(in); u.description = number<unsigned>(in);
            const auto nb = count(in, 4096);
            for (unsigned j = 0; j < nb; ++j) u.bindings.push_back({text(in), number<int>(in)});
            c.units.push_back(std::move(u));
        }
        out.cases.push_back(std::move(c));
    }
    in >> std::ws; require(in.eof(), "no trailing bridge data"); return out;
}

word random_word(word& state) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state;
}
std::vector<byte> random_bytes(unsigned n, word& state) {
    std::vector<byte> out(n); for (auto& b : out) b = byte(random_word(state)); return out;
}
bool bit(std::span<const byte> bytes, unsigned position) {
    require(position / 8 < bytes.size(), "oracle read bound");
    return (bytes[position / 8] >> (position % 8)) & 1;
}
void set_bit(std::span<byte> bytes, unsigned position, bool value) {
    require(position / 8 < bytes.size(), "oracle write bound");
    const byte m = byte(1u << (position % 8));
    bytes[position / 8] = byte((bytes[position / 8] & ~m) | (value ? m : 0));
}
std::vector<byte> field_value(std::span<const byte> wire, const Field& field) {
    std::vector<byte> out((field.bits + 7) / 8);
    for (unsigned i = 0; i < field.bits; ++i) set_bit(out, i, bit(wire, field.start + i));
    return out;
}
template<unsigned N> byte packet_byte(const tp::packet<N>& p, unsigned i) {
    if constexpr (N == 8) return byte(p >> (i * 8));
    else return p[i];
}
template<unsigned N> void packet_set(tp::packet<N>& p, unsigned i, byte value) {
    if constexpr (N == 8) p |= word(value) << (i * 8);
    else p[i] = value;
}
template<unsigned N> word checksum(const tp::packet<N>& p) {
    if constexpr (N == 8) return p;
    else {
        // Consume the complete decoded packet, equally for both controls.
        word out = 0;
        for (unsigned i = 0; i < 64; i += 8) {
            word v; std::memcpy(&v, p.data() + i, 8); out ^= std::rotl(v, int(i));
        }
        return out;
    }
}
template<unsigned N> tp::packet<N> native_read(const tp::reader<N>& p, const byte* source) {
    if constexpr (N == 8) return tp::native::read_body(p.controls(), source);
    else {
        tp::packet<64> out;
        tp::native::store_packet(out.data(), tp::native::read_body(p.controls(), source));
        return out;
    }
}
template<unsigned N> void native_write(const tp::writer<N>& p, byte* destination,
                                       const tp::packet<N>& input) {
    if constexpr (N == 8) tp::native::write_body(p.controls(), destination, input);
    else tp::native::write_body(p.controls(), destination, tp::native::load_packet(input.data()));
}

// Raw controls derive from emitted codes, not native controls. The scalar
// projection fallback is identified in output; it is not a competitive SIMD
// baseline. Contiguous byte maps use native raw unit loads or bounded words.
struct Direct {
    const Description* description;
    const Projection* projection;
    word reads{}, output_mask{};
    unsigned offset{}, bytes{}, shift{};
    bool transfer{}, contiguous{};
    Direct(const Description& d, const Projection& p) : description(&d), projection(&p) {
        require(!p.map.empty(), "nonempty projection");
        const auto first = d.codes.at(p.map.front());
        offset = first.offset; shift = first.shift; bytes = unsigned(p.map.size());
        transfer = bytes <= 8; contiguous = true;
        for (unsigned i = 0; i < p.map.size(); ++i) {
            const auto code = d.codes.at(p.map[i]); reads |= word{1} << code.offset;
            transfer &= code.offset == offset + i && code.shift == shift;
            contiguous &= code.offset == offset + i && code.shift == 0 && code.width == 8;
            if (i < 8) output_mask |= mask(code.width) << (8 * i);
        }
    }
    const char* kind(unsigned n) const {
        return n == 8 && transfer ? "bounded_word" : contiguous ? "raw_native_unit" : "scalar_projection_control";
    }
};
word bounded_load(const byte* p, unsigned bytes) {
    word value = 0;
    switch (bytes) {
#define LOAD(N) case N: std::memcpy(&value, p, N); break
    LOAD(1); LOAD(2); LOAD(3); LOAD(4); LOAD(5); LOAD(6); LOAD(7); LOAD(8);
#undef LOAD
    default: require(false, "bounded load width");
    }
    return value;
}
void bounded_store(byte* p, unsigned bytes, word value) {
    switch (bytes) {
#define STORE(N) case N: std::memcpy(p, &value, N); break
    STORE(1); STORE(2); STORE(3); STORE(4); STORE(5); STORE(6); STORE(7); STORE(8);
#undef STORE
    default: require(false, "bounded store width");
    }
}
template<unsigned N> tp::packet<N> direct_read(const Direct& d, const byte* source) {
    if constexpr (N == 8) {
        if (d.transfer) return (bounded_load(source + d.offset, d.bytes) >> d.shift) & d.output_mask;
    } else if (d.contiguous) {
        tp::packet<64> out;
        tp::native::store_packet(out.data(), tp::native::load_unit(source + d.offset, d.bytes));
        return out;
    }
    tp::packet<N> out{};
    for (unsigned i = 0; i < d.projection->map.size(); ++i) {
        const auto code = d.description->codes[d.projection->map[i]];
        packet_set<N>(out, i, byte((source[code.offset] >> code.shift) & mask(code.width)));
    }
    return out;
}
template<unsigned N> void direct_write(const Direct& d, byte* destination, const tp::packet<N>& input) {
    if constexpr (N == 8) if (d.transfer) {
        const auto changed = d.output_mask << d.shift;
        const auto old = changed == mask(d.bytes * 8) ? 0 : bounded_load(destination + d.offset, d.bytes);
        bounded_store(destination + d.offset, d.bytes, (old & ~changed) | (input << d.shift));
        return;
    }
    for (unsigned i = 0; i < d.projection->map.size(); ++i) {
        const auto code = d.description->codes[d.projection->map[i]];
        const auto changed = byte(mask(code.width) << code.shift);
        destination[code.offset] = byte((destination[code.offset] & ~changed) |
                                        (packet_byte<N>(input, i) << code.shift));
    }
}

template<unsigned N> struct Prepared {
    tp::reader<N> read;
    tp::writer<N> write;
    Direct direct;
    Prepared(const tp::layout& layout, const Description& d, const Projection& p)
        : read([&] { auto r = tp::reader<N>::make(layout, p.map);
              require(bool(r), "TuplePack reader preparation"); return *r; }()),
          write([&] { auto w = tp::writer<N>::make(layout, p.map);
              require(bool(w), "TuplePack writer preparation"); return *w; }()), direct(d, p) {
        require((read.read_bytes() & ~mask(d.bytes)) == 0 &&
                (write.read_bytes() & ~mask(d.bytes)) == 0 &&
                (write.write_bytes() & ~mask(d.bytes)) == 0, "native windows within emitted unit");
        require((direct.reads & ~read.read_bytes()) == 0, "read window covers selected physical bytes");
        require(write.write_bytes() == direct.reads, "write window matches selected bytes");
    }
};
using Native = std::variant<Prepared<8>, Prepared<64>>;
struct PreparedDescription { tp::layout layout; std::vector<Native> projections; };
std::vector<PreparedDescription> prepare(const Case& c) {
    std::vector<PreparedDescription> result;
    for (const auto& d : c.descriptions) {
        auto f = tp::layout::make(d.bytes, d.codes); require(bool(f), "TuplePack emitted layout admission");
        PreparedDescription p{*f, {}};
        for (const auto& projection : d.projections) {
            if (projection.packet == 8) p.projections.emplace_back(std::in_place_type<Prepared<8>>, *f, d, projection);
            else { require(projection.packet == 64, "packet width");
                p.projections.emplace_back(std::in_place_type<Prepared<64>>, *f, d, projection); }
        }
        result.push_back(std::move(p));
    }
    return result;
}

struct Guard {
    std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    byte* allocation = static_cast<byte*>(mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    Guard() { require(allocation != MAP_FAILED && mprotect(allocation + page, page, PROT_READ | PROT_WRITE) == 0, "guard mapping"); }
    ~Guard() { require(munmap(allocation, page * 3) == 0, "guard release"); }
    std::span<byte> memory() { return {allocation + page, page}; }
    byte* place(unsigned bytes, bool right) { return allocation + page + (right ? page - bytes : 0); }
};
struct Stats { std::size_t cases{}, descriptions{}, units{}, projections{}, transfers{}, gathers{}, wide{}, reads{}, writes{}; };

template<unsigned N> void check_projection(const Prepared<N>& native, const tp::layout& format,
        const Case& c, const Unit& unit, const Field& field, const Projection& projection,
        std::span<const byte> wire, std::span<const byte> replacement, std::vector<byte>& reconstructed,
        std::vector<byte>& covered, Guard& guard, Stats& stats) {
    tp::packet<N> wanted{}, input{};
    const auto original = field_value(wire, field);
    const auto& codes = c.descriptions[unit.description].codes;
    for (unsigned i = 0; i < projection.map.size(); ++i) {
        const unsigned width = codes[projection.map[i]].width, logical = projection.logical[i];
        require(logical + width <= field.bits, "reconstruction field bound");
        byte before = 0, after = 0;
        for (unsigned j = 0; j < width; ++j) {
            before |= byte(bit(original, logical + j)) << j;
            after |= byte(bit(replacement, logical + j)) << j;
        }
        packet_set<N>(wanted, i, before); packet_set<N>(input, i, after);
    }
    require(native.write.accepts(input), "writer admits independently generated replacement");
    for (bool right : {false, true}) {
        auto page = guard.memory(); std::fill(page.begin(), page.end(), byte{0xa6});
        byte* data = guard.place(format.bytes(), right);
        require(unit.offset + format.bytes() <= wire.size(), "emitted unit in bucket");
        std::memcpy(data, wire.data() + unit.offset, format.bytes());
        const std::vector<byte> before(page.begin(), page.end());
        auto view = tp::view::bind(format, {data, format.bytes()}, 1, format.bytes());
        require(bool(view), "physical view admission");
        auto operation = tp::bind_reader(native.read, *view); require(bool(operation), "reader binding");
        auto checked = operation->get(0); require(bool(checked) && *checked == wanted, "checked decoded packet oracle");
        require(!operation->get(1), "past-unit window rejected");
        const auto actual = native_read<N>(native.read, data);
        require(actual == wanted && direct_read<N>(native.direct, data) == wanted, "native and direct packet oracle");
        require(native.read.get_unchecked(nullptr, 0, 0) == tp::packet<N>{}, "inactive read issues no access");
        native.write.set_unchecked(nullptr, 0, input, 0);
        if (!right) for (unsigned i = 0; i < projection.map.size(); ++i) {
            const unsigned width = codes[projection.map[i]].width, logical = projection.logical[i];
            for (unsigned j = 0; j < width; ++j) {
                require(!covered[logical + j]++, "each logical bit reconstructed once");
                set_bit(reconstructed, logical + j, (packet_byte<N>(actual, i) >> j) & 1);
            }
        }
        std::vector<byte> expected = before;
        const auto start = unsigned(data - page.data());
        // Write oracle uses original physical field coordinates, not the
        // TuplePack code offsets that the native and direct writers consume.
        for (unsigned i = 0; i < projection.map.size(); ++i) {
            const unsigned width = codes[projection.map[i]].width, logical = projection.logical[i];
            for (unsigned j = 0; j < width; ++j) {
                const auto physical = field.start + logical + j;
                require(physical >= unit.offset * 8 && physical < (unit.offset + format.bytes()) * 8, "write oracle unit bound");
                set_bit(expected, start * 8 + physical - unit.offset * 8, bit(replacement, logical + j));
            }
        }
        for (unsigned arm = 0; arm < 3; ++arm) {
            std::copy(before.begin(), before.end(), page.begin());
            if (arm == 0) native_write<N>(native.write, data, input);
            else if (arm == 1) native.write.set_unchecked(data, input);
            else direct_write<N>(native.direct, data, input);
            require(std::equal(page.begin(), page.end(), expected.begin()), "native/compiled/direct write preserves all other bits and guard neighbors");
            require(native_read<N>(native.read, data) == input, "replacement readback");
            ++stats.writes;
        }
        ++stats.reads;
    }
    ++stats.projections;
    if constexpr (N == 64) ++stats.wide;
    else if (native.read.controls().transfer.bytes) ++stats.transfers;
    else ++stats.gathers;
}

void check_case(const Case& c, const std::vector<PreparedDescription>& prepared, Stats& stats) {
    context = c.name; word state = 0x91d73a18ea572ULL;
    const auto wire = random_bytes(c.bytes, state);
    std::vector<std::vector<byte>> reconstructed, coverage, replacement;
    std::map<std::pair<std::string, int>, unsigned> fields;
    for (unsigned i = 0; i < c.fields.size(); ++i) {
        const auto& f = c.fields[i];
        require(f.bits && f.start + f.bits <= c.bytes * 8, "physical field extent");
        require(fields.emplace(std::pair{f.name, f.slot}, i).second, "unique field identity");
        reconstructed.emplace_back((f.bits + 7) / 8); coverage.emplace_back(f.bits);
        replacement.push_back(random_bytes((f.bits + 7) / 8, state));
    }
    Guard guard;
    for (const auto& unit : c.units) {
        require(unit.description < prepared.size(), "shared description index");
        const auto& d = prepared[unit.description];
        const auto& description = c.descriptions[unit.description];
        require(unit.bindings.size() == d.projections.size(), "projection bindings");
        for (unsigned pi = 0; pi < unit.bindings.size(); ++pi) {
            const auto& binding = unit.bindings[pi]; const auto& projection = description.projections[pi];
            require(binding.field == projection.field, "shared projection field identity");
            const auto found = fields.find({binding.field, binding.slot}); require(found != fields.end(), "physical field oracle exists");
            const auto fi = found->second;
            context = c.name + "/" + binding.field + "/" + std::to_string(binding.slot);
            std::visit([&](const auto& native) { check_projection(native, d.layout, c, unit, c.fields[fi], projection,
                wire, replacement[fi], reconstructed[fi], coverage[fi], guard, stats); }, d.projections[pi]);
        }
        ++stats.units;
    }
    for (unsigned i = 0; i < c.fields.size(); ++i) {
        require(reconstructed[i] == field_value(wire, c.fields[i]), "whole-field native reconstruction");
        require(std::ranges::all_of(coverage[i], [](byte n) { return n == 1; }), "complete logical field coverage");
    }
    ++stats.cases; stats.descriptions += prepared.size();
}

template<unsigned N> void benchmark_projection(const Case& c, unsigned di, const Description& description,
        unsigned pi, const Prepared<N>& p, std::size_t iterations) {
    alignas(64) std::array<byte, 64> storage{}; word state = 0x45721397dULL;
    for (auto& b : storage) b = byte(random_word(state));
    const auto original = storage;
    auto first = direct_read<N>(p.direct, storage.data());
    tp::packet<N> second{};
    for (unsigned i = 0; i < description.projections[pi].map.size(); ++i) {
        const auto code = description.codes[description.projections[pi].map[i]];
        packet_set<N>(second, i, byte(mask(code.width) ^ packet_byte<N>(first, i)));
    }
    require(p.write.accepts(first) && p.write.accepts(second), "benchmark inputs admitted");
    for (unsigned rep = 0; rep < 4; ++rep) for (unsigned operation = 0; operation < 2; ++operation) {
        word expected_sum = 0;
        for (unsigned order = 0; order < 2; ++order) {
            const unsigned arm = rep % 2 ? 1 - order : order;
            auto run = [&](std::size_t n) {
              word sum = 0;
              for (std::size_t i = 0; i < n; ++i) {
                // Only payload can change. A global memory clobber would also
                // prevent legitimate hoisting of immutable prepared controls.
                asm volatile("" : "+m"(storage));
                if (operation == 0) {
                    auto packet = arm ? native_read<N>(p.read, storage.data()) : direct_read<N>(p.direct, storage.data());
                    sum += checksum<N>(packet);
                } else {
                    const auto& input = i & 1 ? first : second;
                    if (arm) native_write<N>(p.write, storage.data(), input);
                    else direct_write<N>(p.direct, storage.data(), input);
                    sum += checksum<N>(input);
                }
              }
              asm volatile("" : "+r"(sum) : "m"(storage));
              return sum;
            };
            storage = original; const auto warm = run(std::min<std::size_t>(iterations, 1024));
            asm volatile("" : : "r"(warm));
            storage = original;
            const auto start = std::chrono::steady_clock::now();
            const auto sum = run(iterations);
            const auto end = std::chrono::steady_clock::now();
            if (!order) expected_sum = sum; else require(sum == expected_sum, "diagnostic consumer checksums match");
            if (operation) require(native_read<N>(p.read, storage.data()) == (iterations & 1 ? second : first), "diagnostic final write");
            const auto ns = std::chrono::duration<double, std::nano>(end - start).count() / double(iterations);
            const char* direct_kind = operation && N == 64 ? "scalar_projection_control" : p.direct.kind(N);
            std::printf("%s,%u,%u,%u,%s,%u,%u,%s,%s,%s,%zu,%.6f,%llu,%016llx,%016llx,%016llx,%016llx\n",
                c.name.c_str(), di, pi, description.bytes, description.projections[pi].field.c_str(), N, rep,
                operation ? "write_packet" : "read_packet", arm ? "tuplepack_native" : direct_kind,
                p.direct.kind(N), iterations, ns, static_cast<unsigned long long>(sum),
                static_cast<unsigned long long>(p.direct.reads), static_cast<unsigned long long>(p.read.read_bytes()),
                static_cast<unsigned long long>(p.write.read_bytes()), static_cast<unsigned long long>(p.write.write_bytes()));
        }
    }
}

int main(int argc, char** argv) {
    require(argc >= 3 && argc <= 5, "usage: layout_check check CASES | bench CASES ITERATIONS [CASE_SUBSTRING]");
    const std::string_view mode = argv[1]; require(mode == "check" || mode == "bench", "mode");
    require(mode != "bench" || argc >= 4, "benchmark iteration argument");
    const auto input = read_input(argv[2]);
    std::fprintf(stderr, "solver_sha256=%s\ncases=%zu\n", input.solver_sha.c_str(), input.cases.size());
    const auto iterations = mode == "bench" ? std::stoull(argv[3]) : 0;
    require(mode != "bench" || (argc >= 4 && iterations), "positive diagnostic iterations");
    const std::string_view filter = argc == 5 ? argv[4] : "";
    Stats stats;
    if (mode == "bench") std::puts("case,description,projection,unit_bytes,field,packet_bytes,rep,operation,implementation,read_control,iterations,ns_per_packet,checksum,direct_read_bytes,tp_read_bytes,tp_write_reads,tp_write_bytes");
    for (const auto& c : input.cases) {
        if (mode == "bench" && c.name.find(filter) == std::string::npos) continue;
        context = c.name; const auto prepared = prepare(c); check_case(c, prepared, stats);
        if (mode == "bench") for (unsigned di = 0; di < prepared.size(); ++di)
            for (unsigned pi = 0; pi < prepared[di].projections.size(); ++pi)
                std::visit([&](const auto& p) { benchmark_projection(c, di, c.descriptions[di], pi, p, iterations); }, prepared[di].projections[pi]);
    }
    require(stats.cases != 0, "at least one case selected");
    std::fprintf(stderr, "checked_cases=%zu shared_descriptions=%zu unit_instances=%zu projections=%zu gpr_transfer=%zu gpr_gather=%zu simd_packet=%zu guarded_reads=%zu guarded_writes=%zu\n",
        stats.cases, stats.descriptions, stats.units, stats.projections, stats.transfers, stats.gathers, stats.wide, stats.reads, stats.writes);
}
