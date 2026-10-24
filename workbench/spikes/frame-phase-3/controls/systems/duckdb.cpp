#include <duckdb.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using u64 = std::uint64_t;
using u128 = unsigned __int128;
using clock_type = std::chrono::steady_clock;
void need(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
std::string decimal(u128 value) {
    std::string out;
    do { out.push_back(static_cast<char>('0' + value % 10)); value /= 10; } while (value);
    std::reverse(out.begin(), out.end()); return out;
}
enum class op { sum, count, pair };
const char* name(op operation) { return operation == op::sum ? "sum" : operation == op::count ? "count" : "pair"; }
struct answer {
    u128 sum = 0;
    u64 count = 0;
    bool sum_valid = false;
    bool operator==(const answer&) const = default;
};
answer expected(std::span<const u64> a, std::span<const u64> b, op operation, u64 threshold) {
    answer out;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (operation == op::sum || a[i] >= threshold) {
            ++out.count;
            if (operation != op::count) { out.sum += b[i]; out.sum_valid = true; }
        }
    return out;
}
struct query {
    duckdb_database database{};
    duckdb_connection connection{};
    duckdb_prepared_statement prepared{};
    op operation;
    struct column {
        u64 base = 0, maximum = ~u64{0};
        unsigned bytes = 8;
    } ac, bc;
    mutable const char* sum_dtype = "not_requested";
    double construction_ns = 0, preparation_ns = 0;
    static column fit(std::span<const u64> values, bool typed_for) {
        if (!typed_for) return {};
        if (values.empty()) return {0, 0, 1};
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        const auto span = *hi - *lo;
        return {*lo, *hi, span <= UINT8_MAX ? 1u : span <= UINT16_MAX ? 2u : span <= UINT32_MAX ? 4u : 8u};
    }
    static const char* sql_type(unsigned bytes) {
        return bytes == 1 ? "UTINYINT" : bytes == 2 ? "USMALLINT" : bytes == 4 ? "UINTEGER" : "UBIGINT";
    }
    static duckdb_type type(unsigned bytes) {
        return bytes == 1 ? DUCKDB_TYPE_UTINYINT : bytes == 2 ? DUCKDB_TYPE_USMALLINT : bytes == 4 ? DUCKDB_TYPE_UINTEGER : DUCKDB_TYPE_UBIGINT;
    }
    static void deposit(duckdb_vector vector, std::span<const u64> values, column c) {
        const auto copy = [&]<class T>() {
            auto* out = static_cast<T*>(duckdb_vector_get_data(vector));
            for (std::size_t i = 0; i < values.size(); ++i) out[i] = static_cast<T>(values[i] - c.base);
        };
        if (c.bytes == 1) copy.template operator()<std::uint8_t>();
        else if (c.bytes == 2) copy.template operator()<std::uint16_t>();
        else if (c.bytes == 4) copy.template operator()<std::uint32_t>();
        else copy.template operator()<u64>();
    }
    query(std::span<const u64> a, std::span<const u64> b, op operation, u64 threshold, bool typed_for) : operation(operation) {
        need(a.size() == b.size(), "unequal column rows");
        const auto start = clock_type::now();
        if (operation != op::sum) ac = fit(a, typed_for);
        if (operation != op::count) bc = fit(b, typed_for);
        duckdb_config config{};
        need(duckdb_create_config(&config) == DuckDBSuccess, "create config");
        need(duckdb_set_config(config, "threads", "1") == DuckDBSuccess, "one thread");
        const auto opened = duckdb_open_ext(nullptr, &database, config, nullptr);
        duckdb_destroy_config(&config);
        need(opened == DuckDBSuccess && duckdb_connect(database, &connection) == DuckDBSuccess, "open database");
        duckdb_result created{};
        const auto schema = std::string("CREATE TABLE values_(") + (operation == op::sum ? "b " : "a ") +
            sql_type(operation == op::sum ? bc.bytes : ac.bytes) +
            (operation == op::pair ? std::string(",b ") + sql_type(bc.bytes) : "") + ")";
        const auto made = duckdb_query(connection, schema.c_str(), &created);
        duckdb_destroy_result(&created);
        need(made == DuckDBSuccess, "create typed table");
        duckdb_appender append{};
        need(duckdb_appender_create(connection, nullptr, "values_", &append) == DuckDBSuccess, "create appender");
        duckdb_logical_type types[]{duckdb_create_logical_type(type(operation == op::sum ? bc.bytes : ac.bytes)), duckdb_create_logical_type(type(bc.bytes))};
        auto data = duckdb_create_data_chunk(types, operation == op::pair ? 2 : 1);
        for (auto& type : types) duckdb_destroy_logical_type(&type);
        const auto capacity = static_cast<std::size_t>(duckdb_vector_size());
        for (std::size_t first = 0; first < a.size(); first += capacity) {
            duckdb_data_chunk_reset(data);
            const auto count = std::min(capacity, a.size() - first);
            const auto av = duckdb_data_chunk_get_vector(data, 0);
            const auto source = operation == op::sum ? b : a;
            deposit(av, source.subspan(first, count), operation == op::sum ? bc : ac);
            if (operation == op::pair) {
                deposit(duckdb_data_chunk_get_vector(data, 1), b.subspan(first, count), bc);
            }
            duckdb_data_chunk_set_size(data, count);
            need(duckdb_append_data_chunk(append, data) == DuckDBSuccess, "append typed chunk");
        }
        duckdb_destroy_data_chunk(&data);
        need(duckdb_appender_close(append) == DuckDBSuccess, "close appender");
        duckdb_appender_destroy(&append);
        construction_ns = std::chrono::duration<double, std::nano>(clock_type::now() - start).count();
        const auto prepare_start = clock_type::now();
        const char* select = operation == op::sum ? "SELECT SUM(b) FROM values_" : operation == op::count ?
            "SELECT COUNT(*) FROM values_" : "SELECT SUM(b),COUNT(*) FROM values_";
        const bool parameter = operation != op::sum && threshold > ac.base && threshold <= ac.maximum;
        const auto sql = std::string(select) + (operation == op::sum ? "" : threshold <= ac.base ? " WHERE true" : threshold > ac.maximum ? " WHERE false" : " WHERE a>=?");
        need(duckdb_prepare(connection, sql.c_str(), &prepared) == DuckDBSuccess, "prepare query");
        if (parameter) {
            const auto rhs = threshold - ac.base;
            const auto bound = ac.bytes == 1 ? duckdb_bind_uint8(prepared, 1, static_cast<std::uint8_t>(rhs)) :
                ac.bytes == 2 ? duckdb_bind_uint16(prepared, 1, static_cast<std::uint16_t>(rhs)) :
                ac.bytes == 4 ? duckdb_bind_uint32(prepared, 1, static_cast<std::uint32_t>(rhs)) : duckdb_bind_uint64(prepared, 1, rhs);
            need(bound == DuckDBSuccess, "bind unsigned threshold");
        }
        preparation_ns = std::chrono::duration<double, std::nano>(clock_type::now() - prepare_start).count();
    }
    ~query() {
        duckdb_destroy_prepare(&prepared); duckdb_disconnect(&connection); duckdb_close(&database);
    }
    query(const query&) = delete;
    answer execute(std::size_t rows) const {
        duckdb_result result{};
        need(duckdb_execute_prepared(prepared, &result) == DuckDBSuccess, "execute retained statement");
        auto data = duckdb_fetch_chunk(result);
        need(data && duckdb_data_chunk_get_size(data) == 1, "one result row");
        answer out;
        if (operation != op::count) {
            auto vector = duckdb_data_chunk_get_vector(data, 0);
            out.sum_valid = duckdb_validity_row_is_valid(duckdb_vector_get_validity(vector), 0);
            const auto dtype = duckdb_column_type(&result, 0);
            need(dtype == DUCKDB_TYPE_HUGEINT || dtype == DUCKDB_TYPE_UHUGEINT, "exact integral SUM result");
            sum_dtype = dtype == DUCKDB_TYPE_HUGEINT ? "HUGEINT" : "UHUGEINT";
            if (out.sum_valid && dtype == DUCKDB_TYPE_HUGEINT) {
                const auto actual = *static_cast<const duckdb_hugeint*>(duckdb_vector_get_data(vector));
                need(actual.upper >= 0, "unsigned sum returned a negative total");
                out.sum = (u128{static_cast<u64>(actual.upper)} << 64) | actual.lower;
            } else if (out.sum_valid) {
                const auto actual = *static_cast<const duckdb_uhugeint*>(duckdb_vector_get_data(vector));
                out.sum = (u128{actual.upper} << 64) | actual.lower;
            }
        }
        if (operation == op::sum) out.count = rows; // Known input cardinality, not an extra query result.
        else {
            const auto col = operation == op::count ? 0 : 1;
            need(duckdb_column_type(&result, col) == DUCKDB_TYPE_BIGINT, "COUNT result type");
            const auto actual = *static_cast<const std::int64_t*>(duckdb_vector_get_data(duckdb_data_chunk_get_vector(data, col)));
            need(actual >= 0, "negative count"); out.count = static_cast<u64>(actual);
        }
        if (operation != op::count) out.sum += u128{bc.base} * out.count;
        duckdb_destroy_data_chunk(&data); duckdb_destroy_result(&result);
        return out;
    }
};
std::vector<u64> load(const std::string& directory, const std::string& column, std::size_t rows) {
    need(std::endian::native == std::endian::little, "little-endian corpus required");
    std::ifstream file(directory + "/" + column + ".u64", std::ios::binary | std::ios::ate);
    need(file && static_cast<std::size_t>(file.tellg()) >= rows * 8, "missing/short corpus input");
    std::vector<u64> out(rows); file.seekg(0); file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(rows * 8));
    need(bool(file), "read corpus prefix"); return out;
}
u64 quantile(std::span<const u64> values, unsigned percent) {
    std::vector<u64> copy(values.begin(), values.end());
    const auto k = copy.size() * percent / 100;
    std::nth_element(copy.begin(), copy.begin() + k, copy.end()); return copy[k];
}
void qualify() {
    const std::vector<std::vector<u64>> fixtures{{}, {0, 0, 0}, {0, 1, ~u64{0}, ~u64{0} - 1}, {~u64{0} - 255, ~u64{0} - 3, ~u64{0}}};
    for (const bool typed_for : {false, true}) for (const auto& a : fixtures) for (auto operation : {op::sum, op::count, op::pair})
        for (auto t : {u64{0}, u64{1}, ~u64{0}}) {
            query q(a, a, operation, t, typed_for);
            need(q.execute(a.size()) == expected(a, a, operation, t), "unsigned qualification answer");
        }
    struct distinct_fixture {
        std::vector<u64> a, b;
        u64 interior;
        unsigned a_bytes, b_bytes;
    };
    constexpr u64 a_base = (u64{1} << 63) + 65536, b_base = u64{1} << 62;
    const std::vector<distinct_fixture> distinct{
        {{a_base, a_base + 300, a_base + 65535, a_base + 1000, a_base + 40000},
         {b_base + UINT32_MAX, b_base + (u64{1} << 31) + 17, b_base, b_base + 37, b_base + UINT32_MAX - 13},
         a_base + 1000, 2, 4},
        {{a_base + UINT32_MAX, a_base, a_base + 70000, a_base + 2100000000, a_base + 17},
         {b_base, b_base + 65535, b_base + 700, b_base + 1, b_base + 40000},
         a_base + 70000, 4, 2}};
    for (const bool typed_for : {false, true}) for (const auto &f : distinct)
        for (auto operation : {op::sum, op::count, op::pair})
            for (auto t : {u64{0}, f.interior, ~u64{0}}) {
                query q(f.a, f.b, operation, t, typed_for);
                if (typed_for) {
                    if (operation != op::sum) need(q.ac.bytes == f.a_bytes, "distinct predicate FOR width");
                    if (operation != op::count) need(q.bc.bytes == f.b_bytes, "distinct payload FOR width");
                    duckdb_result schema{};
                    need(duckdb_query(q.connection, "SELECT * FROM values_ LIMIT 0", &schema) == DuckDBSuccess,
                        "inspect actual typed fixture columns");
                    const auto actual_a = f.a_bytes == 2 ? DUCKDB_TYPE_USMALLINT : DUCKDB_TYPE_UINTEGER;
                    const auto actual_b = f.b_bytes == 2 ? DUCKDB_TYPE_USMALLINT : DUCKDB_TYPE_UINTEGER;
                    need(duckdb_column_type(&schema, 0) == (operation == op::sum ? actual_b : actual_a) &&
                        (operation != op::pair || duckdb_column_type(&schema, 1) == actual_b),
                        "actual typed fixture column dtype");
                    duckdb_destroy_result(&schema);
                }
                need(q.execute(f.a.size()) == expected(f.a, f.b, operation, t), "distinct-column qualification answer");
            }
}
void measure(const char* label, const char* an, const char* bn, const std::map<std::string, std::vector<u64>>& cols,
             op operation, unsigned quantile_percent, unsigned rounds, bool typed_for) {
    const auto& a = cols.at(an); const auto& b = cols.at(bn);
    const auto t = operation == op::sum ? 0 : quantile(a, quantile_percent);
    const auto want = expected(a, b, operation, t);
    std::size_t touched = 0;
    for (std::size_t first = 0; first < a.size(); first += 256) {
        bool hit = false;
        for (std::size_t i = first; i < std::min(first + 256, a.size()); ++i) hit |= operation == op::sum || a[i] >= t;
        touched += hit;
    }
    query q(a, b, operation, t, typed_for);
    need(q.execute(a.size()) == want, "warm query");
    std::vector<double> samples;
    for (unsigned repeat = 0; repeat < rounds; ++repeat) {
        const auto start = clock_type::now(); const auto actual = q.execute(a.size());
        samples.push_back(std::chrono::duration<double, std::nano>(clock_type::now() - start).count());
        need(actual == want, "timed exact result");
    }
    auto sorted = samples; std::sort(sorted.begin(), sorted.end());
    const auto source_bytes = a.capacity() * sizeof(u64) + (operation == op::pair && an != std::string(bn) ? b.capacity() * sizeof(u64) : 0);
    std::printf("{\"engine\":\"duckdb\",\"version\":\"%s\",\"case\":\"%s\",\"arm\":\"%s\",\"op\":\"%s\",\"a\":\"%s\",\"b\":\"%s\",\"a_lane_bytes\":%u,\"b_lane_bytes\":%u,\"a_base\":\"%llu\",\"b_base\":\"%llu\",\"sum_dtype\":\"%s\",\"rows\":%zu,\"threshold\":\"%llu\",\"selected_rows\":%llu,\"touched_blocks\":%zu,\"sum\":\"%s\",\"sum_valid\":%s,\"construction_ns\":%.0f,\"preparation_ns\":%.0f,\"known_source_capacity_bytes\":%zu,\"engine_visible_payload_bytes\":null,\"engine_retained_capacity_bytes\":null,\"condition\":\"repeated_after_untimed_warm_query\",\"query_boundary\":\"execute_prepared_fetch_consume_destroy\",\"threads\":1,\"median_ns\":%.0f,\"samples_ns\":[",
        duckdb_library_version(), label, typed_for ? "external_typed_for" : "sidecar_uint64", name(operation), an, bn,
        operation == op::sum ? 0 : q.ac.bytes, operation == op::count ? 0 : q.bc.bytes,
        static_cast<unsigned long long>(q.ac.base), static_cast<unsigned long long>(q.bc.base), q.sum_dtype,
        a.size(), static_cast<unsigned long long>(t), static_cast<unsigned long long>(want.count), touched,
        decimal(want.sum).c_str(), want.sum_valid ? "true" : "false", q.construction_ns, q.preparation_ns, source_bytes, sorted[sorted.size() / 2]);
    for (std::size_t i = 0; i < samples.size(); ++i) std::printf("%s%.0f", i ? "," : "", samples[i]);
    std::printf("]}\n");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--check-only") {
            qualify(); std::printf("{\"checks\":108,\"pass\":true}\n"); return 0;
        }
        need(argc == 4, "usage: frame3_duckdb_corpus COLS_DIR ROWS ROUNDS");
        const auto rows = std::stoull(argv[2]); const auto rounds = static_cast<unsigned>(std::stoul(argv[3]));
        need(rows && rows <= 65536 && rows % 256 == 0 && rounds && rounds <= 101, "bounded rows/rounds");
        qualify(); std::fprintf(stderr, "unsigned/extrema/empty/all-none/distinct-column qualification: 108 cases passed\n");
        std::map<std::string, std::vector<u64>> columns;
        for (const char* n : {"OS", "URLHash", "EventTime", "WatchID", "ClientIP"}) columns[n] = load(argv[1], n, rows);
        for (const bool typed_for : {false, true}) {
        for (const char* n : {"OS", "URLHash"}) {
            const auto sum_name = std::string("sum_") + n, count_name = std::string("count_") + n;
            measure(sum_name.c_str(), n, n, columns, op::sum, 0, rounds, typed_for);
            measure(count_name.c_str(), n, n, columns, op::count, 50, rounds, typed_for);
        }
        measure("pair_EventTime_ClientIP", "EventTime", "ClientIP", columns, op::pair, 90, rounds, typed_for);
        measure("pair_WatchID_ClientIP", "WatchID", "ClientIP", columns, op::pair, 90, rounds, typed_for);
        }
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
