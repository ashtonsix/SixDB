add_executable(layout_prefix_probe probe.cpp)
sixdb_target(layout_prefix_probe)
target_link_libraries(layout_prefix_probe PRIVATE ikea::tuplepack)
