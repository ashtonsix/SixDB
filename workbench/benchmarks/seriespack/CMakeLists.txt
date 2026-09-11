# The old spike selector also includes this file; define each target once.
if(TARGET ikea_seriespack_bench)
  return()
endif()
set(seriespack_benchmark_sources "${CMAKE_CURRENT_LIST_DIR}")
include("${PROJECT_SOURCE_DIR}/cmake/GoogleBenchmark.cmake")

add_executable(ikea_seriespack_bench "${seriespack_benchmark_sources}/bench.cpp" "${seriespack_benchmark_sources}/points.cpp"
  "${seriespack_benchmark_sources}/composition.cpp" "${seriespack_benchmark_sources}/api.cpp" "${seriespack_benchmark_sources}/probe_controls.cpp"
  "${seriespack_benchmark_sources}/ranges.cpp" "${seriespack_benchmark_sources}/boundaries.cpp"
  "${seriespack_benchmark_sources}/head_placement.cpp" "${seriespack_benchmark_sources}/checked_point.cpp")
sixdb_target(ikea_seriespack_bench)
target_link_libraries(ikea_seriespack_bench PRIVATE ikea::seriespack benchmark::benchmark)

set(SERIESPACK_PRIOR_DIR "" CACHE PATH "Captured Calico BytePack include directory")
if(SERIESPACK_PRIOR_DIR)
  add_library(ikea_seriespack_prior STATIC "${seriespack_benchmark_sources}/calico.cpp")
  sixdb_target(ikea_seriespack_prior)
  # Make Calico's fixed <=5-plane loop specialize with the bound constexpr
  # shape. These benchmark-only controls keep dynamic plane dispatch from
  # becoming an artificial ceiling; they do not change production Ikea flags.
  target_compile_options(ikea_seriespack_prior PRIVATE
    "SHELL:-mllvm -unroll-threshold=100000" "SHELL:-mllvm -unroll-full-max-count=8")
  target_include_directories(ikea_seriespack_prior SYSTEM PRIVATE "${SERIESPACK_PRIOR_DIR}")
  if(SIXDB_TUNE STREQUAL "zen5")
    target_compile_definitions(ikea_seriespack_prior PRIVATE BP_CPU=zen5)
  elseif(SIXDB_TUNE STREQUAL "granite-rapids")
    target_compile_definitions(ikea_seriespack_prior PRIVATE BP_CPU=granite_rapids)
  elseif(SIXDB_TUNE STREQUAL "neoverse-v2")
    target_compile_definitions(ikea_seriespack_prior PRIVATE BP_CPU=neoverse_v2)
  endif()
  target_link_libraries(ikea_seriespack_bench PRIVATE ikea_seriespack_prior)
  target_compile_definitions(ikea_seriespack_bench PRIVATE SERIESPACK_HAS_PRIOR=1)
endif()
