// Complete hot geometry consumers. Local exploratory timings, not a fleet result.
#include <ikea/intervals.h>
#include <ikea/boundlist.h>
#include <ikea/boundlist/author/read.h>
#include <ikea/effects.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace bl = ikea::intervals;
using u64 = std::uint64_t;
volatile u64 consumed;
constexpr unsigned capacity = 256, query_count = 4096;

void need(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

struct fixture {
    std::array<unsigned, capacity> widths{}, ends{};
    std::array<u64, capacity> children{};
    std::vector<std::uint8_t> payload;
    std::array<unsigned, query_count> ranks{}, points{}, short_starts{};
    unsigned count, maximum, extent{};
    fixture(unsigned n, unsigned m) : count(n), maximum(m) {
        std::mt19937 random(713);
        for (unsigned i = 0; i < n; ++i) {
            widths[i] = random() % (m + 1);
            ends[i] = extent += widths[i];
            children[i] = random();
        }
        need(extent != 0, "empty payload fixture");
        payload.resize(extent);
        for (auto &v : payload) v = random();
        for (unsigned q = 0; q < query_count; ++q) {
            ranks[q] = random() % n;
            points[q] = random() % extent;
            short_starts[q] = random() % (n - 7);
        }
    }
    u64 use_interval(unsigned rank, unsigned first, unsigned limit) const {
        // An actual payload byte, plus its logical length. Empty intervals
        // contribute zero and never read a neighbour's payload.
        return limit - first + (first != limit ? payload[first] : 0u) + rank;
    }
    u64 expected(unsigned operation) const {
        u64 sum = 0;
        const unsigned queries = operation == 3 ? 16 : query_count;
        for (unsigned q = 0; q < queries; ++q) {
            if (operation == 1) {
                unsigned rank = 0;
                while (ends[rank] <= points[q]) ++rank;
                sum += children[rank];
                continue;
            }
            const unsigned first = operation == 0 ? ranks[q] :
                                   operation == 2 ? short_starts[q] : 0;
            const unsigned take = operation == 0 ? 1 : operation == 2 ? 8 : count;
            for (unsigned i = first; i < first + take; ++i)
                sum += use_interval(i, i ? ends[i - 1] : 0, ends[i]);
        }
        return sum;
    }
};

template <unsigned G, class W, class P, unsigned Maximum, bool Relative> struct sparse_image {
    using L = std::conditional_t<!Relative, W,
              std::conditional_t<(u64(G) * Maximum <= 255), std::uint8_t,
              std::conditional_t<(u64(G) * Maximum <= 65535), std::uint16_t, std::uint32_t>>>;
    std::array<L, capacity> fields{};
    std::array<P, capacity / G> checkpoints{};
    unsigned count;
    P extent;
    explicit sparse_image(const fixture &f) : count(f.count), extent(f.extent) {
        unsigned local = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (i % G == 0) {
                checkpoints[i / G] = i ? f.ends[i - 1] : 0;
                local = 0;
            }
            local += f.widths[i];
            fields[i] = Relative ? local : f.widths[i];
        }
    }
    auto source() const {
        if constexpr (Relative)
            return ikea::intervals::sparse_ends<G, L, P, capacity / G>{
                fields.data(), checkpoints.data(), count, 0, extent};
        else
            return ikea::intervals::sparse_sizes<G, W, P, capacity / G>{
                fields.data(), checkpoints.data(), count, 0, extent, Maximum};
    }
    std::size_t readable_bytes() const {
        return (count + 15) / 16 * 16 * sizeof(L) + (count + G - 1) / G * sizeof(P);
    }
};

