# Opt-in experimental baseline. No Objects API is exported by the runtime module.
add_library(orbital_mvcc_baseline STATIC
  baseline/src/catalog.cpp baseline/src/metadata.cpp baseline/src/representation.cpp)
sixdb_target(orbital_mvcc_baseline)
target_include_directories(orbital_mvcc_baseline PUBLIC baseline/include)
target_link_libraries(orbital_mvcc_baseline PUBLIC orbital::orbital)

add_executable(orbital_object_check baseline/test/object.cpp)
sixdb_target(orbital_object_check)
target_link_libraries(orbital_object_check PRIVATE orbital_mvcc_baseline)
add_test(NAME orbital_object_check COMMAND orbital_object_check)

add_executable(loom_object_access_check callers/loom.cpp)
sixdb_target(loom_object_access_check)
target_link_libraries(loom_object_access_check
  PRIVATE orbital_mvcc_baseline loom::loom ikea::tuplepack)
add_test(NAME loom_object_access_check COMMAND loom_object_access_check)

add_library(orbital_mvcc_interfaces OBJECT baseline/interfaces.cpp)
sixdb_target(orbital_mvcc_interfaces)
target_include_directories(orbital_mvcc_interfaces PRIVATE
  "${PROJECT_SOURCE_DIR}/workbench/design/orbital/include")
target_link_libraries(orbital_mvcc_interfaces PRIVATE orbital_mvcc_baseline)

include("${PROJECT_SOURCE_DIR}/workbench/benchmarks/orbital/workload.cmake")
add_subdirectory(consumer)
add_subdirectory(performance)

foreach(check IN ITEMS orbital_versioned_lmdb_check orbital_versioned_body_check
    orbital_versioned_performance_check)
  add_test(NAME ${check} COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/check_database.py"
    $<TARGET_FILE:${check}>)
endforeach()
set_tests_properties(orbital_object_check loom_object_access_check
  orbital_versioned_lmdb_check orbital_versioned_body_check
  orbital_versioned_performance_check
  PROPERTIES LABELS orbital-mvcc TIMEOUT 60)
add_custom_target(orbital_mvcc_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -L "^orbital-mvcc$"
  DEPENDS orbital_object_check loom_object_access_check orbital_mvcc_interfaces
    orbital_versioned_data_check orbital_versioned_check
    orbital_versioned_lmdb_check orbital_versioned_body_check
    orbital_versioned_performance_check
  USES_TERMINAL VERBATIM)
