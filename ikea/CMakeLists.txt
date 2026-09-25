add_library(ikea_core STATIC src/overlap.cpp)
sixdb_target(ikea_core)
target_include_directories(ikea_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(ikea_seriespack STATIC
  src/seriespack/readers.cpp src/seriespack/admission.cpp src/seriespack/representation.cpp)
add_library(ikea::seriespack ALIAS ikea_seriespack)
sixdb_target(ikea_seriespack)
target_include_directories(ikea_seriespack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_seriespack PUBLIC cxx_std_23)
target_link_libraries(ikea_seriespack PUBLIC ikea_core)

add_library(ikea_tuplepack STATIC src/tuplepack/description.cpp src/tuplepack/construction.cpp
  src/tuplepack/prepare.cpp src/tuplepack/kernels.cpp src/tuplepack/gpr.cpp src/tuplepack/routes.cpp
  src/tuplepack/packet_layout.cpp src/tuplepack/packet_prepare.cpp src/tuplepack/packet.cpp)
add_library(ikea::tuplepack ALIAS ikea_tuplepack)
sixdb_target(ikea_tuplepack)
target_include_directories(ikea_tuplepack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_tuplepack PUBLIC cxx_std_23)
target_link_libraries(ikea_tuplepack PUBLIC ikea_core)

add_library(ikea_bec256 STATIC src/bec256/codec.cpp src/bec256/validation.cpp src/bec256/analysis.cpp)
add_library(ikea::bec256 ALIAS ikea_bec256)
sixdb_target(ikea_bec256)
target_include_directories(ikea_bec256 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ikea_bec256 PUBLIC cxx_std_23)
target_link_libraries(ikea_bec256 PUBLIC ikea_core)
add_executable(ikea_bec256_check EXCLUDE_FROM_ALL test/bec256/check.cpp test/bec256/analysis.cpp)
sixdb_target(ikea_bec256_check)
target_link_libraries(ikea_bec256_check PRIVATE ikea::bec256)
add_executable(ikea_bec256_composition_check EXCLUDE_FROM_ALL test/bec256/composition.cpp)
sixdb_target(ikea_bec256_composition_check)
target_link_libraries(ikea_bec256_composition_check PRIVATE ikea::bec256)
foreach(example ordinary native integration)
  add_executable(ikea_example_bec256_${example} EXCLUDE_FROM_ALL examples/bec256/${example}.cpp)
  sixdb_target(ikea_example_bec256_${example})
  target_link_libraries(ikea_example_bec256_${example} PRIVATE ikea::bec256)
endforeach()
target_link_libraries(ikea_example_bec256_integration PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_wire_check EXCLUDE_FROM_ALL test/tuplepack/wire.cpp)
sixdb_target(ikea_tuplepack_wire_check)
target_link_libraries(ikea_tuplepack_wire_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_operations_check EXCLUDE_FROM_ALL test/tuplepack/operations.cpp)
sixdb_target(ikea_tuplepack_operations_check)
target_link_libraries(ikea_tuplepack_operations_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_execution_check EXCLUDE_FROM_ALL test/tuplepack/execution.cpp)
sixdb_target(ikea_tuplepack_execution_check)
target_link_libraries(ikea_tuplepack_execution_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_packets_check EXCLUDE_FROM_ALL test/tuplepack/packets.cpp)
sixdb_target(ikea_tuplepack_packets_check)
target_link_libraries(ikea_tuplepack_packets_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_gpr_check EXCLUDE_FROM_ALL
  test/tuplepack/gpr.cpp test/tuplepack/gpr_pipeline.cpp test/tuplepack/gpr_carriers.cpp)
sixdb_target(ikea_tuplepack_gpr_check)
target_link_libraries(ikea_tuplepack_gpr_check PRIVATE ikea::tuplepack)

add_executable(ikea_tuplepack_ownership_check EXCLUDE_FROM_ALL test/tuplepack/ownership.cpp)
sixdb_target(ikea_tuplepack_ownership_check)
target_link_libraries(ikea_tuplepack_ownership_check PRIVATE ikea::tuplepack)

add_executable(ikea_seriespack_check EXCLUDE_FROM_ALL
  test/seriespack/support.cpp test/seriespack/read/wire.cpp test/seriespack/read/composition.cpp
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
  ikea_example_seriespack_ordinary ikea_example_seriespack_composition
  ikea_example_seriespack_pipeline ikea_example_seriespack_integration
  ikea_example_tuplepack_ordinary ikea_example_tuplepack_composition
  ikea_example_tuplepack_integration ikea_example_tuplepack_packets
  ikea_example_tuplepack_words ikea_example_tuplepack_groups
  ikea_example_bec256_ordinary ikea_example_bec256_native ikea_example_bec256_integration)
set(ikea_check_targets
  ikea_seriespack_check ikea_seriespack_descriptor_check ikea_seriespack_ownership_check
  ikea_tuplepack_wire_check ikea_tuplepack_operations_check ikea_tuplepack_execution_check
  ikea_tuplepack_packets_check ikea_tuplepack_gpr_check ikea_tuplepack_ownership_check
  ikea_bec256_check ikea_bec256_composition_check)
add_custom_target(ikea_examples DEPENDS ${ikea_example_targets})
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
