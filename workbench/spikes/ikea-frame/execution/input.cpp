#include "input.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace frame_execution {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Part read_part(std::istream& in) {
    Part part;
    std::string token;
    std::size_t dictionary_count, row_count;
    in >> token >> std::quoted(part.id) >> std::quoted(part.dictionary_edition)
             >> part.limit >> dictionary_count >> row_count;
    require(bool(in) && token == "PART" && !part.id.empty() &&
            !part.dictionary_edition.empty() && part.limit <= 65536 &&
            row_count <= part.limit, "invalid part header");
    part.dictionary.resize(dictionary_count);
    std::unordered_set<Word> keys;
    for (auto& key : part.dictionary) {
        in >> key;
        require(bool(in) && keys.insert(key).second, "invalid dictionary");
    }
    std::vector<bool> occupied(part.limit);
    for (std::size_t i = 0; i < row_count; ++i) {
        Row row;
        unsigned has_x;
        in >> row.position >> row.code >> has_x >> row.x;
        require(bool(in) && row.position < part.limit && !occupied[row.position] &&
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

Query read_query(std::istream& in, const std::vector<Part>& parts) {
    Query query;
    std::string token;
    unsigned all, nulls;
    std::size_t allowed;
    in >> token >> std::quoted(query.id) >> query.threshold >> all >> nulls
             >> query.shift >> allowed;
    require(bool(in) && token == "QUERY" && !query.id.empty() && all <= 1 &&
            nulls <= 1 && query.shift <= 63, "invalid query header");
    query.all_categories = all;
    query.include_null = nulls;
    for (std::size_t i = 0; i < allowed; ++i) {
        Word key;
        in >> key;
        require(bool(in), "invalid predicate value");
        query.allowed.insert(key);
    }
    for (const auto& part : parts) {
        std::int64_t count;
        in >> count;
        require(bool(in) && count >= -1 && count <= part.limit,
                "invalid selection count");
        Selection selected{count == -1, count == -1 ? part.limit != 0 : count != 0,
                           std::vector<bool>(count > 0 ? part.limit : 0)};
        for (std::int64_t i = 0; i < count; ++i) {
            unsigned position;
            in >> position;
            require(bool(in) && position < part.limit && !selected.positions[position],
                    "invalid selected position");
            selected.positions[position] = true;
        }
        query.selection.push_back(std::move(selected));
        Coarse coarse;
        in >> token >> coarse.grain >> coarse.per_group >> count;
        require(bool(in) && token == "COARSE" && coarse.grain &&
                coarse.grain <= 65536 && coarse.per_group && coarse.per_group <= 65536 &&
                count >= -1, "invalid coarse header");
        coarse.count = (part.limit + coarse.grain - 1) / coarse.grain;
        coarse.all = count == -1;
        require(count <= coarse.count, "invalid coarse selection count");
        for (std::int64_t i = 0; i < count; ++i) {
            unsigned block;
            in >> block;
            require(bool(in) && block < coarse.count, "invalid coarse block");
            coarse.blocks.push_back(block);
        }
        std::ranges::sort(coarse.blocks);
        require(std::adjacent_find(coarse.blocks.begin(), coarse.blocks.end()) ==
                coarse.blocks.end(), "duplicate coarse block");
        query.coarse.push_back(std::move(coarse));
    }
    return query;
}

}
Input read_input(std::istream& in) {
    Input input;
    std::string format;
    std::size_t part_count, query_count;
    in >> format >> std::quoted(input.snapshot) >> part_count >> query_count;
    require(bool(in) && format == "frame-execution-transport-v2" && !input.snapshot.empty(),
            "invalid workload header");
    std::unordered_set<std::string> ids;
    for (std::size_t i = 0; i < part_count; ++i) {
        auto part = read_part(in);
        require(ids.insert(part.id).second, "duplicate part identity");
        input.parts.push_back(std::move(part));
    }
    for (std::size_t i = 0; i < query_count; ++i)
        input.queries.push_back(read_query(in, input.parts));
    in >> std::ws;
    require(in.eof(), "trailing workload content");
    return input;
}
}
