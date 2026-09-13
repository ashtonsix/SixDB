include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")
add_executable(ikea_bec256_bench main.cpp composition.cpp)
sixdb_target(ikea_bec256_bench)
target_link_libraries(ikea_bec256_bench PRIVATE ikea::bec256 benchmark::benchmark)
