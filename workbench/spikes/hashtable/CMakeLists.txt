include("${PROJECT_SOURCE_DIR}/workbench/benchmarks/hashtable/abseil.cmake")
function(hashtable_frontier_target name source)
  add_executable(${name} EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
  sixdb_target(${name})
  target_compile_options(${name} PRIVATE -mllvm -inline-threshold=1000)
  target_link_libraries(${name} PRIVATE ikea::hashtable ikea::streampack hashtable_abseil_baseline loom::loom)
endfunction()
set(frontier_checks)
foreach(pair IN ITEMS "ordinary|multimap/multimap-check.cpp" "observed|multimap/guarded/observed-check.cpp" "ring|multimap/guarded/ring-check.cpp" "growth|multimap/rebuild/mm-check.cpp" "merge|multimap/rebuild/mm-merge-check.cpp")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 name)
  list(GET fields 1 source)
  set(target hashtable_mm_${name}_check)
  hashtable_frontier_target(${target} ${source})
  target_compile_definitions(${target} PRIVATE WRITE_ARCH_GUARDED_TYPED=1 WRITE_ARCH_OBSERVED_TYPED=1 WRITE_ARCH_FITTING_PLAN=1 WRITE_ARCH_POOL_ASSIGNMENT=1 WRITE_ARCH_POOL_BORROWED=1)
  list(APPEND frontier_checks ${target})
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endforeach()
add_custom_target(hashtable_frontier_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure --no-tests=error -R "^hashtable_mm_.*_check$"
  DEPENDS ${frontier_checks} USES_TERMINAL VERBATIM)
foreach(v IN ITEMS 8 64)
  hashtable_frontier_target(hashtable_mm_scalar_${v} multimap/multimap-compare.cpp)
  target_compile_definitions(hashtable_mm_scalar_${v} PRIVATE NDEBUG WRITE_ARCH_V=${v} WRITE_ARCH_MM_FOCUSED=1)
  hashtable_frontier_target(hashtable_mm_loom_write_${v} multimap/guarded/loom-compare.cpp)
  target_compile_definitions(hashtable_mm_loom_write_${v} PRIVATE NDEBUG WRITE_ARCH_V=${v} WRITE_ARCH_LOOM_WRITE_B=10 WRITE_ARCH_LOOM_WRITE_K=30
    WRITE_ARCH_GUARDED_TYPED=1 WRITE_ARCH_OBSERVED_TYPED=1 WRITE_ARCH_FITTING_PLAN=1 WRITE_ARCH_POOL_ASSIGNMENT=1 WRITE_ARCH_POOL_BORROWED=1)
endforeach()

# Keep the selected read/chain caller executable after archival; the fixed K30
# geometry identifies the retained experiment rather than a new tuning search.
foreach(v IN ITEMS 8 64)
  hashtable_frontier_target(hashtable_mm_loom_read_${v} multimap/loom-composition/compare.cpp)
  target_link_libraries(hashtable_mm_loom_read_${v} PRIVATE workbench::pmu)
  target_compile_definitions(hashtable_mm_loom_read_${v} PRIVATE NDEBUG
    IBS_W=40 IBS_B=10 IBS_V=${v} IBS_E=30 IBS_MULTI=1 IBS_BAGS=1)
endforeach()
hashtable_frontier_target(hashtable_mm_loom_read_check multimap/loom-composition/check.cpp)
target_link_libraries(hashtable_mm_loom_read_check PRIVATE workbench::pmu)
target_compile_definitions(hashtable_mm_loom_read_check PRIVATE
  IBS_W=40 IBS_B=10 IBS_V=8 IBS_E=30 IBS_MULTI=1 IBS_BAGS=1)
add_test(NAME hashtable_mm_loom_read_check COMMAND hashtable_mm_loom_read_check)
add_dependencies(hashtable_frontier_validate hashtable_mm_loom_read_check)
