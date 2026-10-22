// Complete grouped consumers over the SAME immutable experimental encoded images.
#include "input.h"
#include "material.h"
#include <ikea/hashtable.h>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>

namespace {
using namespace frame_execution;
namespace e = frame_encoding;
using Clock = std::chrono::steady_clock;
struct Total { Word count{}, sum{}; };
static_assert(sizeof(Total) == 16);
using Map = ikea::hashtable::table<64, 6, 16>;
struct Bits {
    std::array<Word, 4> word{};
    void set(unsigned p) { word[p / 64] |= Word{1} << (p % 64); }
    bool test(unsigned p) const { return word[p / 64] >> (p % 64) & 1; }
    unsigned count() const {
        unsigned n = 0;
        for (auto w : word) n += std::popcount(w);
        return n;
    }
    bool full() const { return count() == e::block_rows; }
};
Word low(unsigned n) { return n == 64 ? ~Word{} : (Word{1} << n) - 1; }
Bits interval(unsigned first, unsigned limit) {
    Bits out;
    for (unsigned w = 0; w < 4; ++w) {
        const auto a = std::max(first, w * 64), b = std::min(limit, (w + 1) * 64);
        if (a < b) out.word[w] = low(b - a) << (a - w * 64);
    }
    return out;
}
Bits intersect(Bits a, const Bits& b) {
    for (unsigned i = 0; i < 4; ++i) a.word[i] &= b.word[i];
    return a;
}
Bits without(Bits a, const Bits& b) {
    for (unsigned i = 0; i < 4; ++i) a.word[i] &= ~b.word[i];
    return a;
}
template<class F> void each(const Bits& bits, F&& f) {
    for (unsigned w = 0; w < 4; ++w)
        for (auto rest = bits.word[w]; rest; rest &= rest - 1)
            f(w * 64 + unsigned(std::countr_zero(rest)));
}
void need(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }

struct Image {
    e::column category, x;
    std::vector<Bits> live, has_x, has_category;
};
Image encode(const Part& part) {
    const auto n = (part.limit + 255) / 256 * 256;
    std::vector<Word> category(n), x(n);
    Image image;
    image.live.resize(n / 256);
    image.has_x.resize(n / 256);
    image.has_category.resize(n / 256);
    for (const auto& row : part.rows) {
        const auto b = row.position / 256, p = row.position % 256;
        image.live[b].set(p);
        if (row.has_x) { image.has_x[b].set(p); x[row.position] = row.x; }
        if (row.code >= 0) {
            image.has_category[b].set(p);
            category[row.position] = Word(row.code);
        }
    }
    image.category = e::encode(category);
    image.x = e::encode(x);
    need(image.x.rows == n && image.category.rows == n, "encoded tail was truncated");
    return image;
}

// Candidate block bits remain coarse. Misaligned experimental mask geometries
// need a local boundary mask only when a candidate physical block is visited.
Bits activity(const Image& image, const Query& q, std::size_t part, unsigned block) {
    auto out = intersect(image.live[block], image.has_x[block]);
    const auto& fine = q.selection[part];
    const auto& coarse = q.coarse[part];
    if (!fine.all) {
        Bits selected;
        each(out, [&](unsigned p) { if (fine.contains(block * 256 + p)) selected.set(p); });
        out = selected;
    }
    if (!coarse.all && coarse.grain % 256) {
        Bits selected;
        const unsigned origin = block * 256, limit = origin + 256;
        auto at = std::lower_bound(coarse.blocks.begin(), coarse.blocks.end(), origin / coarse.grain);
        for (; at != coarse.blocks.end() && *at * coarse.grain < limit; ++at) {
            const auto first = std::max(origin, *at * coarse.grain);
            const auto end = std::min(limit, (*at + 1) * coarse.grain);
            const auto piece = interval(first - origin, end - origin);
            for (unsigned i = 0; i < 4; ++i) selected.word[i] |= piece.word[i];
        }
        out = intersect(out, selected);
    }
    return out;
}
enum class Op : std::uint8_t { generic, constant_category, constant_value };
Op choose(const Image& image, unsigned block) {
    if (image.category.blocks[block].type == e::kind::constant) return Op::constant_category;
    if (image.x.blocks[block].type == e::kind::constant) return Op::constant_value;
    return Op::generic;
}
struct PartPlan {
    e::block_mask blocks;
    std::vector<std::uint8_t> allowed;
    std::vector<Op> operations;
    PartPlan(unsigned n, bool rich) : blocks(n), operations(rich ? n : 0) {}
};
std::vector<PartPlan> prepare(const Input& input, const std::vector<Image>& images,
                              const Query& q, bool rich) {
    std::vector<PartPlan> plans;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto n = unsigned(images[i].x.blocks.size());
        plans.emplace_back(n, rich);
        auto& plan = plans.back();
        if (!q.selection[i].any || q.threshold > UINT32_MAX ||
            (!q.all_categories && q.allowed.empty() && !q.include_null)) continue;
        const auto& coarse = q.coarse[i];
        if (coarse.all) for (unsigned b = 0; b < n; ++b) plan.blocks.set(b);
        else for (auto logical : coarse.blocks) {
            const auto first = logical * coarse.grain;
            const auto limit = std::min(input.parts[i].limit, (logical + 1) * coarse.grain);
            for (auto b = first / 256; b < (limit + 255) / 256; ++b) plan.blocks.set(b);
        }
        e::block_mask nonempty(n);
        e::for_each_block(plan.blocks, [&](unsigned b) {
            if (activity(images[i], q, i, b).count()) nonempty.set(b);
        });
        plan.blocks = std::move(nonempty);
        if (!plan.blocks.popcount()) continue;
        for (auto value : input.parts[i].dictionary) plan.allowed.push_back(q.accepts(value));
        if (rich) e::for_each_block(plan.blocks, [&](unsigned b) {
            if (activity(images[i], q, i, b).count()) plan.operations[b] = choose(images[i], b);
        });
    }
    return plans;
}
struct Work {
    Word blocks{}, generic{}, constant_category{}, constant_value{}, decoded_values{}, run_visits{},
         weighted{}, hash_updates{}, output_bytes{}, delegated_sums{};
};
// An operation on exposed reconstruction, not an independent copy of a codec.
Total reduce(const e::column& column, unsigned block, const Bits& mask, Word threshold, Work& work) {
    const auto n = mask.count();
    if (!n) return {};
    const auto& d = column.blocks[block];
    if (d.type == e::kind::constant) return d.base >= threshold ? Total{n, n * d.base} : Total{};
    if (!threshold && mask.full()) {
        ++work.delegated_sums;
        return {n, Word(e::block_sum(column, block))};
    }
    alignas(64) Word values[256];
    if (d.type == e::kind::runs) {
        column.decoders[block].read_unchecked(0, d.count, values);
        work.decoded_values += d.count;
        Total total;
        unsigned first = 0;
        e::each_run(column, block, [&](unsigned rank, unsigned size) {
            ++work.run_visits;
            const auto value = values[rank] + d.base;
            if (value >= threshold) {
                const auto count = intersect(mask, interval(first, first + size)).count();
                total.count += count; total.sum += count * value;
            }
            first += size;
        });
        return total;
    }
    if (d.type == e::kind::packed) {
        const auto rhs = threshold > d.base ? threshold - d.base : 0;
        if (d.width < 64 && rhs >> d.width) return {};
        column.decoders[block].read_unchecked(0, 256, values);
        work.decoded_values += 256;
        Total total;
        each(mask, [&](unsigned p) {
            if (values[p] >= rhs) { ++total.count; total.sum += values[p]; }
        });
        total.sum += total.count * d.base;
        return total;
    }
    if (d.type == e::kind::delta && !threshold) {
        column.decoders[block].read_unchecked(0, 256, values);
        work.decoded_values += 256;
        ++work.weighted;
        e::i64 step;
        std::memcpy(&step, column.payload.data() + d.offset + e::plane_bytes(d.width, 256), 8);
        e::i128 sum = e::i128(n) * d.base;
        Word suffix = 0;
        for (unsigned p = 255; p; --p) {
            suffix += mask.test(p);
            sum += e::i128(suffix) * (e::i128(step) + values[p]);
        }
        need(sum >= 0 && sum <= e::i128(UINT64_MAX), "selected delta sum outside admitted law");
        return {n, Word(sum)};
    }
    e::decode_block(column, block, values);
    work.decoded_values += 256;
    Total total;
    each(mask, [&](unsigned p) { if (values[p] >= threshold) { ++total.count; total.sum += values[p]; } });
    return total;
}

