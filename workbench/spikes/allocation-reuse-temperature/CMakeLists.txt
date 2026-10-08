add_executable(allocation_reuse_temperature_probe probe.cpp)
sixdb_target(allocation_reuse_temperature_probe)
add_test(NAME allocation_reuse_temperature_policy
  COMMAND allocation_reuse_temperature_probe --check)

add_executable(allocation_reuse_temperature_allocator allocator.cpp)
target_link_libraries(allocation_reuse_temperature_allocator PRIVATE loom)
target_link_options(allocation_reuse_temperature_allocator PRIVATE
  -Wl,--wrap=mmap,--wrap=munmap,--wrap=mprotect)
sixdb_target(allocation_reuse_temperature_allocator)
