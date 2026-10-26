add_library(ikea_core STATIC src/overlap.cpp)
sixdb_target(ikea_core)
target_include_directories(ikea_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(ikea_intervals STATIC src/intervals/search.cpp)
add_library(ikea::intervals ALIAS ikea_intervals)
sixdb_target(ikea_intervals)
target_include_directories(ikea_intervals PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_intervals PUBLIC cxx_std_23)

foreach(example ordinary composition)
  add_executable(ikea_example_intervals_${example} EXCLUDE_FROM_ALL examples/intervals/${example}.cpp)
  sixdb_target(ikea_example_intervals_${example})
  target_link_libraries(ikea_example_intervals_${example} PRIVATE ikea::intervals)
endforeach()
target_link_libraries(ikea_example_intervals_composition PRIVATE ikea::seriespack)

foreach(check search cuts sparse seams wide)
  add_executable(ikea_intervals_${check}_check EXCLUDE_FROM_ALL test/intervals/${check}.cpp)
  sixdb_target(ikea_intervals_${check}_check)
  target_compile_options(ikea_intervals_${check}_check PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(ikea_intervals_${check}_check PRIVATE ikea::intervals)
endforeach()

add_library(ikea_streampack STATIC src/streampack/layout.cpp src/streampack/view.cpp
  src/streampack/compact/description.cpp src/streampack/compact/view.cpp)
add_library(ikea::streampack ALIAS ikea_streampack)
sixdb_target(ikea_streampack)
target_include_directories(ikea_streampack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_streampack PUBLIC cxx_std_23)
target_link_libraries(ikea_streampack PUBLIC ikea::intervals)
add_executable(ikea_streampack_check EXCLUDE_FROM_ALL test/streampack/check.cpp)
sixdb_target(ikea_streampack_check)
target_compile_options(ikea_streampack_check PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(ikea_streampack_check PRIVATE ikea::streampack)
add_executable(ikea_streampack_boundaries_check EXCLUDE_FROM_ALL test/streampack/boundaries.cpp)
sixdb_target(ikea_streampack_boundaries_check)
target_compile_options(ikea_streampack_boundaries_check PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(ikea_streampack_boundaries_check PRIVATE ikea::streampack)
add_executable(ikea_streampack_encodings_check EXCLUDE_FROM_ALL test/streampack/encodings.cpp)
sixdb_target(ikea_streampack_encodings_check)
target_compile_options(ikea_streampack_encodings_check PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(ikea_streampack_encodings_check PRIVATE ikea::streampack)
foreach(check geometry cuts edits)
  add_executable(ikea_streampack_compact_${check}_check EXCLUDE_FROM_ALL test/streampack/compact/${check}.cpp)
  sixdb_target(ikea_streampack_compact_${check}_check)
  target_compile_options(ikea_streampack_compact_${check}_check PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(ikea_streampack_compact_${check}_check PRIVATE ikea::streampack)
endforeach()
foreach(example ordinary composition)
  add_executable(ikea_example_streampack_${example} EXCLUDE_FROM_ALL examples/streampack/${example}.cpp)
  sixdb_target(ikea_example_streampack_${example})
  target_compile_options(ikea_example_streampack_${example} PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(ikea_example_streampack_${example} PRIVATE ikea::streampack)
endforeach()

add_library(ikea_seriespack STATIC
  src/seriespack/readers.cpp src/seriespack/readers_ranges.cpp src/seriespack/admission.cpp src/seriespack/layout.cpp
  src/seriespack/writers_u8.cpp src/seriespack/writers_u16.cpp
  src/seriespack/writers_u32.cpp src/seriespack/writers_u64.cpp
  src/seriespack/operations_1.cpp src/seriespack/operations_5.cpp src/seriespack/operations_9.cpp src/seriespack/operations_13.cpp
  src/seriespack/operations_17.cpp src/seriespack/operations_21.cpp src/seriespack/operations_25.cpp src/seriespack/operations_29.cpp
  src/seriespack/operations_33.cpp src/seriespack/operations_37.cpp src/seriespack/operations_41.cpp src/seriespack/operations_45.cpp
  src/seriespack/operations_49.cpp src/seriespack/operations_53.cpp src/seriespack/operations_57.cpp src/seriespack/operations_61.cpp)
add_library(ikea::seriespack ALIAS ikea_seriespack)
sixdb_target(ikea_seriespack)
target_include_directories(ikea_seriespack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_seriespack PUBLIC cxx_std_23)
target_link_libraries(ikea_seriespack PUBLIC ikea_core)

add_library(ikea_tuplepack STATIC src/tuplepack/layout.cpp src/tuplepack/initialize.cpp
  src/tuplepack/prepare.cpp src/tuplepack/kernels.cpp src/tuplepack/gpr.cpp src/tuplepack/routes.cpp
  src/tuplepack/packet_layout.cpp src/tuplepack/packet_prepare.cpp src/tuplepack/packet.cpp)
add_library(ikea::tuplepack ALIAS ikea_tuplepack)
sixdb_target(ikea_tuplepack)
target_include_directories(ikea_tuplepack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_tuplepack PUBLIC cxx_std_23)
target_link_libraries(ikea_tuplepack PUBLIC ikea_core)

add_library(ikea_bec STATIC src/bec/bec256.cpp src/bec/validation.cpp src/bec/analysis.cpp
  src/bec/bec4096.cpp src/bec/common.cpp)
add_library(ikea::bec ALIAS ikea_bec)
sixdb_target(ikea_bec)
target_include_directories(ikea_bec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_bec PUBLIC cxx_std_23)
target_link_libraries(ikea_bec PUBLIC ikea_core)
add_executable(ikea_bec_check EXCLUDE_FROM_ALL test/bec/check.cpp test/bec/analysis.cpp test/bec/block.cpp)
sixdb_target(ikea_bec_check)
target_link_libraries(ikea_bec_check PRIVATE ikea::bec)
add_executable(ikea_bec4096_check EXCLUDE_FROM_ALL test/bec/bec4096.cpp)
sixdb_target(ikea_bec4096_check)
target_link_libraries(ikea_bec4096_check PRIVATE ikea::bec)
add_executable(ikea_bec_composition_check EXCLUDE_FROM_ALL test/bec/composition.cpp)
sixdb_target(ikea_bec_composition_check)
target_link_libraries(ikea_bec_composition_check PRIVATE ikea::bec)
foreach(example ordinary native integration bec4096)
  add_executable(ikea_example_bec_${example} EXCLUDE_FROM_ALL examples/bec/${example}.cpp)
  sixdb_target(ikea_example_bec_${example})
  target_link_libraries(ikea_example_bec_${example} PRIVATE ikea::bec)
endforeach()
target_link_libraries(ikea_example_bec_integration PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_wire_check EXCLUDE_FROM_ALL test/tuplepack/wire.cpp)
sixdb_target(ikea_tuplepack_wire_check)
target_link_libraries(ikea_tuplepack_wire_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_transfer_check EXCLUDE_FROM_ALL test/tuplepack/transfer.cpp)
sixdb_target(ikea_tuplepack_transfer_check)
target_link_libraries(ikea_tuplepack_transfer_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_operations_check EXCLUDE_FROM_ALL test/tuplepack/operations.cpp)
sixdb_target(ikea_tuplepack_operations_check)
target_link_libraries(ikea_tuplepack_operations_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_execution_check EXCLUDE_FROM_ALL test/tuplepack/execution.cpp)
sixdb_target(ikea_tuplepack_execution_check)
target_link_libraries(ikea_tuplepack_execution_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_packets_check EXCLUDE_FROM_ALL test/tuplepack/packets.cpp)
sixdb_target(ikea_tuplepack_packets_check)
target_link_libraries(ikea_tuplepack_packets_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_access_check EXCLUDE_FROM_ALL test/tuplepack/access.cpp)
sixdb_target(ikea_tuplepack_access_check)
target_link_libraries(ikea_tuplepack_access_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_predicate_check EXCLUDE_FROM_ALL test/tuplepack/predicate.cpp)
sixdb_target(ikea_tuplepack_predicate_check)
target_link_libraries(ikea_tuplepack_predicate_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_gpr_check EXCLUDE_FROM_ALL
  test/tuplepack/gpr.cpp test/tuplepack/gpr_pipeline.cpp test/tuplepack/gpr_carriers.cpp)
sixdb_target(ikea_tuplepack_gpr_check)
target_link_libraries(ikea_tuplepack_gpr_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_ownership_check EXCLUDE_FROM_ALL test/tuplepack/ownership.cpp)
sixdb_target(ikea_tuplepack_ownership_check)
target_link_libraries(ikea_tuplepack_ownership_check PRIVATE ikea::tuplepack)

add_executable(ikea_seriespack_check EXCLUDE_FROM_ALL
  test/seriespack/support.cpp test/seriespack/read/wire.cpp test/seriespack/read/composition.cpp
  test/seriespack/read/range.cpp test/seriespack/read/range_striped.cpp
  test/seriespack/mutation/range.cpp test/seriespack/mutation/substitution.cpp
  test/seriespack/execution/chain.cpp test/seriespack/mutation/construction.cpp
  test/seriespack/mutation/widths_1_8.cpp test/seriespack/mutation/widths_9_16.cpp
  test/seriespack/mutation/widths_17_24.cpp test/seriespack/mutation/widths_25_32.cpp
  test/seriespack/mutation/widths_33_40.cpp test/seriespack/mutation/widths_41_48.cpp
  test/seriespack/mutation/widths_49_56.cpp test/seriespack/mutation/widths_57_64.cpp
  test/seriespack/mutation/dense.cpp test/seriespack/mutation/verify.cpp)
sixdb_target(ikea_seriespack_check)
target_link_libraries(ikea_seriespack_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_descriptor_check EXCLUDE_FROM_ALL
  test/seriespack/format/descriptor.cpp test/seriespack/support.cpp)
sixdb_target(ikea_seriespack_descriptor_check)
target_link_libraries(ikea_seriespack_descriptor_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_ownership_check EXCLUDE_FROM_ALL
  test/seriespack/integration/ownership.cpp test/seriespack/support.cpp)
sixdb_target(ikea_seriespack_ownership_check)
target_link_libraries(ikea_seriespack_ownership_check PRIVATE ikea::seriespack)

add_executable(ikea_seriespack_admission_check EXCLUDE_FROM_ALL
  test/seriespack/mutation/admission.cpp test/seriespack/support.cpp)
sixdb_target(ikea_seriespack_admission_check)
target_link_libraries(ikea_seriespack_admission_check PRIVATE ikea::seriespack)

add_executable(ikea_filters_check EXCLUDE_FROM_ALL test/filters/check.cpp test/filters/tuple.cpp
  test/filters/local_equal.cpp
  test/filters/masks.cpp
  test/filters/custom.cpp
  test/filters/tiles.cpp
  test/filters/tiles_1.cpp
  test/filters/tiles_9.cpp
  test/filters/tiles_17.cpp
  test/filters/tiles_25.cpp
  test/filters/tiles_33.cpp
  test/filters/tiles_41.cpp
  test/filters/tiles_49.cpp
  test/filters/tiles_57.cpp
  test/filters/guarded.cpp
  test/filters/widths_1.cpp
  test/filters/widths_9.cpp
  test/filters/widths_17.cpp
  test/filters/widths_25.cpp
  test/filters/widths_33.cpp
  test/filters/widths_41.cpp
  test/filters/widths_49.cpp
  test/filters/widths_57.cpp
)
sixdb_target(ikea_filters_check)
target_compile_options(ikea_filters_check PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(ikea_filters_check PRIVATE ikea::seriespack ikea::tuplepack)

foreach(example ordinary composition guarded)
  add_executable(ikea_example_filters_${example} EXCLUDE_FROM_ALL examples/filters/${example}.cpp)
  sixdb_target(ikea_example_filters_${example})
  target_compile_options(ikea_example_filters_${example} PRIVATE -fno-vectorize -fno-slp-vectorize)
  target_link_libraries(ikea_example_filters_${example} PRIVATE ikea::seriespack ikea::tuplepack)
endforeach()

foreach(example ordinary composition pipeline integration)
  add_executable(ikea_example_seriespack_${example} EXCLUDE_FROM_ALL examples/seriespack/${example}.cpp)
  sixdb_target(ikea_example_seriespack_${example})
  target_link_libraries(ikea_example_seriespack_${example} PRIVATE ikea::seriespack)
endforeach()
foreach(example ordinary composition integration packets words groups)
  add_executable(ikea_example_tuplepack_${example} EXCLUDE_FROM_ALL examples/tuplepack/${example}.cpp)
  sixdb_target(ikea_example_tuplepack_${example})
  target_link_libraries(ikea_example_tuplepack_${example} PRIVATE ikea::tuplepack)
endforeach()
set(ikea_example_targets
  ikea_example_intervals_ordinary ikea_example_intervals_composition
  ikea_example_filters_ordinary ikea_example_filters_composition ikea_example_filters_guarded
  ikea_example_streampack_ordinary ikea_example_streampack_composition
  ikea_example_seriespack_ordinary ikea_example_seriespack_composition
  ikea_example_seriespack_pipeline ikea_example_seriespack_integration
  ikea_example_tuplepack_ordinary ikea_example_tuplepack_composition
  ikea_example_tuplepack_integration ikea_example_tuplepack_packets
  ikea_example_tuplepack_words ikea_example_tuplepack_groups
  ikea_example_bec_ordinary ikea_example_bec_native ikea_example_bec_integration
  ikea_example_bec_bec4096)
set(ikea_check_targets
  ikea_intervals_search_check ikea_intervals_cuts_check ikea_intervals_sparse_check ikea_intervals_seams_check ikea_intervals_wide_check
  ikea_streampack_compact_geometry_check ikea_streampack_compact_cuts_check ikea_streampack_compact_edits_check
  ikea_streampack_check ikea_streampack_boundaries_check ikea_streampack_encodings_check
  ikea_seriespack_check ikea_seriespack_descriptor_check ikea_seriespack_ownership_check
  ikea_seriespack_admission_check ikea_filters_check
  ikea_tuplepack_wire_check ikea_tuplepack_transfer_check
  ikea_tuplepack_operations_check ikea_tuplepack_execution_check
  ikea_tuplepack_packets_check ikea_tuplepack_access_check ikea_tuplepack_predicate_check
  ikea_tuplepack_gpr_check ikea_tuplepack_ownership_check
  ikea_bec_check ikea_bec4096_check ikea_bec_composition_check)
add_custom_target(ikea_examples DEPENDS ${ikea_example_targets})
sixdb_heavy_compile(ikea_seriespack_check ikea_filters_check ikea_tuplepack_packets_check
  ikea_intervals_sparse_check ikea_streampack_compact_geometry_check ikea_streampack_encodings_check)
sixdb_test_scope(ikea_seriespack_check ikea_filters_check)
foreach(target IN LISTS ikea_check_targets ikea_example_targets)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endforeach()
add_test(NAME ikea_seriespack_reference
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/test/seriespack/reference/check.py")
add_custom_target(ikea_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -R "^ikea_"
  DEPENDS ${ikea_check_targets} ikea_examples
  USES_TERMINAL)

include(cmake/Hashtable.cmake)

add_executable(ikea_contracts_check EXCLUDE_FROM_ALL test/contracts.cpp)
sixdb_target(ikea_contracts_check)
target_link_libraries(ikea_contracts_check PRIVATE ikea::seriespack ikea::tuplepack ikea::streampack ikea::bec)
add_test(NAME ikea_contracts_check COMMAND $<TARGET_FILE:ikea_contracts_check>)
add_dependencies(ikea_validate ikea_contracts_check)

add_executable(ikea_selection_check EXCLUDE_FROM_ALL test/selection.cpp)
sixdb_target(ikea_selection_check)
target_link_libraries(ikea_selection_check PRIVATE ikea::intervals)
add_test(NAME ikea_selection_check COMMAND $<TARGET_FILE:ikea_selection_check>)
add_dependencies(ikea_validate ikea_selection_check)

include(cmake/Frame.cmake)
