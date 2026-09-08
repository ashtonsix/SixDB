#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace signatures {
using U = uint64_t;
using Row = std::array<uint32_t, 8>;
using Bits = std::array<U, 4>;
enum class Expr { one, both, either, cnf };
struct Case {
  std::string name, family, distribution;
  Expr expr = Expr::both;
  std::array<unsigned, 4> fields{0, 1, 2, 3};
  unsigned rate =
      100; // Combined mass of four target values, parts per ten thousand.
};
struct Query {
  Expr expr;
  std::array<unsigned, 4> fields;
  std::array<uint32_t, 4> values;
  uint32_t lo = 0, hi = 0;
  std::string term;
};
struct Fixture {
  Case config;
  std::vector<Row> rows;
  std::array<std::vector<uint32_t>, 8> columns;
  std::vector<std::string> strings;
  std::vector<Query> queries;
  size_t size() const {
    return config.family == "text" ? strings.size() : rows.size();
  }
};
struct Work {
  U candidates = 0, residuals = 0, certain = 0, plane_bytes = 0,
    summary_bytes = 0;
  U blocks = 0, skipped = 0, text_bytes = 0;
  std::array<U, 4> groups{};
};
struct Result {
  U count = 0, sum = 0;
  bool operator==(const Result &) const = default;
};
struct Store {
  std::string method;
  unsigned width = 0, block = 0;
  std::vector<uint32_t> words;
  std::array<std::vector<uint16_t>, 2> halves;
  std::array<std::vector<uint8_t>, 4> bytes;
  std::vector<Bits> text;
  std::vector<std::array<Bits, 4>> joint;
  std::array<std::vector<Bits>, 4> joint_planes;
  std::vector<std::array<uint16_t, 8>> marginal;
  std::vector<uint32_t> unions;
  // Query-specific exact term materialization, one bit per row and query.
  std::vector<std::vector<U>> exact;
  U allocated() const;
};
U mix(U x);
uint32_t tag(unsigned field, uint32_t value, U seed);
uint32_t shared(unsigned field, uint32_t value, U seed, unsigned width);
std::vector<Case> cases();
std::vector<std::string> methods(const Case &);
Fixture fixture(Case c, size_t n, U seed, const std::string &text_path = "");
Store build(const Fixture &, const std::string &, U hash_seed);
Result oracle(const Fixture &, const Query &, std::vector<U> *mask = nullptr);
Result scan(const Fixture &, const Store &, const Query &, U hash_seed,
            std::vector<U> &mask, Work *work = nullptr);
void replace(Row &row, Store &, size_t id, unsigned field, uint32_t value,
             U seed);
unsigned atoms(Expr);
bool evaluate(Expr, const std::array<bool, 4> &);
} // namespace signatures
