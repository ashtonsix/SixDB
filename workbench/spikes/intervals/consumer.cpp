#include "consumer_fixture.h"

namespace {
using namespace intervals_probe;
volatile u64 consumed;
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
        if constexpr(G==16 || G==64) {
            const char *before=Relative?(G==16?"before_ends16":"before_ends64"):
                                        (G==16?"before_sizes16":"before_sizes64");
            add_arm(arms,before,owner->readable_bytes(),0,backing,sizeof(Image),
                    sizeof(typename Image::L),sizeof(P),owner->before_source(),f);
        }
        if constexpr(G==64 && !Relative)
            add_arm(arms,"batch_sizes64",owner->readable_bytes(),0,backing,sizeof(Image),
                    sizeof(typename Image::L),sizeof(P),owner->source().for_independent_reads(),f);
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

    compact_image sizes(f, ikea::streampack::detail::compact::geometry::sizes), ends(f, ikea::streampack::detail::compact::geometry::relative_ends);
    auto add_compact = [&](const char *name, const compact_image &owner) {
        ikea::streampack::detail::compact::author::dispatch(*owner.view, [&](auto access) {
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