struct Group { bool present; Word key; Total total; };
struct Result { std::optional<Map> output; Total nulls; Work work; };
struct Scratch {
    std::vector<Total> totals;
    std::vector<unsigned> touched;
    explicit Scratch(std::size_t n) : totals(n) { touched.reserve(n); }
    void add(unsigned code, Total total) {
        if (!total.count) return;
        if (!totals[code].count) touched.push_back(code);
        totals[code].count += total.count; totals[code].sum += total.sum;
    }
};
Result execute(const Input& input, const std::vector<Image>& images, const Query& q,
               const std::vector<PartPlan>& plans, std::vector<Scratch>& scratch, unsigned program) {
    Result result;
    auto& output = result.output;
    auto& nulls = result.nulls;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];
        const auto& plan = plans[i];
        auto& local = scratch[i];
        e::for_each_block(plan.blocks, [&](unsigned b) {
            const auto mask = activity(image, q, i, b);
            if (!mask.count()) return;
            ++result.work.blocks;
            const auto ordinary = intersect(mask, image.has_category[b]);
            const auto null_mask = q.include_null ? without(mask, image.has_category[b]) : Bits{};
            const auto op = program == 2 ? Op::generic :
                            program == 1 ? plan.operations[b] : choose(image, b);
            if (op == Op::constant_category) {
                ++result.work.constant_category;
                const auto code = unsigned(image.category.blocks[b].base);
                if (ordinary.count() && plan.allowed[code])
                    local.add(code, reduce(image.x, b, ordinary, q.threshold, result.work));
                const auto add = reduce(image.x, b, null_mask, q.threshold, result.work);
                nulls.count += add.count; nulls.sum += add.sum;
                return;
            }
            alignas(64) Word codes[256], values[256];
            e::decode_block(image.category, b, codes);
            result.work.decoded_values += 256;
            if (op == Op::constant_value) {
                ++result.work.constant_value;
                const auto value = image.x.blocks[b].base;
                if (value < q.threshold) return;
                each(ordinary, [&](unsigned p) { if (plan.allowed[codes[p]]) local.add(unsigned(codes[p]), {1, value}); });
                const auto n = null_mask.count();
                nulls.count += n; nulls.sum += n * value;
                return;
            }
            ++result.work.generic;
            e::decode_block(image.x, b, values);
            result.work.decoded_values += 256;
            each(ordinary, [&](unsigned p) {
                if (values[p] >= q.threshold && plan.allowed[codes[p]]) local.add(unsigned(codes[p]), {1, values[p]});
            });
            each(null_mask, [&](unsigned p) { if (values[p] >= q.threshold) { ++nulls.count; nulls.sum += values[p]; } });
        });
        for (auto code : local.touched) {
            if (!output) output.emplace(713);
            const auto incoming = local.totals[code];
            output->modify_or_insert(input.parts[i].dictionary[code] >> q.shift, {}, [&](auto& bytes, bool) {
                Total total;
                std::memcpy(&total, bytes.data(), 16);
                total.count += incoming.count; total.sum += incoming.sum;
                std::memcpy(bytes.data(), &total, 16);
            });
            ++result.work.hash_updates;
            local.totals[code] = {};
        }
        local.touched.clear();
    }
    if (output) result.work.output_bytes = output->bytes_used();
    return result;
}
std::vector<Group> normalise(const Result& result) {
    std::vector<Group> groups;
    if (result.nulls.count) groups.push_back({false, 0, result.nulls});
    if (result.output) result.output->visit([&](Word key, const auto& bytes) {
        Total total;
        std::memcpy(&total, bytes.data(), 16);
        groups.push_back({true, key, total});
    });
    std::ranges::sort(groups, [](const Group& a, const Group& b) {
        return a.present != b.present ? a.present < b.present : a.key < b.key;
    });
    return groups;
}
Word digest(const Result& result) {
    Word value = 713 ^ result.nulls.count ^ (result.nulls.sum * 31337);
    if (result.output) result.output->visit([&](Word key, const auto& bytes) {
        Total total;
        std::memcpy(&total, bytes.data(), 16);
        value += (key * 0x9e3779b97f4a7c15ULL) ^ (total.count * 31337) ^ total.sum;
    });
    return value;
}
volatile Word consumed;
double ns(Clock::time_point first) {
    return std::chrono::duration<double, std::nano>(Clock::now() - first).count();
}
const char* names[] = {"opaque-complete", "rich-prepared", "decode-scratch"};

