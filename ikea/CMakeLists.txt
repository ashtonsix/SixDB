add_library(ikea_seriespack STATIC
  src/seriespack/view.cpp
  src/seriespack/operations.cpp
  src/seriespack/native.cpp)
add_library(ikea::seriespack ALIAS ikea_seriespack)
sixdb_target(ikea_seriespack)
target_include_directories(ikea_seriespack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_seriespack PUBLIC cxx_std_23)

add_executable(ikea_hello examples/seriespack_basics.cpp)
sixdb_target(ikea_hello)
target_link_libraries(ikea_hello PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_query EXCLUDE_FROM_ALL examples/seriespack_query.cpp)
sixdb_target(ikea_seriespack_query)
target_link_libraries(ikea_seriespack_query PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_join EXCLUDE_FROM_ALL examples/seriespack_join.cpp)
sixdb_target(ikea_seriespack_join)
target_link_libraries(ikea_seriespack_join PRIVATE ikea::seriespack)

add_custom_target(ikea_examples
  DEPENDS ikea_hello ikea_seriespack_query ikea_seriespack_join)

add_executable(ikea_seriespack_layout_view_check EXCLUDE_FROM_ALL test/seriespack/layout_view.cpp)
sixdb_target(ikea_seriespack_layout_view_check)
target_link_libraries(ikea_seriespack_layout_view_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_composition_check EXCLUDE_FROM_ALL test/seriespack/composition.cpp)
sixdb_target(ikea_seriespack_composition_check)
target_link_libraries(ikea_seriespack_composition_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_physical_check EXCLUDE_FROM_ALL test/seriespack/physical_wire.cpp)
sixdb_target(ikea_seriespack_physical_check)
target_link_libraries(ikea_seriespack_physical_check PRIVATE ikea::seriespack)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(seriespack_body_check_source test/seriespack/body_neon.cpp)
  set(seriespack_payload_check_source test/seriespack/payload_neon.cpp)
else()
  set(seriespack_body_check_source test/seriespack/body_x86.cpp)
  set(seriespack_payload_check_source test/seriespack/payload_x86.cpp)
endif()
add_executable(ikea_seriespack_body_check EXCLUDE_FROM_ALL ${seriespack_body_check_source})
sixdb_target(ikea_seriespack_body_check)
target_link_libraries(ikea_seriespack_body_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_operations_check EXCLUDE_FROM_ALL test/seriespack/operations.cpp)
sixdb_target(ikea_seriespack_operations_check)
target_link_libraries(ikea_seriespack_operations_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_payload_check EXCLUDE_FROM_ALL ${seriespack_payload_check_source})
sixdb_target(ikea_seriespack_payload_check)
target_link_libraries(ikea_seriespack_payload_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_physical_operations_check EXCLUDE_FROM_ALL test/seriespack/physical_operations.cpp)
sixdb_target(ikea_seriespack_physical_operations_check)
target_link_libraries(ikea_seriespack_physical_operations_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_range_bounds_check EXCLUDE_FROM_ALL test/seriespack/range_bounds.cpp)
sixdb_target(ikea_seriespack_range_bounds_check)
target_link_libraries(ikea_seriespack_range_bounds_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_native_composition_check EXCLUDE_FROM_ALL test/seriespack/native_composition.cpp)
sixdb_target(ikea_seriespack_native_composition_check)
target_link_libraries(ikea_seriespack_native_composition_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_grouped_composition_check EXCLUDE_FROM_ALL test/seriespack/native_grouped.cpp)
sixdb_target(ikea_seriespack_grouped_composition_check)
target_link_libraries(ikea_seriespack_grouped_composition_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_deferred_sum_check EXCLUDE_FROM_ALL test/seriespack/native_deferred_sum.cpp)
sixdb_target(ikea_seriespack_deferred_sum_check)
target_link_libraries(ikea_seriespack_deferred_sum_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_static_point_check EXCLUDE_FROM_ALL test/seriespack/static_point.cpp)
sixdb_target(ikea_seriespack_static_point_check)
target_link_libraries(ikea_seriespack_static_point_check PRIVATE ikea::seriespack)

add_custom_target(ikea_seriespack_check
  COMMAND $<TARGET_FILE:ikea_seriespack_layout_view_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_composition_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_physical_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_body_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_operations_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_payload_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_physical_operations_check> --all-available
  COMMAND $<TARGET_FILE:ikea_seriespack_range_bounds_check> --all-available
  COMMAND $<TARGET_FILE:ikea_seriespack_native_composition_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_grouped_composition_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_deferred_sum_check>
  COMMAND $<TARGET_FILE:ikea_seriespack_static_point_check>
  DEPENDS ikea_seriespack_static_point_check ikea_seriespack_native_composition_check
          ikea_seriespack_grouped_composition_check ikea_seriespack_deferred_sum_check
          ikea_seriespack_payload_check ikea_seriespack_physical_operations_check
          ikea_seriespack_range_bounds_check
          ikea_seriespack_operations_check ikea_seriespack_layout_view_check ikea_seriespack_composition_check
          ikea_seriespack_physical_check ikea_seriespack_body_check
  USES_TERMINAL)