struct compact_image {
    ikea::boundlist::layout layout;
    std::vector<std::byte> directory, lengths, control;
    std::optional<ikea::boundlist::view> view;
    compact_image(const fixture &f, ikea::boundlist::geometry family)
        : layout(*ikea::boundlist::layout::prepare({.family = family,
                                      .maximum = static_cast<std::uint16_t>(f.maximum),
                                      .extent_limit = capacity * f.maximum,
                                      .immortal = true})),
          directory(layout.directory_bytes()), lengths(layout.lengths_bytes(f.count)),
          control(layout.control_bytes()) {
        auto bound = ikea::boundlist::view::bind(layout, {directory, lengths, control});
        need(bound.has_value(), "compact admission failed");
        view = *bound;
        std::array<ikea::owner_write, 16> records;
        {
            ikea::source_write_journal effects{records};
            need(ikea::boundlist::initialize(*view, effects).has_value(), "compact initialization failed");
        }
        for (unsigned i = 0; i < f.count; ++i) {
            ikea::source_write_journal effects{records};
            need(ikea::boundlist::append(*view, f.widths[i], effects).has_value(), "compact append failed");
        }
        need(view->validate().has_value(), "compact image invalid");
    }
    std::size_t bytes() const { return directory.size() + lengths.size() + control.size(); }
};

// Separate operation instantiations leave no operation branch in query loops.
template <unsigned Operation, class Source>
u64 run(const Source &source, const fixture &f) {
    std::array<u64, 4> sums{};
    const unsigned queries = Operation == 3 ? 16 : query_count;
    for (unsigned q = 0; q < queries; ++q) {
        u64 value = 0;
        if constexpr (Operation == 0) {
            const unsigned rank = f.ranks[q];
            const auto bounds = bl::locate(source, rank);
            if (!bounds) std::abort();
            value = f.use_interval(rank, bounds->first, bounds->limit);
        } else if constexpr (Operation == 1) {
            const unsigned rank = bl::reverse(source, f.points[q]);
            if (rank >= f.count) std::abort();
            value = f.children[rank];
        } else {
            const unsigned first = Operation == 2 ? f.short_starts[q] : 0;
            const unsigned take = Operation == 2 ? 8 : f.count;
            const auto answer = bl::range(source, first, take, [&](unsigned rank, auto interval) {
                value += f.use_interval(rank, interval.first, interval.limit);
            });
            if (!answer) std::abort();
        }
        sums[q % 4] += value;
    }
    return sums[0] + sums[1] + sums[2] + sums[3];
}

struct settings { unsigned trials = 11, repeat = 32, maximum = 255, count = 256; };
struct arm {
    const char *name;
    std::size_t data_bytes, control_bytes, capacity_bytes, owner_bytes, binding_bytes;
    std::size_t field_bytes, checkpoint_bytes;
    std::function<u64(unsigned)> query; // Only once per timed batch, never per query.
};

template <class Source> void add_arm(std::vector<arm> &arms, const char *name,
                                     std::size_t bytes, std::size_t control,
                                     std::size_t capacity_bytes, std::size_t resident,
                                     unsigned field, unsigned checkpoint,
                                     Source source, const fixture &f) {
    arms.push_back({name, bytes, control, capacity_bytes, resident, sizeof(Source),
                   field, checkpoint, [source, &f](unsigned op) {
        switch (op) {
        case 0: return run<0>(source, f);
        case 1: return run<1>(source, f);
        case 2: return run<2>(source, f);
        case 3: return run<3>(source, f);
        }
        std::abort();
    }});
}