void print(const Query& q, unsigned program, const Result& result, double prepare_ns,
           Word plan_bytes, const std::vector<double>& times) {
    std::cout << "{\"type\":\"result\",\"program\":" << std::quoted(names[program])
              << ",\"query_id\":" << std::quoted(q.id) << ",\"groups\":[";
    bool first = true;
    for (const auto& g : normalise(result)) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"key\":";
        if (g.present) std::cout << g.key; else std::cout << "null";
        std::cout << ",\"count\":" << g.total.count << ",\"sum\":" << g.total.sum << '}';
    }
    std::cout << "],\"prepare_ns\":" << prepare_ns << ",\"plan_and_scratch_capacity_bytes\":"
              << plan_bytes << ",\"whole_query_warm_ns\":[";
    for (std::size_t i = 0; i < times.size(); ++i) std::cout << (i ? "," : "") << times[i];
    const auto& w = result.work;
    std::cout << "],\"work\":{\"blocks\":" << w.blocks << ",\"generic\":" << w.generic
              << ",\"constant_category\":" << w.constant_category << ",\"constant_value\":" << w.constant_value
              << ",\"decoded_values\":" << w.decoded_values << ",\"run_visits\":" << w.run_visits
              << ",\"delegated_block_sums\":" << w.delegated_sums
              << ",\"selected_delta_reductions\":" << w.weighted << ",\"hash_updates\":" << w.hash_updates
              << ",\"hash_owner_bytes\":" << w.output_bytes << "}}\n";
}
}

