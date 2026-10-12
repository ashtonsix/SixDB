add_library(loom STATIC src/worker.cpp src/ring.cpp src/hints.cpp src/hardware.cpp)
add_library(loom::loom ALIAS loom)
sixdb_target(loom)
target_include_directories(loom PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(loom PUBLIC cxx_std_23)
target_link_libraries(loom PUBLIC orbital)

add_executable(loom_worker_check EXCLUDE_FROM_ALL test/worker.cpp)
sixdb_target(loom_worker_check)
target_link_libraries(loom_worker_check PRIVATE loom)
add_test(NAME loom_worker_check COMMAND loom_worker_check)

add_executable(loom_capacity_check EXCLUDE_FROM_ALL test/capacity.cpp)
sixdb_target(loom_capacity_check)
target_link_libraries(loom_capacity_check PRIVATE loom)
add_test(NAME loom_capacity_check COMMAND loom_capacity_check)

add_executable(loom_ring_check EXCLUDE_FROM_ALL test/ring.cpp)
sixdb_target(loom_ring_check)
target_link_libraries(loom_ring_check PRIVATE loom)
add_test(NAME loom_ring_check COMMAND loom_ring_check)

add_executable(loom_hint_check EXCLUDE_FROM_ALL test/hints.cpp)
sixdb_target(loom_hint_check)
target_link_libraries(loom_hint_check PRIVATE loom ikea::tuplepack)
add_test(NAME loom_hint_check COMMAND loom_hint_check)

add_executable(loom_hardware_check EXCLUDE_FROM_ALL test/hardware.cpp)
sixdb_target(loom_hardware_check)
target_link_libraries(loom_hardware_check PRIVATE loom)
add_test(NAME loom_hardware_check COMMAND loom_hardware_check)
set_tests_properties(loom_hardware_check PROPERTIES TIMEOUT 30)

add_executable(loom_example_hints EXCLUDE_FROM_ALL examples/hints.cpp)
sixdb_target(loom_example_hints)
target_link_libraries(loom_example_hints PRIVATE loom)
add_test(NAME loom_example_hints COMMAND loom_example_hints --force-miss)
set_tests_properties(loom_example_hints PROPERTIES TIMEOUT 30)

add_executable(loom_example_hardware EXCLUDE_FROM_ALL examples/hardware.cpp)
sixdb_target(loom_example_hardware)
target_link_libraries(loom_example_hardware PRIVATE loom)
add_test(NAME loom_example_hardware COMMAND loom_example_hardware)
set_tests_properties(loom_example_hardware PROPERTIES TIMEOUT 30)

add_executable(loom_ring_probe EXCLUDE_FROM_ALL test/ring_probe.cpp)
sixdb_target(loom_ring_probe)
target_link_libraries(loom_ring_probe PRIVATE loom)

add_executable(loom_hold_probe EXCLUDE_FROM_ALL test/hold_probe.cpp)
sixdb_target(loom_hold_probe)
target_link_libraries(loom_hold_probe PRIVATE loom)

add_executable(loom_example_allocation EXCLUDE_FROM_ALL examples/allocation.cpp)
sixdb_target(loom_example_allocation)
target_link_libraries(loom_example_allocation PRIVATE loom ikea::seriespack)
add_test(NAME loom_example_allocation COMMAND loom_example_allocation)

add_executable(loom_example_capacity EXCLUDE_FROM_ALL examples/capacity.cpp)
sixdb_target(loom_example_capacity)
target_link_libraries(loom_example_capacity PRIVATE loom)
add_test(NAME loom_example_capacity COMMAND loom_example_capacity)

add_executable(loom_example_preparation EXCLUDE_FROM_ALL examples/preparation.cpp)
sixdb_target(loom_example_preparation)
target_link_libraries(loom_example_preparation PRIVATE loom)
add_test(NAME loom_example_preparation_window COMMAND loom_example_preparation window)
add_test(NAME loom_example_preparation_finite COMMAND loom_example_preparation finite)
set_tests_properties(loom_example_preparation_window loom_example_preparation_finite PROPERTIES TIMEOUT 60)

add_executable(loom_example_partition EXCLUDE_FROM_ALL examples/partition.cpp)
sixdb_target(loom_example_partition)
target_link_libraries(loom_example_partition PRIVATE loom)
add_test(NAME loom_example_partition COMMAND loom_example_partition)

add_executable(loom_budget_check EXCLUDE_FROM_ALL test/budget.cpp)
sixdb_target(loom_budget_check)
target_link_libraries(loom_budget_check PRIVATE loom)
add_test(NAME loom_budget_check COMMAND loom_budget_check)
set_tests_properties(loom_budget_check PROPERTIES TIMEOUT 60)

set_tests_properties(
  loom_worker_check loom_capacity_check loom_ring_check loom_hint_check
  loom_hardware_check loom_example_hints loom_example_hardware loom_example_allocation
  loom_example_capacity loom_example_partition loom_example_preparation_window
  loom_example_preparation_finite loom_budget_check
  PROPERTIES LABELS loom-runtime)

add_custom_target(loom_validate
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}"
    --output-on-failure --no-tests=error -L "^loom-runtime$"
  DEPENDS loom_worker_check loom_capacity_check loom_ring_check loom_hint_check loom_hardware_check loom_example_hints loom_example_hardware loom_example_allocation loom_example_capacity loom_example_partition loom_example_preparation loom_budget_check
  USES_TERMINAL VERBATIM)
