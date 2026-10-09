# Metadata controls are independently runnable from the withdrawn baseline suite.
add_subdirectory(orbital/descriptor)

add_executable(orbital_mvcc_candidates_check orbital/candidates/check.cpp)
sixdb_target(orbital_mvcc_candidates_check)
target_link_libraries(orbital_mvcc_candidates_check PRIVATE orbital_mvcc_descriptor)
add_test(NAME orbital_mvcc_candidates_check COMMAND orbital_mvcc_candidates_check)

add_executable(orbital_mvcc_retention_check orbital/retention/check.cpp)
sixdb_target(orbital_mvcc_retention_check)
add_test(NAME orbital_mvcc_retention_check COMMAND orbital_mvcc_retention_check)

add_executable(orbital_mvcc_retention_store_check orbital/retention/store_check.cpp)
sixdb_target(orbital_mvcc_retention_store_check)
target_link_libraries(orbital_mvcc_retention_store_check PRIVATE orbital_mvcc_descriptor)
add_test(NAME orbital_mvcc_retention_store_check COMMAND orbital_mvcc_retention_store_check)

set(metadata_checks orbital_mvcc_candidates_check
  orbital_mvcc_retention_check orbital_mvcc_retention_store_check)
foreach(check IN ITEMS candidates_check compact_law_check descriptor_adapter_check
    compact_operations_check canonical_law_check compact_descriptor_check)
  set(target orbital_mvcc_consumer_${check})
  add_executable(${target} consumer/${check}.cpp)
  sixdb_target(${target})
  target_compile_definitions(${target} PRIVATE CONSUMER_METADATA_FROZEN=0)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(${target} PRIVATE orbital_versioned_consumer orbital_mvcc_descriptor)
  add_test(NAME ${target} COMMAND ${target})
  list(APPEND metadata_checks ${target})
endforeach()
# Only this comparator retains the optional program-wide preparation Schema.
target_sources(orbital_mvcc_consumer_candidates_check PRIVATE consumer/candidate_default_schema.cpp)

foreach(check IN LISTS metadata_checks)
  target_compile_options(${check} PRIVATE -UNDEBUG)
endforeach()
target_compile_options(orbital_mvcc_descriptor_check PRIVATE -UNDEBUG)
set_tests_properties(${metadata_checks} PROPERTIES LABELS orbital-mvcc-metadata TIMEOUT 60)
add_custom_target(orbital_mvcc_metadata_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -L "^orbital-mvcc-metadata$"
  DEPENDS ${metadata_checks} orbital_mvcc_descriptor_check orbital_mvcc_descriptor_abi
  USES_TERMINAL VERBATIM)
