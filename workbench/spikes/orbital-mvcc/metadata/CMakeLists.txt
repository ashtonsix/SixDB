add_subdirectory(orbital/descriptor)
add_subdirectory(orbital/shape)

add_library(orbital_mvcc_directory STATIC orbital/directory/object_directory.cpp)
sixdb_target(orbital_mvcc_directory)
target_link_libraries(orbital_mvcc_directory PUBLIC orbital::orbital)

add_library(orbital_mvcc_fixture STATIC composed/fixture.cpp)
sixdb_target(orbital_mvcc_fixture)
target_link_libraries(orbital_mvcc_fixture PUBLIC orbital_versioned_data loom::loom)

set(metadata_checks)
add_library(orbital_mvcc_write_batch STATIC consumer/write_batch.cpp)
sixdb_target(orbital_mvcc_write_batch)
target_include_directories(orbital_mvcc_write_batch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(orbital_mvcc_write_batch PUBLIC orbital_versioned_data)

add_library(orbital_mvcc_shape_read STATIC consumer/shape_read.cpp consumer/shape_read_bridge.cpp)
sixdb_target(orbital_mvcc_shape_read)
target_link_libraries(orbital_mvcc_shape_read PUBLIC orbital_mvcc_shape)

add_library(orbital_mvcc_shape_write STATIC consumer/shape_write.cpp)
sixdb_target(orbital_mvcc_shape_write)
target_link_libraries(orbital_mvcc_shape_write PUBLIC orbital_mvcc_shape_read orbital_mvcc_write_batch)

add_library(orbital_mvcc_point_write STATIC consumer/point_write.cpp)
sixdb_target(orbital_mvcc_point_write)
target_link_libraries(orbital_mvcc_point_write PUBLIC orbital_mvcc_shape_write)

add_library(orbital_mvcc_catalog STATIC composed/catalog.cpp)
sixdb_target(orbital_mvcc_catalog)
target_link_libraries(orbital_mvcc_catalog PUBLIC orbital_mvcc_shape)

add_library(orbital_mvcc_composed STATIC composed/material.cpp composed/read_fusion.cpp)
add_executable(orbital_mvcc_composed_read_fusion_check composed/read_fusion_check.cpp)
sixdb_target(orbital_mvcc_composed_read_fusion_check)
target_link_libraries(orbital_mvcc_composed_read_fusion_check PRIVATE orbital_mvcc_composed)
add_test(NAME orbital_mvcc_composed_read_fusion_check COMMAND orbital_mvcc_composed_read_fusion_check)
list(APPEND metadata_checks orbital_mvcc_composed_read_fusion_check)
sixdb_target(orbital_mvcc_composed)
target_link_libraries(orbital_mvcc_composed PUBLIC
  orbital_mvcc_catalog orbital_mvcc_fixture orbital_mvcc_point_write
  orbital_mvcc_shape_read)

add_executable(orbital_mvcc_composed_check composed/check.cpp)
sixdb_target(orbital_mvcc_composed_check)
target_link_libraries(orbital_mvcc_composed_check PRIVATE orbital_mvcc_composed)
add_test(NAME orbital_mvcc_composed_check COMMAND orbital_mvcc_composed_check)
list(APPEND metadata_checks orbital_mvcc_composed_check)
add_executable(orbital_mvcc_composed_append_check composed/append_check.cpp)
sixdb_target(orbital_mvcc_composed_append_check)
target_link_libraries(orbital_mvcc_composed_append_check PRIVATE orbital_mvcc_composed)
add_test(NAME orbital_mvcc_composed_append_check COMMAND orbital_mvcc_composed_append_check)
list(APPEND metadata_checks orbital_mvcc_composed_append_check)

add_executable(loom_object_module_check ../callers/object_module.cpp)
sixdb_target(loom_object_module_check)
target_link_libraries(loom_object_module_check PRIVATE orbital_mvcc_catalog loom::loom)
add_test(NAME loom_object_module_check COMMAND loom_object_module_check)
list(APPEND metadata_checks loom_object_module_check)

# Current Loom borrower/lifecycle integration, independent of retired timings.
add_library(loom_object_ring STATIC ../callers/object_ring.cpp)
sixdb_target(loom_object_ring)
target_link_libraries(loom_object_ring PUBLIC orbital_mvcc_catalog orbital_mvcc_shape_read loom::loom)

add_executable(loom_object_ring_check ../callers/object_ring_check.cpp)
sixdb_target(loom_object_ring_check)
target_link_libraries(loom_object_ring_check PRIVATE loom_object_ring)
add_test(NAME loom_object_ring_check COMMAND loom_object_ring_check)
list(APPEND metadata_checks loom_object_ring_check)

# The selected Object module runs the same consequence histories as the controls.
foreach(pair IN ITEMS "catalog|check" "shape|shape_store_check" "bounded|bounded_check")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 label)
  list(GET fields 1 source)
  set(target orbital_object_${label}_check)
  add_executable(${target} orbital/candidates/${source}.cpp)
  sixdb_target(${target})
  target_compile_definitions(${target} PRIVATE SIXDB_OBJECT_MODULE_TEST=1)
  target_link_libraries(${target} PRIVATE orbital_mvcc_shape orbital_mvcc_descriptor orbital_mvcc_directory)
  add_test(NAME ${target} COMMAND ${target})
  list(APPEND metadata_checks ${target})
endforeach()

foreach(check IN LISTS metadata_checks)
  target_compile_options(${check} PRIVATE -UNDEBUG)
endforeach()
set_tests_properties(${metadata_checks} PROPERTIES LABELS orbital-mvcc-metadata TIMEOUT 60)
add_custom_target(orbital_mvcc_metadata_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -L "^orbital-mvcc-metadata$"
  DEPENDS ${metadata_checks} USES_TERMINAL VERBATIM)
