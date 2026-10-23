#pragma once
#include <cstdint>
#include <istream>
#include <string>
#include <unordered_set>
#include <vector>

// Private research input shared by logical and encoded complete consumers.
namespace frame_execution {
using Word = std::uint64_t;
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
struct Input {
    std::string snapshot;
    std::vector<Part> parts;
    std::vector<Query> queries;
};
Input read_input(std::istream&);
}
