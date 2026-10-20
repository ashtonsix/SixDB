add_executable(call_boundary_probe EXCLUDE_FROM_ALL returns.cpp callers.cpp driver.cpp ring.cpp)
sixdb_target(call_boundary_probe)
target_link_libraries(call_boundary_probe PRIVATE loom ikea::tuplepack)

add_subdirectory(access-interleaving)
