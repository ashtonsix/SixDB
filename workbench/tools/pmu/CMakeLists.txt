# Counter support is independent of database modules and peer libraries.
find_package(Threads REQUIRED)
add_library(workbench_pmu STATIC pmu.cpp)
add_library(workbench::pmu ALIAS workbench_pmu)
sixdb_target(workbench_pmu)
target_include_directories(workbench_pmu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(workbench_pmu PUBLIC Threads::Threads)

add_executable(sixdb_pmu_probe probe.cpp)
sixdb_target(sixdb_pmu_probe)
target_link_libraries(sixdb_pmu_probe PRIVATE workbench::pmu)
