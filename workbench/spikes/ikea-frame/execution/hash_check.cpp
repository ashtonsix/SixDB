#include <ikea/hashtable.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// Untimed boundary controls over authored inputs, not Frame encoded kernels.
namespace {
using Word = std::uint64_t;
struct Total { Word count{}, sum{}; };
static_assert(sizeof(Total) == 16);
using Map = ikea::hashtable::table<64, 6, 16>;
struct Row { unsigned position{}; std::int64_t code{}; bool has_x{}; Word x{}; };
struct Part {
    std::string id, dictionary_edition;
    unsigned limit{};
    std::vector<Word> dictionary;
    std::vector<Row> rows;
    std::vector<unsigned> positions; // Prepared address directory, separate from row bodies.
};
struct Selection {
    bool all{};
    bool any{};
    std::vector<bool> positions;
    bool contains(unsigned p) const { return all || positions[p]; }
};
struct Coarse {
    unsigned grain{}, per_group{}, count{};
    bool all{};
    std::vector<unsigned> blocks;
    unsigned selected() const { return all ? count : unsigned(blocks.size()); }
    unsigned group_count() const { return (count + per_group - 1) / per_group; }
};
struct Query {
    std::string id;
    Word threshold{};
    bool all_categories{}, include_null{};
    unsigned shift{};
    std::unordered_set<Word> allowed;
    std::vector<Selection> selection;
    std::vector<Coarse> coarse;
    bool accepts(Word key) const { return all_categories || allowed.contains(key); }
};
struct Work {
    Word rows{}, dictionary_values{}, hash_updates{}, null_updates{}, counter_bytes_peak{};
    Word parts_skipped{}, coarse_selected{}, coarse_excluded{}, coarse_ranges{}, position_probes{};
    Word groups_skipped{}, groups_full{}, groups_mixed{};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void add(Total& to, Total from) {
    require(from.count <= std::numeric_limits<Word>::max() - to.count &&
            from.sum <= std::numeric_limits<Word>::max() - to.sum, "aggregate overflow");
    to.count += from.count;
    to.sum += from.sum;
}
void add(Map& output, Word key, Total incoming, Work& work) {
    output.modify_or_insert(key, {}, [&](auto& bytes, bool) {
        Total total;
        std::memcpy(&total, bytes.data(), sizeof(total));
        add(total, incoming);
        std::memcpy(bytes.data(), &total, sizeof(total));
    });
    ++work.hash_updates;
}

Part read_part() {
    Part part;
    std::string token;
    std::size_t dictionary_count, row_count;
    std::cin >> token >> std::quoted(part.id) >> std::quoted(part.dictionary_edition)
             >> part.limit >> dictionary_count >> row_count;
    require(bool(std::cin) && token == "PART" && !part.id.empty() &&
            !part.dictionary_edition.empty() && part.limit <= 65536 &&
            row_count <= part.limit, "invalid part header");
    part.dictionary.resize(dictionary_count);
    std::unordered_set<Word> keys;
    for (auto& key : part.dictionary) {
        std::cin >> key;
        require(bool(std::cin) && keys.insert(key).second, "invalid dictionary");
    }
    std::vector<bool> occupied(part.limit);
    for (std::size_t i = 0; i < row_count; ++i) {
        Row row;
        unsigned has_x;
        std::cin >> row.position >> row.code >> has_x >> row.x;
        require(bool(std::cin) && row.position < part.limit && !occupied[row.position] &&
                row.code >= -1 && (row.code == -1 || Word(row.code) < dictionary_count) &&
                has_x <= 1 && row.x <= std::numeric_limits<std::uint32_t>::max(),
                "invalid coded row");
        row.has_x = has_x;
        occupied[row.position] = true;
        part.rows.push_back(row);
    }
    std::ranges::sort(part.rows, {}, &Row::position);
    for (const auto& row : part.rows) part.positions.push_back(row.position);
    return part;
}

Query read_query(const std::vector<Part>& parts) {
    Query query;
    std::string token;
    unsigned all, nulls;
    std::size_t allowed;
    std::cin >> token >> std::quoted(query.id) >> query.threshold >> all >> nulls
             >> query.shift >> allowed;
    require(bool(std::cin) && token == "QUERY" && !query.id.empty() && all <= 1 &&
            nulls <= 1 && query.shift <= 63, "invalid query header");
    query.all_categories = all;
    query.include_null = nulls;
    for (std::size_t i = 0; i < allowed; ++i) {
        Word key;
        std::cin >> key;
        require(bool(std::cin), "invalid predicate value");
        query.allowed.insert(key);
    }
    for (const auto& part : parts) {
        std::int64_t count;
        std::cin >> count;
        require(bool(std::cin) && count >= -1 && count <= part.limit,
                "invalid selection count");
        Selection selected{count == -1, count == -1 ? part.limit != 0 : count != 0,
                           std::vector<bool>(count > 0 ? part.limit : 0)};
        for (std::int64_t i = 0; i < count; ++i) {
            unsigned position;
            std::cin >> position;
            require(bool(std::cin) && position < part.limit && !selected.positions[position],
                    "invalid selected position");
            selected.positions[position] = true;
        }
        query.selection.push_back(std::move(selected));
        Coarse coarse;
        std::cin >> token >> coarse.grain >> coarse.per_group >> count;
        require(bool(std::cin) && token == "COARSE" && coarse.grain &&
                coarse.grain <= 65536 && coarse.per_group && coarse.per_group <= 65536 &&
                count >= -1, "invalid coarse header");
        coarse.count = (part.limit + coarse.grain - 1) / coarse.grain;
        coarse.all = count == -1;
        require(count <= coarse.count, "invalid coarse selection count");
        for (std::int64_t i = 0; i < count; ++i) {
            unsigned block;
            std::cin >> block;
            require(bool(std::cin) && block < coarse.count, "invalid coarse block");
            coarse.blocks.push_back(block);
        }
        std::ranges::sort(coarse.blocks);
        require(std::adjacent_find(coarse.blocks.begin(), coarse.blocks.end()) ==
                coarse.blocks.end(), "duplicate coarse block");
        query.coarse.push_back(std::move(coarse));
    }
    return query;
}

std::size_t first_at(const Part& part, unsigned position, Work& work) {
    std::size_t low = 0, high = part.positions.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        ++work.position_probes;
        if (part.positions[middle] < position) low = middle + 1;
        else high = middle;
    }
    return low;
}

void execute(const std::vector<Part>& parts, const Query& query, bool by_code) {
    std::optional<Map> output; // Create the scratch table only for a non-null group.
    Total null_group;
    Work work;
    auto add_output = [&](Word key, Total total) {
        if (!output) output.emplace(713); // Generic boundary control, not tuned geometry.
        add(*output, key, total, work);
    };
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        const auto& selection = query.selection[i];
        const auto& coarse = query.coarse[i];
        work.coarse_selected += coarse.selected();
        work.coarse_excluded += coarse.count - coarse.selected();
        if (!coarse.selected()) work.groups_skipped += coarse.group_count();
        if (!selection.any || !coarse.selected() || part.rows.empty()) {
            ++work.parts_skipped;
            continue;
        }
        std::vector<Total> local;
        std::vector<bool> accepts;
        bool prepared = false;
        auto prepare = [&] {
            local.resize(part.dictionary.size());
            work.counter_bytes_peak = std::max(work.counter_bytes_peak,
                                               Word(local.capacity() * sizeof(Total)));
            for (const auto value : part.dictionary) accepts.push_back(query.accepts(value));
            work.dictionary_values += part.dictionary.size();
            prepared = true;
        };
        auto consume = [&](const Row& row) {
            ++work.rows;
            if (!row.has_x || row.x < query.threshold) return;
            if (row.code == -1) {
                if (query.include_null) {
                    add(null_group, {1, row.x});
                    ++work.null_updates;
                }
            } else if (by_code) {
                if (!prepared) prepare();
                if (accepts[row.code]) add(local[row.code], {1, row.x});
            } else {
                const auto key = part.dictionary[row.code];
                ++work.dictionary_values;
                if (query.accepts(key)) add_output(key >> query.shift, {1, row.x});
            }
        };
        auto visit = [&](std::size_t first, std::size_t limit) {
            ++work.coarse_ranges;
            for (auto rank = first; rank < limit; ++rank) {
                if (!selection.all) {
                    ++work.position_probes;
                    if (!selection.contains(part.positions[rank])) continue;
                }
                consume(part.rows[rank]);
            }
        };
        auto visit_blocks = [&](unsigned first_block, unsigned limit_block) {
            const auto first = first_at(part, first_block * coarse.grain, work);
            const auto limit = first_at(part, std::min(part.limit, limit_block * coarse.grain), work);
            visit(first, limit);
        };
        if (coarse.all) {
            work.groups_full += coarse.group_count();
            visit(0, part.rows.size());
        } else {
            unsigned active_groups = 0;
            for (std::size_t first = 0; first < coarse.blocks.size();) {
                const auto group = coarse.blocks[first] / coarse.per_group;
                auto limit = first + 1;
                while (limit < coarse.blocks.size() && coarse.blocks[limit] / coarse.per_group == group)
                    ++limit;
                ++active_groups;
                const auto first_block = group * coarse.per_group;
                const auto limit_block = std::min(coarse.count, first_block + coarse.per_group);
                if (limit - first == limit_block - first_block) {
                    ++work.groups_full;
                    visit_blocks(first_block, limit_block);
                } else {
                    ++work.groups_mixed;
                    for (auto j = first; j < limit; ++j)
                        visit_blocks(coarse.blocks[j], coarse.blocks[j] + 1);
                }
                first = limit;
            }
            work.groups_skipped += coarse.group_count() - active_groups;
        }
        if (by_code) {
            for (std::size_t code = 0; code < local.size(); ++code)
                if (local[code].count) {
                    ++work.dictionary_values; // Key translation after dictionary classification.
                    add_output(part.dictionary[code] >> query.shift, local[code]);
                }
        }
    }
    std::vector<std::pair<Word, Total>> groups;
    if (output) {
        output->validate();
        output->visit([&](Word key, const auto& value) {
            Total total;
            std::memcpy(&total, value.data(), sizeof(total));
            groups.emplace_back(key, total);
        });
    }
    std::ranges::sort(groups, {}, &std::pair<Word, Total>::first);
    std::cout << "RESULT " << std::quoted(query.id) << ' '
              << groups.size() + bool(null_group.count) << '\n';
    if (null_group.count)
        std::cout << "0 0 " << null_group.count << ' ' << null_group.sum << '\n';
    for (const auto& [key, total] : groups)
        std::cout << "1 " << key << ' ' << total.count << ' ' << total.sum << '\n';
    std::cout << "WORK2 " << work.rows << ' ' << work.dictionary_values << ' '
              << work.hash_updates << ' ' << work.null_updates << ' '
              << work.counter_bytes_peak << ' ' << work.parts_skipped << ' '
              << work.coarse_selected << ' ' << work.coarse_excluded << ' '
              << work.coarse_ranges << ' ' << work.position_probes << ' '
              << work.rows * sizeof(Row) << ' ' << work.groups_skipped << ' '
              << work.groups_full << ' ' << work.groups_mixed << '\n';
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2 && (std::string(argv[1]) == "rowwise" ||
                             std::string(argv[1]) == "by-code"),
                "usage: frame_execution_hash_check rowwise|by-code < transport.txt");
        std::string format, snapshot;
        std::size_t part_count, query_count;
        std::cin >> format >> std::quoted(snapshot) >> part_count >> query_count;
        require(bool(std::cin) && format == "frame-execution-transport-v2" &&
                !snapshot.empty(), "invalid workload header");
        std::vector<Part> parts;
        std::unordered_set<std::string> ids;
        for (std::size_t i = 0; i < part_count; ++i) {
            auto part = read_part();
            require(ids.insert(part.id).second, "duplicate part identity");
            parts.push_back(std::move(part));
        }
        for (std::size_t i = 0; i < query_count; ++i)
            execute(parts, read_query(parts), std::string(argv[1]) == "by-code");
        std::cin >> std::ws;
        require(std::cin.eof(), "trailing workload content");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
