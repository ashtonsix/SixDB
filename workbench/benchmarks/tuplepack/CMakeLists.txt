include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")
add_executable(ikea_tuplepack_bench main.cpp composition.cpp packets.cpp
  packets_wide.cpp packets_medium.cpp packets_narrow.cpp)
sixdb_target(ikea_tuplepack_bench)
target_link_libraries(ikea_tuplepack_bench PRIVATE ikea::tuplepack benchmark::benchmark)