template <unsigned Maximum> void experiment(const settings &options) {
    using W = std::conditional_t<(Maximum <= 255), std::uint8_t, std::uint16_t>;
    using P = std::conditional_t<(capacity * Maximum <= 65535), std::uint16_t, std::uint32_t>;
    fixture f(options.count, Maximum);
    std::array<P, capacity> direct{};
    for (unsigned i = 0; i < f.count; ++i) direct[i] = f.ends[i];
    std::vector<arm> arms;
    std::vector<std::shared_ptr<void>> owners;
    auto add_sparse = [&]<unsigned G, bool Relative>(const char *name) {
        using Image = sparse_image<G, W, P, Maximum, Relative>;
        auto owner = std::make_shared<Image>(f);
        const auto backing = sizeof(owner->fields) + sizeof(owner->checkpoints);
        add_arm(arms, name, owner->readable_bytes(), 0, backing,
                sizeof(Image), sizeof(typename Image::L), sizeof(P), owner->source(), f);
        owners.push_back(owner);
    };
    add_arm(arms, "direct", f.count * sizeof(P), 0, sizeof(direct), sizeof(direct),
            sizeof(P), 0, bl::from_ends(bl::direct_source<P>{direct.data(), f.count}, P{0}), f);
    add_sparse.template operator()<16, false>("sizes16");
    add_sparse.template operator()<32, false>("sizes32");
    add_sparse.template operator()<64, false>("sizes64");
    add_sparse.template operator()<128, false>("sizes128");
    add_sparse.template operator()<256, false>("sizes256");
    add_sparse.template operator()<16, true>("ends16");
    add_sparse.template operator()<64, true>("ends64");
    add_sparse.template operator()<256, true>("ends256");

    compact_image sizes(f, ikea::boundlist::geometry::sizes), ends(f, ikea::boundlist::geometry::relative_ends);
    auto add_compact = [&](const char *name, const compact_image &owner) {
        ikea::boundlist::author::dispatch(*owner.view, [&](auto access) {
            add_arm(arms, name, owner.directory.size() + owner.lengths.size(), owner.control.size(),
                    owner.directory.size() + owner.lengths.size(), owner.bytes() + sizeof(owner),
                    owner.layout.width_bytes(), owner.layout.offset_bytes(), access, f);
        });
    };
    add_compact("maintained_sizes16", sizes);
    add_compact("maintained_ends16", ends);

    const std::array names{"rank_payload", "inverse_child", "range8_payload", "range_all_payload"};
    for (unsigned op = 0; op < names.size(); ++op) {
        const u64 expected = f.expected(op);
        for (const auto &a : arms)
            need(a.query(op) == expected, "complete consumer differs from scalar oracle");
        for (unsigned trial = 0; trial < options.trials; ++trial) {
            for (unsigned position = 0; position < arms.size(); ++position) {
                const auto &a = arms[(position + trial) % arms.size()];
                const auto begin = std::chrono::steady_clock::now();
                for (unsigned r = 0; r < options.repeat; ++r) {
                    asm volatile("" ::: "memory");
                    consumed = a.query(op);
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - begin).count();
                const u64 operations = u64(options.repeat) * (op == 3 ? 16 : query_count);
                const unsigned per_operation = op == 3 ? f.count : op == 2 ? 8 : 1;
                std::printf("{\"arm\":\"%s\",\"operation\":\"%s\",\"trial\":%u,"
                    "\"maximum\":%u,\"count\":%u,\"data_bytes\":%zu,\"control_bytes\":%zu,"
                    "\"capacity_bytes\":%zu,\"owner_bytes\":%zu,\"binding_bytes\":%zu,"
                    "\"field_bytes\":%zu,\"checkpoint_bytes\":%zu,\"payload_bytes\":%zu,"
                    "\"child_bytes\":%zu,\"query_bytes\":%zu,\"elapsed_ns\":%lld,"
                    "\"operations\":%llu,\"intervals_per_operation\":%u,\"checksum\":\"%llu\"}\n",
                    a.name, names[op], trial, Maximum, f.count, a.data_bytes, a.control_bytes,
                    a.capacity_bytes, a.owner_bytes, a.binding_bytes,
                    a.field_bytes, a.checkpoint_bytes, f.payload.size(), sizeof(f.children),
                    sizeof(f.ranks) + sizeof(f.points) + sizeof(f.short_starts),
                    static_cast<long long>(elapsed), static_cast<unsigned long long>(operations),
                    per_operation, static_cast<unsigned long long>(expected));
            }
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        settings options;
        for (int i = 1; i < argc; i += 2) {
            need(i + 1 < argc, "expected --option value");
            const std::string option = argv[i];
            char *end;
            const unsigned long value = std::strtoul(argv[i + 1], &end, 10);
            need(!*end && value <= 65535, "invalid numeric option");
            if (option == "--trials") options.trials = value;
            else if (option == "--repeat") options.repeat = value;
            else if (option == "--maximum") options.maximum = value;
            else if (option == "--count") options.count = value;
            else throw std::runtime_error("unknown option");
        }
        need(options.trials && options.repeat && options.count >= 8 && options.count <= capacity,
             "invalid work count");
        switch (options.maximum) {
        case 15: experiment<15>(options); break;
        case 255: experiment<255>(options); break;
        case 4095: experiment<4095>(options); break;
        default: throw std::runtime_error("maximum must be 15, 255 or 4095");
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
