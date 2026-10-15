include(baseline.cmake)
add_executable(hashtable_compare compare.cpp)
sixdb_target(hashtable_compare)
target_link_libraries(hashtable_compare PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
# Both maps receive the same release exposure. Checks use require(), not assert().
target_compile_definitions(hashtable_compare PRIVATE NDEBUG)
target_compile_options(hashtable_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_compare_check COMMAND hashtable_compare check)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/prior_compare.cpp")
  add_executable(hashtable_prior_compare prior_compare.cpp)
  sixdb_target(hashtable_prior_compare)
  target_link_libraries(hashtable_prior_compare PRIVATE hashtable_abseil_baseline)
  target_compile_definitions(hashtable_prior_compare PRIVATE NDEBUG)
  target_compile_options(hashtable_prior_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
  add_test(NAME hashtable_prior_compare_check COMMAND hashtable_prior_compare check)
endif()

add_executable(hashtable_width_check width_check.cpp)
sixdb_target(hashtable_width_check)
target_link_libraries(hashtable_width_check PRIVATE ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_options(hashtable_width_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_width_check COMMAND hashtable_width_check)

add_executable(hashtable_ring ring.cpp)
sixdb_target(hashtable_ring)
target_link_libraries(hashtable_ring PRIVATE hashtable_abseil_baseline ikea::seriespack ikea::tuplepack orbital::orbital loom::loom)
target_compile_definitions(hashtable_ring PRIVATE NDEBUG)
target_compile_options(hashtable_ring PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_ring_check COMMAND hashtable_ring 4096 8 1)

add_executable(hashtable_set_compare set_compare.cpp)
sixdb_target(hashtable_set_compare)
target_link_libraries(hashtable_set_compare PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_definitions(hashtable_set_compare PRIVATE NDEBUG)
target_compile_options(hashtable_set_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_set_compare_check COMMAND hashtable_set_compare check)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(hashtable_layout_cases "${CMAKE_CURRENT_BINARY_DIR}/layout-cases.txt")
add_custom_command(OUTPUT "${hashtable_layout_cases}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/layout_emit.py"
    --output "${hashtable_layout_cases}"
  DEPENDS layout_emit.py layout.py VERBATIM)
add_custom_target(hashtable_layout_cases DEPENDS "${hashtable_layout_cases}")
add_executable(hashtable_layout_check layout_check.cpp)
sixdb_target(hashtable_layout_check)
target_link_libraries(hashtable_layout_check PRIVATE ikea::tuplepack)
target_compile_options(hashtable_layout_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_dependencies(hashtable_layout_check hashtable_layout_cases)
add_test(NAME hashtable_layout_check COMMAND hashtable_layout_check check "${hashtable_layout_cases}")

add_executable(hashtable_lifecycle lifecycle.cpp)
sixdb_target(hashtable_lifecycle)
target_link_libraries(hashtable_lifecycle PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_definitions(hashtable_lifecycle PRIVATE NDEBUG)
target_compile_options(hashtable_lifecycle PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_lifecycle_check COMMAND hashtable_lifecycle check)

add_executable(hashtable_historical_bridge historical_bridge.cpp)
sixdb_target(hashtable_historical_bridge)
target_link_libraries(hashtable_historical_bridge PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_definitions(hashtable_historical_bridge PRIVATE NDEBUG)
target_compile_options(hashtable_historical_bridge PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_historical_bridge_check COMMAND hashtable_historical_bridge check)

add_executable(hashtable_packed_key_baseline_check packed_key_baseline_check.cpp)
sixdb_target(hashtable_packed_key_baseline_check)
target_link_libraries(hashtable_packed_key_baseline_check PRIVATE hashtable_abseil_baseline)
target_compile_options(hashtable_packed_key_baseline_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_packed_key_baseline_check COMMAND hashtable_packed_key_baseline_check)

add_executable(hashtable_domain_compare domain_compare.cpp)
sixdb_target(hashtable_domain_compare)
target_link_libraries(hashtable_domain_compare PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_compile_definitions(hashtable_domain_compare PRIVATE NDEBUG)
target_compile_options(hashtable_domain_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_domain_compare_check COMMAND hashtable_domain_compare check)

set(hashtable_single_plans "${CMAKE_CURRENT_BINARY_DIR}/single-plans.h")
add_custom_command(OUTPUT "${hashtable_single_plans}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/single_emit.py"
    --output "${hashtable_single_plans}"
  DEPENDS single_emit.py layout.py VERBATIM)
add_custom_target(hashtable_single_plans DEPENDS "${hashtable_single_plans}")
add_executable(hashtable_single_check single_check.cpp)
sixdb_target(hashtable_single_check)
target_link_libraries(hashtable_single_check PRIVATE ikea::seriespack ikea::tuplepack orbital::orbital)
target_include_directories(hashtable_single_check PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(hashtable_single_check PRIVATE -fno-vectorize -fno-slp-vectorize)
add_dependencies(hashtable_single_check hashtable_single_plans)
add_test(NAME hashtable_single_check COMMAND hashtable_single_check)
target_include_directories(hashtable_domain_compare PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(hashtable_domain_compare hashtable_single_plans)

add_executable(hashtable_single_lifecycle single_lifecycle.cpp)
sixdb_target(hashtable_single_lifecycle)
target_link_libraries(hashtable_single_lifecycle PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack orbital::orbital)
target_include_directories(hashtable_single_lifecycle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/orbital/src")
target_compile_definitions(hashtable_single_lifecycle PRIVATE NDEBUG)
target_compile_options(hashtable_single_lifecycle PRIVATE -fno-vectorize -fno-slp-vectorize)
add_dependencies(hashtable_single_lifecycle hashtable_single_plans)
add_test(NAME hashtable_single_lifecycle_check COMMAND hashtable_single_lifecycle check)

set(hashtable_table_plans "${CMAKE_CURRENT_BINARY_DIR}/table-plans.h")
add_custom_command(OUTPUT "${hashtable_table_plans}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/table_emit.py"
    --output "${hashtable_table_plans}"
  DEPENDS table_emit.py layout.py VERBATIM)
add_executable(hashtable_table_compare table_compare.cpp "${hashtable_table_plans}")
sixdb_target(hashtable_table_compare)
target_link_libraries(hashtable_table_compare PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_include_directories(hashtable_table_compare PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(hashtable_table_compare PRIVATE NDEBUG)
target_compile_options(hashtable_table_compare PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_table_compare_check COMMAND hashtable_table_compare check)
add_test(NAME hashtable_table_exact_check COMMAND hashtable_table_compare exact-check)
add_test(NAME hashtable_table_word_check COMMAND hashtable_table_compare word-check)

add_executable(hashtable_table_ring table_ring.cpp "${hashtable_table_plans}")
sixdb_target(hashtable_table_ring)
target_link_libraries(hashtable_table_ring PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital loom::loom)
target_include_directories(hashtable_table_ring PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(hashtable_table_ring PRIVATE NDEBUG)
target_compile_options(hashtable_table_ring PRIVATE -fno-vectorize -fno-slp-vectorize)
# Non-divisible streams, mixed misses, real secondary/chain pressure and aging.
add_test(NAME hashtable_table_ring_check COMMAND hashtable_table_ring 5 701 2 1 8 1 37913)

add_executable(hashtable_table_exact_ring table_ring.cpp "${hashtable_table_plans}")
sixdb_target(hashtable_table_exact_ring)
target_link_libraries(hashtable_table_exact_ring PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital loom::loom)
target_include_directories(hashtable_table_exact_ring PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(hashtable_table_exact_ring PRIVATE NDEBUG HASHTABLE_EXACT_KEYS)
target_compile_options(hashtable_table_exact_ring PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_table_exact_ring_check COMMAND hashtable_table_exact_ring 5 701 2 1 8 1 37913)

add_executable(hashtable_table_word_ring table_ring.cpp "${hashtable_table_plans}")
sixdb_target(hashtable_table_word_ring)
target_link_libraries(hashtable_table_word_ring PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital loom::loom)
target_include_directories(hashtable_table_word_ring PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(hashtable_table_word_ring PRIVATE NDEBUG HASHTABLE_WORD_KEYS)
target_compile_options(hashtable_table_word_ring PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_table_word_ring_check COMMAND hashtable_table_word_ring 8 5000 2 1 8 1 37913)

add_executable(hashtable_table_values table_values.cpp "${hashtable_table_plans}")
sixdb_target(hashtable_table_values)
target_link_libraries(hashtable_table_values PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_include_directories(hashtable_table_values PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(hashtable_table_values PRIVATE NDEBUG)
target_compile_options(hashtable_table_values PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_table_values_check COMMAND hashtable_table_values check)

add_executable(hashtable_growth growth.cpp "${hashtable_table_plans}" "${hashtable_single_plans}")
sixdb_target(hashtable_growth)
target_link_libraries(hashtable_growth PRIVATE hashtable_abseil_baseline
  ikea::seriespack ikea::tuplepack ikea::streampack orbital::orbital)
target_include_directories(hashtable_growth PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/orbital/src")
target_compile_definitions(hashtable_growth PRIVATE NDEBUG)
target_compile_options(hashtable_growth PRIVATE -fno-vectorize -fno-slp-vectorize)
add_test(NAME hashtable_growth_check COMMAND hashtable_growth check)
