#pragma once
#include "lower.h"

namespace lowering {
enum class Bool { no, yes, atom, all, any, sequence };
struct Clause {
    Bool op;
    std::vector<size_t> children{};
    Glob atom{};
};
struct Factored {
    std::vector<Clause> nodes{{Bool::no}, {Bool::yes}};
    size_t root=1, relaxations=0;
    bool latin1=false, ordered=false;
    std::vector<size_t> reachable() const;
    size_t atoms() const;
};
// Necessary conditions only. `chains` packs adjacent linear AST regions into
// LIKE atoms; false uses only literal containment atoms. No DNF expansion.
Factored factor(const Plan&, bool chains, size_t node_budget=4096, bool ordered=false);
struct Work {
    uint64_t atoms=0, offered_bytes=0, node_visits=0, cache_hits=0;
    Work& operator+=(const Work&);
};
using Order = std::vector<std::vector<size_t>>;
// Eager evaluation visits all reachable nodes and can gather warmup counts.
// The independent flag selects the DP LIKE oracle for correctness checks.
bool evaluate(const Factored&, std::string_view, Work&, const Order* = nullptr,
              bool eager=false, std::vector<uint64_t>* positives=nullptr,
              bool independent=false);
Order train_order(const Factored&, const std::vector<uint64_t>& positives, size_t rows);
struct PositionWork {
    uint64_t atoms=0, node_visits=0, cache_hits=0, position_visits=0;
    size_t peak_buffer_bytes=0, peak_memo_entries=0;
};
// Existential, ordered, non-overlapping witnesses with arbitrary gaps between
// clauses. Earliest completion dominates later completions in this profile.
bool ordered_match(const Factored&, std::string_view, PositionWork&, bool independent=false);
std::string render(const Factored&);
} // namespace lowering
