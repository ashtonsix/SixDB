include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")
add_library(ikea_seriespack_bench_cases OBJECT ordinary.cpp stress_chain.cpp pipeline_packets.cpp
  placed_low.cpp placed_mid.cpp placed_wide.cpp)
sixdb_target(ikea_seriespack_bench_cases)
target_link_libraries(ikea_seriespack_bench_cases PRIVATE ikea::seriespack benchmark::benchmark)
add_executable(ikea_seriespack_bench main.cpp)
sixdb_target(ikea_seriespack_bench)
target_link_libraries(ikea_seriespack_bench PRIVATE ikea_seriespack_bench_cases ikea::seriespack benchmark::benchmark)
foreach(style inline cps)
  add_library(ikea_seriespack_catalog_${style} STATIC EXCLUDE_FROM_ALL catalog_${style}.cpp)
  sixdb_target(ikea_seriespack_catalog_${style})
  target_link_libraries(ikea_seriespack_catalog_${style} PRIVATE ikea::seriespack)
endforeach()

option(IKEA_COMPARISONS "Build frozen predecessor comparisons from Workbench" OFF)
if(IKEA_COMPARISONS)
  set(campaign "${PROJECT_SOURCE_DIR}/workbench/spikes/ikea-composition/ikea2-campaign")
  add_subdirectory("${campaign}/reference" "${CMAKE_CURRENT_BINARY_DIR}/predecessor" EXCLUDE_FROM_ALL)
  add_executable(ikea_seriespack_compare "${campaign}/bench.cpp" "${campaign}/mutation.cpp"
    "${campaign}/probe_controls.cpp")
  sixdb_target(ikea_seriespack_compare)
  target_include_directories(ikea_seriespack_compare PRIVATE "${campaign}" "${campaign}/reference/include")
  target_link_libraries(ikea_seriespack_compare PRIVATE ikea_seriespack_bench_cases
    ikea::seriespack ikea_seriespack_predecessor benchmark::benchmark)
  add_custom_target(ikea_seriespack_reference_check
    COMMAND python3 "${campaign}/reference/check.py" USES_TERMINAL)
endif()
