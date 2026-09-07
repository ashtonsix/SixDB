add_library(aggregate_deltas_core STATIC model.cpp)
sixdb_target(aggregate_deltas_core)

add_executable(aggregate_deltas_check check.cpp)
sixdb_target(aggregate_deltas_check)
target_link_libraries(aggregate_deltas_check PRIVATE aggregate_deltas_core)

include(${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake)
add_executable(aggregate_deltas_bench bench.cpp)
sixdb_target(aggregate_deltas_bench)
target_link_libraries(aggregate_deltas_bench PRIVATE
  aggregate_deltas_core benchmark::benchmark)

add_subdirectory(dirty-buffer)
