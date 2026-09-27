find_package(Threads REQUIRED)
add_executable(orbital_local_handoff probe.cpp)
sixdb_target(orbital_local_handoff)
target_link_libraries(orbital_local_handoff PRIVATE Threads::Threads)
