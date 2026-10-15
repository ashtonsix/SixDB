include_guard(GLOBAL)
# Opt-in bridge; the original six-arm target and its dependencies are unchanged.
# The parent may include this under SIXDB_HASHTABLE_CONCURRENT_WORKERS.
if(NOT TARGET hashtable_concurrent_baselines)
  include("${CMAKE_CURRENT_LIST_DIR}/concurrent_baselines.cmake")
endif()
add_executable(hashtable_design_concurrent_workers "${CMAKE_CURRENT_LIST_DIR}/workers.cpp")
sixdb_target(hashtable_design_concurrent_workers)
target_link_libraries(hashtable_design_concurrent_workers PRIVATE
  hashtable_abseil_baseline hashtable_concurrent_baselines
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_compile_definitions(hashtable_design_concurrent_workers PRIVATE NDEBUG HT_CONCURRENT_WORKERS=1)
target_compile_options(hashtable_design_concurrent_workers PRIVATE
  -fno-vectorize -fno-slp-vectorize -mllvm -inline-threshold=1000)
add_test(NAME hashtable_design_concurrent_workers_check
  COMMAND hashtable_design_concurrent_workers check)
