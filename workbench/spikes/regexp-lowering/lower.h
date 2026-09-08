#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <re2/re2.h>

namespace lowering {
constexpr size_t infinity = size_t(-1) / 4;
enum class Kind { literal, one, many, begin, end };
struct Token {
    Kind kind;
    std::string text;
    size_t maximum = 0; // Conservative decoded byte width, separate from LIKE.
    std::vector<std::pair<int,int>> ranges{}; // Rich IR only; empty means unrestricted.
};
using Glob = std::vector<Token>;
struct Plan {
    std::unique_ptr<RE2> oracle;
    std::vector<Glob> branches; // OR of whole-value LIKEs.
    std::vector<Glob> regions;  // Same alternatives before outer search %s.
    bool exact = true;
    bool context = false;      // Bounds decline assertions and byte-in-UTF8 ops.
    bool latin1 = false;
    std::string reason;
    std::string mandatory;
    bool useful() const;
};
Plan compile(const std::string& pattern, bool insensitive = false,
             bool latin1 = false, size_t branch_budget = 8);
bool like(const Glob&, std::string_view, bool latin1);
bool like_oracle(const Glob&, std::string_view, bool latin1);
bool matches(const Plan&, std::string_view);
bool rich_matches(const Plan&, std::string_view);
bool reference(const Plan&, std::string_view);
std::string render(const Plan&);
using Interval = std::pair<size_t, size_t>;
std::vector<Interval> bounds(const Plan&, std::string_view, size_t occurrence_cap = 128);
bool verify_bounds(const Plan&, std::string_view, const std::vector<Interval>&);
size_t coverage(const std::vector<Interval>&);
} // namespace lowering
