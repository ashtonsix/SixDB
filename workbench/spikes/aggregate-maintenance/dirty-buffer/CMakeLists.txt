find_package(Threads REQUIRED)
add_executable(dirty_buffer_probe probe.cpp)
sixdb_target(dirty_buffer_probe)
target_link_libraries(dirty_buffer_probe PRIVATE benchmark::benchmark Threads::Threads)
