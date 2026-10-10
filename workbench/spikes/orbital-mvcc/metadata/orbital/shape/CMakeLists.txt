add_library(orbital_mvcc_shape STATIC shape.cpp)
sixdb_target(orbital_mvcc_shape)
target_include_directories(orbital_mvcc_shape PRIVATE "${PROJECT_SOURCE_DIR}/orbital/src")
target_include_directories(orbital_mvcc_shape PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/workbench/spikes/orbital-mvcc")
target_link_libraries(orbital_mvcc_shape PUBLIC orbital_versioned_data orbital::orbital)

add_executable(orbital_mvcc_shape_check check.cpp)
sixdb_target(orbital_mvcc_shape_check)
target_link_libraries(orbital_mvcc_shape_check PRIVATE orbital_mvcc_shape)
add_test(NAME orbital_mvcc_shape_check COMMAND orbital_mvcc_shape_check)
set_tests_properties(orbital_mvcc_shape_check PROPERTIES LABELS orbital-mvcc-metadata TIMEOUT 60)

add_library(orbital_mvcc_shape_abi OBJECT abi.cpp)
sixdb_target(orbital_mvcc_shape_abi)
target_link_libraries(orbital_mvcc_shape_abi PRIVATE orbital_mvcc_shape)
