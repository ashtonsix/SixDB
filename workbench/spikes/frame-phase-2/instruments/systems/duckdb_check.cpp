// Native prepared SQL over an owned typed table. No Python or timed ingestion.
#include <duckdb.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
using wide = __int128;
void need(bool value, const char* what) {
    if (!value) throw std::runtime_error(what);
}
struct database {
    duckdb_database db{};
    duckdb_connection connection{};
    database() {
        duckdb_config config{};
        need(duckdb_create_config(&config) == DuckDBSuccess, "create config");
        const auto configured = duckdb_set_config(config, "threads", "1");
        char* error{};
        const auto opened = configured == DuckDBSuccess ? duckdb_open_ext(nullptr, &db, config, &error) : DuckDBError;
        duckdb_destroy_config(&config);
        if (error) {
            const std::string message(error); duckdb_free(error);
            if (db) duckdb_close(&db);
            throw std::runtime_error(message);
        }
        need(opened == DuckDBSuccess, "open database");
        if (duckdb_connect(db, &connection) != DuckDBSuccess) {
            duckdb_close(&db);
            throw std::runtime_error("connect");
        }
    }
    ~database() { if (connection) duckdb_disconnect(&connection); if (db) duckdb_close(&db); }
    void execute(const char* sql) {
        duckdb_result result{};
        const auto ok = duckdb_query(connection, sql, &result);
        const std::string error = ok == DuckDBSuccess ? "" : duckdb_result_error(&result);
        duckdb_destroy_result(&result);
        if (ok != DuckDBSuccess) throw std::runtime_error(error);
    }
};
struct result {
    duckdb_result value{};
    ~result() { duckdb_destroy_result(&value); }
};
struct chunk {
    duckdb_data_chunk value{};
    ~chunk() { if (value) duckdb_destroy_data_chunk(&value); }
};
struct appender {
    duckdb_appender value{};
    ~appender() { if (value) duckdb_appender_destroy(&value); }
};
struct statement {
    duckdb_prepared_statement value{};
    ~statement() { if (value) duckdb_destroy_prepare(&value); }
};
std::int16_t a_value(std::size_t i) { return std::int16_t(int(i % 65) - 32); }
std::int64_t b_value(std::size_t i) {
    if (i % 511 == 0) return INT64_MAX;
    if (i % 509 == 0) return INT64_MIN;
    return -INT64_C(1000000000000000000) + std::int64_t(i) * INT64_C(100000000007);
}
bool a_valid(std::size_t i, bool nullable) { return !nullable || i % 11 != 0; }
bool b_valid(std::size_t i, bool nullable) { return !nullable || i % 13 != 0; }
void load(database& db, std::size_t rows, bool nullable) {
    db.execute("CREATE TABLE frame_values(a SMALLINT, b BIGINT)");
    appender append;
    need(duckdb_appender_create(db.connection, nullptr, "frame_values", &append.value) == DuckDBSuccess, "appender");
    duckdb_logical_type types[]{duckdb_create_logical_type(DUCKDB_TYPE_SMALLINT),
                                duckdb_create_logical_type(DUCKDB_TYPE_BIGINT)};
    chunk data{duckdb_create_data_chunk(types, 2)};
    for (auto& type : types) duckdb_destroy_logical_type(&type);
    need(data.value != nullptr, "input chunk");
    const auto capacity = std::size_t(duckdb_vector_size());
    for (std::size_t first = 0; first < rows; first += capacity) {
        duckdb_data_chunk_reset(data.value);
        const auto count = std::min(capacity, rows - first);
        const auto av = duckdb_data_chunk_get_vector(data.value, 0);
        const auto bv = duckdb_data_chunk_get_vector(data.value, 1);
        auto* ap = static_cast<std::int16_t*>(duckdb_vector_get_data(av));
        auto* bp = static_cast<std::int64_t*>(duckdb_vector_get_data(bv));
        duckdb_vector_ensure_validity_writable(av); duckdb_vector_ensure_validity_writable(bv);
        auto* am = duckdb_vector_get_validity(av); auto* bm = duckdb_vector_get_validity(bv);
        for (std::size_t j = 0; j < count; ++j) {
            ap[j] = a_value(first + j); bp[j] = b_value(first + j);
            duckdb_validity_set_row_validity(am, j, a_valid(first + j, nullable));
            duckdb_validity_set_row_validity(bm, j, b_valid(first + j, nullable));
        }
        duckdb_data_chunk_set_size(data.value, count);
        need(duckdb_append_data_chunk(append.value, data.value) == DuckDBSuccess, "append chunk");
    }
    need(duckdb_appender_close(append.value) == DuckDBSuccess, "close appender");
    // Both the input chunk and appender die before any query. The control must
    // consume the database's table rather than borrowing a dead input buffer.
}
std::size_t check(std::size_t rows, bool nullable) {
    database db; load(db, rows, nullable);
    statement query;
    need(duckdb_prepare(db.connection,
        "SELECT sum(b), count(b), count(*) FROM frame_values WHERE a >= ?", &query.value) == DuckDBSuccess,
        "prepare predicate and payload");
    std::size_t checks = 0;
    for (const std::int16_t threshold : std::array<std::int16_t, 9>{-32768,-33,-32,-1,0,1,31,32,-32768}) {
        wide expected = 0; std::uint64_t count = 0, matches = 0;
        for (std::size_t i = 0; i < rows; ++i) if (a_valid(i, nullable) && a_value(i) >= threshold) {
            ++matches;
            if (b_valid(i, nullable)) { expected += wide(b_value(i)); ++count; }
        }
        need(duckdb_bind_int16(query.value, 1, threshold) == DuckDBSuccess, "bind threshold");
        result answer;
        need(duckdb_execute_prepared(query.value, &answer.value) == DuckDBSuccess, "execute query");
        need(duckdb_column_count(&answer.value) == 3, "result columns");
        need(duckdb_column_type(&answer.value, 0) == DUCKDB_TYPE_HUGEINT &&
             duckdb_column_type(&answer.value, 1) == DUCKDB_TYPE_BIGINT, "actual aggregate types");
        chunk output{duckdb_fetch_chunk(answer.value)};
        need(output.value != nullptr && duckdb_data_chunk_get_size(output.value) == 1, "result chunk");
        const auto sum = duckdb_data_chunk_get_vector(output.value, 0);
        const bool valid = duckdb_validity_row_is_valid(duckdb_vector_get_validity(sum), 0);
        need(valid == (count != 0), "empty or all-NULL SUM law");
        if (valid) {
            const auto value = *static_cast<const duckdb_hugeint*>(duckdb_vector_get_data(sum));
            const wide actual = wide(value.upper) * (wide(1) << 64) + value.lower;
            need(actual == expected, "exact signed wide SUM");
        }
        for (unsigned c = 1; c < 3; ++c) {
            const auto vector = duckdb_data_chunk_get_vector(output.value, c);
            need(duckdb_validity_row_is_valid(duckdb_vector_get_validity(vector), 0), "COUNT validity");
            const auto actual = *static_cast<const std::int64_t*>(duckdb_vector_get_data(vector));
            need(actual >= 0 && std::uint64_t(actual) == (c == 1 ? count : matches), "COUNT law");
        }
        chunk extra{duckdb_fetch_chunk(answer.value)};
        need(!extra.value || duckdb_data_chunk_get_size(extra.value) == 0, "single result row");
        ++checks;
    }
    return checks;
}
} // namespace
int main() {
    try {
        need(std::strcmp(duckdb_library_version(), "v2.0.0") == 0, "pinned runtime version");
        std::size_t checks = 0;
        for (auto rows : {std::size_t(0),std::size_t(1),std::size_t(2048),std::size_t(2049),std::size_t(4097)})
            for (bool nullable : {false,true}) checks += check(rows, nullable);
        std::printf("{\"peer\":\"DuckDB C API\",\"version\":\"%s\",\"checks\":%zu,"
                    "\"vector_rows\":%llu,\"engine_build\":\"official release binary\",\"timing_claim\":false}\n",
                    duckdb_library_version(), checks, static_cast<unsigned long long>(duckdb_vector_size()));
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
