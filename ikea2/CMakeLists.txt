add_library(ikea2_seriespack STATIC src/readers.cpp src/admission.cpp src/representation.cpp)
add_library(ikea2::seriespack ALIAS ikea2_seriespack)
sixdb_target(ikea2_seriespack)
target_include_directories(ikea2_seriespack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea2_seriespack PUBLIC cxx_std_23)

add_executable(ikea2_check EXCLUDE_FROM_ALL test/support.cpp test/read/wire.cpp test/read/composition.cpp test/mutation/range.cpp test/mutation/substitution.cpp test/execution/chain.cpp test/mutation/construction.cpp test/format/descriptor.cpp
  test/mutation/widths_1_8.cpp test/mutation/widths_9_16.cpp test/mutation/widths_17_24.cpp test/mutation/widths_25_32.cpp
  test/mutation/widths_33_40.cpp test/mutation/widths_41_48.cpp test/mutation/widths_49_56.cpp test/mutation/widths_57_64.cpp
  test/mutation/dense.cpp test/mutation/verify.cpp)
sixdb_target(ikea2_check)
target_link_libraries(ikea2_check PRIVATE ikea2::seriespack)

add_executable(ikea2_integration_check EXCLUDE_FROM_ALL test/integration/ownership.cpp test/support.cpp)
sixdb_target(ikea2_integration_check)
target_link_libraries(ikea2_integration_check PRIVATE ikea2::seriespack)

add_executable(ikea2_ordinary_check EXCLUDE_FROM_ALL examples/ordinary.cpp)
sixdb_target(ikea2_ordinary_check)
target_link_libraries(ikea2_ordinary_check PRIVATE ikea2::seriespack)

foreach(example composition pipeline integration)
  add_executable(ikea2_example_${example} EXCLUDE_FROM_ALL examples/${example}.cpp)
  sixdb_target(ikea2_example_${example})
  target_link_libraries(ikea2_example_${example} PRIVATE ikea2::seriespack)
endforeach()
add_custom_target(ikea2_examples DEPENDS ikea2_ordinary_check ikea2_example_composition
  ikea2_example_pipeline ikea2_example_integration)
add_custom_target(ikea2_validate
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/test/reference/check.py"
  COMMAND $<TARGET_FILE:ikea2_check>
  COMMAND $<TARGET_FILE:ikea2_integration_check>
  COMMAND $<TARGET_FILE:ikea2_ordinary_check>
  COMMAND $<TARGET_FILE:ikea2_example_composition>
  COMMAND $<TARGET_FILE:ikea2_example_pipeline>
  COMMAND $<TARGET_FILE:ikea2_example_integration>
  DEPENDS ikea2_check ikea2_integration_check ikea2_examples
  USES_TERMINAL)

option(IKEA2_BENCHMARKS "Build maintained SeriesPack benchmarks" OFF)
option(IKEA2_COMPARISONS "Build frozen predecessor comparisons from Workbench" OFF)
if(IKEA2_BENCHMARKS OR IKEA2_COMPARISONS)
  include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")
  add_library(ikea2_bench_cases OBJECT bench/ordinary.cpp bench/stress_chain.cpp bench/pipeline_packets.cpp
    bench/placed_low.cpp bench/placed_mid.cpp bench/placed_wide.cpp)
  sixdb_target(ikea2_bench_cases)
  target_link_libraries(ikea2_bench_cases PRIVATE ikea2::seriespack benchmark::benchmark)
  add_executable(ikea2_bench bench/main.cpp)
  sixdb_target(ikea2_bench)
  target_link_libraries(ikea2_bench PRIVATE ikea2_bench_cases ikea2::seriespack benchmark::benchmark)
  foreach(style inline cps)
    add_library(ikea2_catalog_${style} STATIC EXCLUDE_FROM_ALL bench/catalog_${style}.cpp)
    sixdb_target(ikea2_catalog_${style})
    target_link_libraries(ikea2_catalog_${style} PRIVATE ikea2::seriespack)
  endforeach()
endif()
if(IKEA2_COMPARISONS)
  set(campaign "${PROJECT_SOURCE_DIR}/workbench/spikes/ikea-composition/ikea2-campaign")
  add_subdirectory("${campaign}/reference" "${CMAKE_CURRENT_BINARY_DIR}/predecessor" EXCLUDE_FROM_ALL)
  add_executable(ikea2_compare "${campaign}/bench.cpp" "${campaign}/mutation.cpp"
    "${PROJECT_SOURCE_DIR}/workbench/benchmarks/seriespack/probe_controls.cpp")
  sixdb_target(ikea2_compare)
  target_include_directories(ikea2_compare PRIVATE "${PROJECT_SOURCE_DIR}/workbench/benchmarks/seriespack")
  target_link_libraries(ikea2_compare PRIVATE ikea2_bench_cases ikea2::seriespack ikea2_predecessor benchmark::benchmark)
endif()
