add_executable(layout_bucket_probe probe.cpp)
sixdb_target(layout_bucket_probe)
target_link_libraries(layout_bucket_probe PRIVATE ikea::tuplepack ikea::seriespack)