int main(int argc, char** argv) {
    try {
        const unsigned trials = argc == 2 ? unsigned(std::stoul(argv[1])) : 0;
        need(argc <= 2 && trials <= 1000, "usage: frame_execution_encoded [warm-trials<=1000]");
        auto input = read_input(std::cin);
        std::vector<Image> images;
        images.reserve(input.parts.size());
        Word bytes = 0, masks = 0, padded = 0, occupied = 0, backing = 0, readers = 0,
             controls = 0, dictionaries = 0;
        std::map<unsigned, Word> kinds;
        const auto start = Clock::now();
        for (auto& part : input.parts) {
            occupied += part.rows.size();
            images.push_back(encode(part));
            const auto& image = images.back();
            controls += sizeof(Image);
            dictionaries += part.dictionary.capacity() * sizeof(Word);
            padded += image.x.rows - part.limit;
            for (const auto* c : {&image.category, &image.x}) {
                bytes += c->descriptor_bytes() + c->payload.size() + c->run_geometry_bytes();
                backing += c->blocks.capacity() * sizeof(e::descriptor) + c->payload.capacity();
                readers += c->decoders.capacity() * sizeof(c->decoders[0]) +
                           c->d8.capacity() * sizeof(c->d8[0]) + c->d16.capacity() * sizeof(c->d16[0]) +
                           c->d32.capacity() * sizeof(c->d32[0]) + c->exceptions.capacity() * sizeof(c->exceptions[0]);
                controls += c->runs.capacity() * sizeof(c->runs[0]);
                for (const auto& run : c->runs) if (run) {
                    controls += sizeof(*run);
                    backing += run->directory.capacity() + run->lengths.capacity() + run->control.capacity();
                }
                for (const auto& d : c->blocks) ++kinds[unsigned(d.type)];
            }
            masks += image.live.size() * sizeof(Bits) * 3;
            part.rows.clear(); part.rows.shrink_to_fit();
            part.positions.clear(); part.positions.shrink_to_fit();
        }
        std::cout << "{\"type\":\"material\",\"encoded_logical_bytes\":" << bytes
                  << ",\"validity_bytes\":" << masks << ",\"padded_positions\":" << padded
                  << ",\"encoded_buffer_capacity_bytes\":" << backing
                  << ",\"prepared_decoder_capacity_bytes\":" << readers
                  << ",\"image_control_bytes\":" << controls
                  << ",\"dictionary_capacity_bytes\":" << dictionaries
                  << ",\"occupied_rows\":" << occupied << ",\"encode_ns\":" << ns(start)
                  << ",\"kind_blocks\":{";
        bool first_kind = true;
        for (const auto [kind, count] : kinds) {
            if (!first_kind) std::cout << ',';
            first_kind = false;
            std::cout << std::quoted(std::to_string(kind)) << ':' << count;
        }
        std::cout << "},\"checkpoint_option\":" << std::quoted(e::checkpoint_name(
                         images.empty() ? e::checkpoints::native : images.front().x.options.delta_checkpoints))
                  << "}\n";
        for (const auto& q : input.queries) {
            std::array<std::vector<PartPlan>, 3> plans;
            std::array<std::vector<Scratch>, 3> workspaces;
            std::array<double, 3> preparation{};
            std::array<Word, 3> plan_bytes{};
            for (unsigned p = 0; p < 3; ++p) {
                const auto begun = Clock::now();
                plans[p] = prepare(input, images, q, p == 1);
                for (std::size_t i = 0; i < input.parts.size(); ++i)
                    workspaces[p].emplace_back(plans[p][i].blocks.popcount() ? input.parts[i].dictionary.size() : 0);
                preparation[p] = ns(begun);
                plan_bytes[p] = plans[p].capacity() * sizeof(PartPlan) + workspaces[p].capacity() * sizeof(Scratch);
                for (const auto& plan : plans[p])
                    plan_bytes[p] += plan.blocks.words.capacity() * sizeof(Word) +
                                     plan.allowed.capacity() + plan.operations.capacity() * sizeof(Op);
                for (const auto& workspace : workspaces[p])
                    plan_bytes[p] += workspace.totals.capacity() * sizeof(Total) +
                                     workspace.touched.capacity() * sizeof(unsigned);
            }
            std::array<Result, 3> results;
            for (unsigned p = 0; p < 3; ++p)
                results[p] = execute(input, images, q, plans[p], workspaces[p], p);
            std::array<std::vector<double>, 3> times;
            for (unsigned t = 0; t < trials; ++t)
                for (unsigned arm = 0; arm < 3; ++arm) {
                    const auto p = (arm + t) % 3;
                    const auto begun = Clock::now();
                    auto result = execute(input, images, q, plans[p], workspaces[p], p);
                    consumed = digest(result);
                    result.output.reset(); // Charge retirement after consuming the actual hash result.
                    times[p].push_back(ns(begun));
                }
            for (unsigned p = 0; p < 3; ++p)
                print(q, p, results[p], preparation[p], plan_bytes[p], times[p]);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
