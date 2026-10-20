find_package(Threads REQUIRED)
add_executable(memory_characterisation_probe probe.cpp)
sixdb_target(memory_characterisation_probe)
target_link_libraries(memory_characterisation_probe PRIVATE Threads::Threads workbench::pmu)

add_executable(memory_adjacent_probe adjacent.cpp)
sixdb_target(memory_adjacent_probe)
target_compile_options(memory_adjacent_probe PRIVATE -fno-vectorize -fno-slp-vectorize)
target_link_libraries(memory_adjacent_probe PRIVATE Threads::Threads)

add_executable(memory_adjacent_runtime adjacent_runtime.cpp)
sixdb_target(memory_adjacent_runtime)
target_link_libraries(memory_adjacent_runtime PRIVATE loom::loom)
